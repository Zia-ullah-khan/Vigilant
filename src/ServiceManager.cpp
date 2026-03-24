#include "../include/ServiceManager.h"
#include "../include/Logger.h"
#include "../include/httplib_vendor.h"
#include <unordered_set>

#include <iostream>
#include <cstdlib>
#include <cstdio>
#include <csignal>
#include <fstream>
#include <array>
#include <sstream>
#include <filesystem>
#include <vector>
namespace fs = std::filesystem;
#ifndef _WIN32
#include <sys/wait.h>
#include <unistd.h>
#else
#include <process.h>
#endif

#ifdef _WIN32
#define popen _popen
#define pclose _pclose
#define WNOHANG 1
#define SIGKILL 9
#ifndef SIGTERM
#define SIGTERM 15
#endif
inline int kill(pid_t, int) { return 0; }
inline pid_t waitpid(pid_t, int*, int) { return -1; }
inline pid_t fork() { return 1; }
inline void setsid() {}
inline void _exit(int status) { exit(status); }
#endif

static std::string Exec(const std::string& cmd)
{
    std::array<char, 256> buffer;
    std::string result;
    FILE* pipe = popen(cmd.c_str(), "r");

    if (!pipe)
    {
        return "";
    }

    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr)
    {
        result += buffer.data();
    }

    pclose(pipe);
    return result;
}

static std::string EscapeShellArg(const std::string& value)
{
#ifdef _WIN32
    std::string out = "\"";
    for (char c : value) {
        if (c == '"') {
            out += "\\\"";
        } else {
            out += c;
        }
    }
    out += "\"";
    return out;
#else
    std::string out = "'";
    for (char c : value) {
        if (c == '\'') {
            out += "'\\''";
        } else {
            out += c;
        }
    }
    out += "'";
    return out;
#endif
}

static std::string BuildCommand(const std::string& executable, const std::vector<std::string>& args)
{
    std::string cmd = EscapeShellArg(executable);
    for (const auto& arg : args) {
        cmd += " ";
        cmd += EscapeShellArg(arg);
    }
    return cmd;
}

static int RunCommand(const std::string& executable, const std::vector<std::string>& args)
{
    const std::string cmd = BuildCommand(executable, args);
    return std::system(cmd.c_str());
}

static std::string ExecCommand(const std::string& executable, const std::vector<std::string>& args)
{
    return Exec(BuildCommand(executable, args));
}

ServiceManager::ServiceManager(int sleepMinutes)
    : _sleepMinutes(sleepMinutes)
{
}

ServiceManager::~ServiceManager()
{
    StopReaper();
}

void ServiceManager::Register(const VigService& svc)
{
    std::unique_lock<std::shared_mutex> lock(_mapMutex);

    auto state = std::make_shared<ServiceState>();
    state->config = svc;
    state->status = ServiceStatus::SLEEPING;
    state->lastActivity = std::chrono::steady_clock::now();

    _domainToName[svc.domain] = svc.name;
    _services.emplace(svc.name, state);

    std::cout << "Registered service: " << svc.name
              << " (" << svc.domain << " -> localhost:" << svc.port << ")" << std::endl;
}

std::shared_ptr<ServiceState> ServiceManager::FindByDomain(const std::string& domain)
{
    std::shared_lock<std::shared_mutex> lock(_mapMutex);

    auto it = _domainToName.find(domain);
    if (it == _domainToName.end())
    {
        return nullptr;
    }

    auto svcIt = _services.find(it->second);
    if (svcIt == _services.end())
    {
        return nullptr;
    }

    return svcIt->second;
}

bool ServiceManager::WakeService(const std::string& name)
{
    std::shared_ptr<ServiceState> state;
    {
        std::shared_lock<std::shared_mutex> lock(_mapMutex);
        auto it = _services.find(name);
        if (it == _services.end())
        {
            std::cerr << "Unknown service: " << name << std::endl;
            return false;
        }
        state = it->second;
    }

    std::unique_lock<std::mutex> lock(state->stateMutex);

    if (state->status == ServiceStatus::RUNNING) {
        return true;
    }

    if (state->status == ServiceStatus::SLEEPING) {
        Logger::Info("Waking service (Async): " + name);
        state->status = ServiceStatus::STARTING;
        
        std::thread([this, state, name]() {
            bool started = false;
            // Since this accesses ServiceManager methods (this->StartDocker), ServiceManager must outlive the thread.
            // The daemon architecture guarantees ServiceManager lives until shutdown.
            if (state->config.type == ServiceType::Docker) {
                started = StartDocker(*state);
            } else {
                started = StartProcess(*state);
            }

            if (!started) {
                Logger::Error("Failed to start service: " + name);
            } else if (!HealthCheck(*state)) {
                Logger::Error("Health check failed for: " + name);
                started = false;
            }

            std::unique_lock<std::mutex> bgLock(state->stateMutex);
            if (started) {
                state->status = ServiceStatus::RUNNING;
                state->lastActivity = std::chrono::steady_clock::now();
                Logger::Info("Service awake: " + name);
            } else {
                state->status = ServiceStatus::SLEEPING;
            }
            bgLock.unlock();
            state->cv.notify_all();
        }).detach();
    }

    state->cv.wait(lock, [&state] { 
        return state->status == ServiceStatus::RUNNING || state->status == ServiceStatus::SLEEPING;
    });

    return state->status == ServiceStatus::RUNNING;
}

void ServiceManager::SleepService(const std::string& name)
{
    std::shared_ptr<ServiceState> state;
    {
        std::shared_lock<std::shared_mutex> lock(_mapMutex);
        auto it = _services.find(name);
        if (it == _services.end())
        {
            return;
        }
        state = it->second;
    }

    std::unique_lock<std::mutex> stateLock(state->stateMutex);

    if (state->status == ServiceStatus::SLEEPING)
    {
        return;
    }

    Logger::Info("Sleeping service: " + name);

    if (state->config.type == ServiceType::Docker)
    {
        StopDocker(*state);
    }
    else
    {
        StopProcess(*state);
    }

    state->status = ServiceStatus::SLEEPING;
    Logger::Info("Service asleep: " + name);
}

bool ServiceManager::IsAwake(const std::string& name)
{
    std::shared_ptr<ServiceState> state;
    {
        std::shared_lock<std::shared_mutex> lock(_mapMutex);
        auto it = _services.find(name);
        if (it == _services.end())
        {
            return false;
        }
        state = it->second;
    }

    std::lock_guard<std::mutex> stateLock(state->stateMutex);
    return state->status == ServiceStatus::RUNNING;
}

void ServiceManager::TouchService(const std::string& name)
{
    std::shared_ptr<ServiceState> state;
    {
        std::shared_lock<std::shared_mutex> lock(_mapMutex);
        auto it = _services.find(name);
        if (it != _services.end())
        {
            state = it->second;
        }
    }
    
    if (state)
    {
        std::lock_guard<std::mutex> stateLock(state->stateMutex);
        state->lastActivity = std::chrono::steady_clock::now();
    }
}

// --- Docker lifecycle ---

bool ServiceManager::StartDocker(ServiceState& state)
{
    std::string status = ExecCommand("docker", {
        "ps", "-a", "--filter", "name=" + state.config.container, "--format", "{{.Status}}"
    });

    if (status.find("Up") != std::string::npos)
    {
        Logger::Info("Docker container " + state.config.container + " is already running.");
        return true;
    }

    if (!status.empty())
    {
        Logger::Info("Starting existing docker container: " + state.config.container);
        int ret = RunCommand("docker", {"start", state.config.container});
        if (ret != 0) {
            Logger::Error("Failed to start existing docker container: " + state.config.container);
        }
        return (ret == 0);
    }

    Logger::Info("Running new docker container: " + state.config.container + " from image: " + state.config.image);
    int ret = RunCommand("docker", {
        "run", "-d", "--name", state.config.container,
        "-p", "127.0.0.1:" + std::to_string(state.config.port) + ":" + std::to_string(state.config.port),
        state.config.image
    });
    if (ret != 0) {
        Logger::Error("Failed to run new docker container: " + state.config.container);
    }
    return (ret == 0);
}

void ServiceManager::StopDocker(ServiceState& state)
{
    Logger::Info("Stopping docker container: " + state.config.container);
    int ret = RunCommand("docker", {"stop", state.config.container});
    if (ret != 0) {
        Logger::Error("Failed to stop docker container: " + state.config.container);
    }
}

// --- Process lifecycle ---

bool ServiceManager::StartProcess(ServiceState& state)
{
    Logger::Info("Starting process: " + state.config.command);
    pid_t pid = fork();

    if (pid < 0)
    {
        Logger::Error("Fork failed for: " + state.config.name);
        return false;
    }

    if (pid == 0)
    {
        setsid();
        
        std::error_code ec;
        fs::path logDir = fs::temp_directory_path(ec) / "vigilant" / "logs";
        fs::create_directories(logDir, ec);
        std::string logPath = (logDir / (state.config.name + ".log")).string();
        
        std::string fullCmd = state.config.command + " > \"" + logPath + "\" 2>&1";

#ifdef _WIN32
        _execl("cmd.exe", "cmd.exe", "/c", fullCmd.c_str(), nullptr);
#else
        execl("/bin/sh", "sh", "-c", fullCmd.c_str(), nullptr);
#endif
        // If execl returns, it means it failed
        Logger::Error("Execl failed for: " + state.config.name);
        _exit(1);
    }

    state.pid = pid;

    std::ofstream pidFile(state.config.pidFile);
    if (pidFile.is_open())
    {
        pidFile << pid;
        pidFile.close();
        Logger::Info("Wrote PID " + std::to_string(pid) + " to " + state.config.pidFile);
    } else {
        Logger::Error("Failed to open PID file: " + state.config.pidFile);
    }

    return true;
}

void ServiceManager::StopProcess(ServiceState& state)
{
    Logger::Info("Stopping process: " + state.config.name);
    if (state.pid > 0)
    {
        Logger::Info("Sending SIGTERM to PID " + std::to_string(state.pid));
        kill(state.pid, SIGTERM);

        int retries = 10;
        while (retries-- > 0)
        {
            int status;
            pid_t result = waitpid(state.pid, &status, WNOHANG);
            if (result != 0) // Process has exited
            {
                Logger::Info("Process " + std::to_string(state.pid) + " exited after SIGTERM.");
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }

        if (retries <= 0) {
            Logger::Warn("Process " + std::to_string(state.pid) + " did not exit after SIGTERM, sending SIGKILL.");
            kill(state.pid, SIGKILL);
            waitpid(state.pid, nullptr, WNOHANG); // Clean up zombie process
        }
        state.pid = 0;
    }

    if (!state.config.pidFile.empty())
    {
        if (std::remove(state.config.pidFile.c_str()) == 0) {
            Logger::Info("Removed PID file: " + state.config.pidFile);
        } else {
            Logger::Error("Failed to remove PID file: " + state.config.pidFile);
        }
    }
}

// --- Health check ---

bool ServiceManager::HealthCheck(const ServiceState& state)
{
    Logger::Info("Performing health check for service: " + state.config.name + " at localhost:" + std::to_string(state.config.port) + state.config.healthPath);
    int elapsed = 0;
    int interval = 1;

    while (elapsed < state.config.timeout)
    {
        try
        {
            httplib::Client client("localhost", state.config.port);
            client.set_connection_timeout(2);
            client.set_read_timeout(2);

            auto res = client.Get(state.config.healthPath);
            if (res && res->status >= 200 && res->status < 500)
            {
                Logger::Info("Health check successful for: " + state.config.name);
                return true;
            }
        }
        catch (const std::exception& e)
        {
            Logger::Warn("Health check exception for " + state.config.name + ": " + e.what());
        }
        catch (...)
        {
            Logger::Warn("Health check unknown exception for " + state.config.name);
        }

        std::this_thread::sleep_for(std::chrono::seconds(interval));
        elapsed += interval;
    }

    Logger::Error("Health check failed for: " + state.config.name + " after " + std::to_string(state.config.timeout) + " seconds.");
    return false;
}

// --- Reaper ---

void ServiceManager::StartReaper()
{
    _reaperRunning = true;
    _reaperThread = std::thread(&ServiceManager::ReaperLoop, this);
    Logger::Info("Reaper started (" + std::to_string(_sleepMinutes) + " min timeout)");
}

void ServiceManager::StopReaper()
{
    Logger::Info("Stopping reaper...");
    _reaperRunning = false;
    if (_reaperThread.joinable())
    {
        _reaperThread.join();
    }
    Logger::Info("Reaper stopped.");
}

void ServiceManager::ReaperLoop()
{
    while (_reaperRunning)
    {
        std::this_thread::sleep_for(std::chrono::seconds(30));

        if (!_reaperRunning)
        {
            break;
        }

        auto now = std::chrono::steady_clock::now();
        std::vector<std::string> toSleep;

        {
            std::shared_lock<std::shared_mutex> lock(_mapMutex);
            for (auto& [name, state] : _services)
            {
                std::lock_guard<std::mutex> stateLock(state->stateMutex);
                if (state->status != ServiceStatus::RUNNING) // Changed from !state->awake
                {
                    continue;
                }

                auto idle = std::chrono::duration_cast<std::chrono::minutes>(
                    now - state->lastActivity
                ).count();

                if (idle >= _sleepMinutes) // Using _sleepMinutes as the global timeout for now
                {
                    Logger::Info("Service " + name + " idle for " + std::to_string(idle) + " minutes, marking for sleep.");
                    toSleep.push_back(name);
                }
            }
        }

        for (const auto& name : toSleep)
        {
            SleepService(name);
        }
    }
}

void ServiceManager::ReloadConfigs(const std::vector<VigService>& newConfigs)
{
    std::unique_lock<std::shared_mutex> lock(_mapMutex);

    std::unordered_set<std::string> newNames;

    for (const auto& svc : newConfigs)
    {
        newNames.insert(svc.name);
        auto it = _services.find(svc.name);
        
        if (it != _services.end())
        {
            auto state = it->second;
            std::lock_guard<std::mutex> stateLock(state->stateMutex);
            
            // Check if critical restart-requiring fields changed
            bool needsRestart = (state->config.port != svc.port || 
                                 state->config.command != svc.command || 
                                 state->config.image != svc.image ||
                                 state->config.container != svc.container ||
                                 state->config.type != svc.type);
                                 
            if (needsRestart && (state->status == ServiceStatus::RUNNING || state->status == ServiceStatus::STARTING))
            {
                if (state->config.type == ServiceType::Docker) StopDocker(*state);
                else StopProcess(*state);
                state->status = ServiceStatus::SLEEPING;
            }

            // Check if domain changed
            if (state->config.domain != svc.domain)
            {
                _domainToName.erase(state->config.domain);
                _domainToName[svc.domain] = svc.name;
            }

            state->config = svc;
            Logger::Info("Reloaded config for service: " + svc.name);
        }
        else
        {
            auto state = std::make_shared<ServiceState>();
            state->config = svc;
            state->status = ServiceStatus::SLEEPING;
            state->lastActivity = std::chrono::steady_clock::now();

            _domainToName[svc.domain] = svc.name;
            _services.emplace(svc.name, state);
            Logger::Info("Loaded new service from hot reload: " + svc.name);
        }
    }

    std::vector<std::string> toRemove;
    for (const auto& [name, state] : _services)
    {
        if (newNames.find(name) == newNames.end())
        {
            toRemove.push_back(name);
        }
    }

    for (const auto& name : toRemove)
    {
        auto state = _services[name];
        {
            std::lock_guard<std::mutex> stateLock(state->stateMutex);
            if (state->status == ServiceStatus::RUNNING || state->status == ServiceStatus::STARTING)
            {
                if (state->config.type == ServiceType::Docker) StopDocker(*state);
                else StopProcess(*state);
            }
        }
        _domainToName.erase(state->config.domain);
        _services.erase(name);
        Logger::Info("Removed deleted service during hot reload: " + name);
    }
}
