#include "../include/ServiceManager.h"
#include "../include/Logger.h"
#include "../include/httplib_vendor.h"

#include <iostream>
#include <cstdlib>
#include <cstdio>
#include <csignal>
#include <fstream>
#include <array>
#include <sstream>
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
    std::lock_guard<std::mutex> lock(_mutex);

    ServiceState state;
    state.config = svc;
    state.awake = false;
    state.lastActivity = std::chrono::steady_clock::now();

    _domainToName[svc.domain] = svc.name;
    _services.emplace(svc.name, std::move(state));

    std::cout << "Registered service: " << svc.name
              << " (" << svc.domain << " -> localhost:" << svc.port << ")" << std::endl;
}

ServiceState* ServiceManager::FindByDomain(const std::string& domain)
{
    std::lock_guard<std::mutex> lock(_mutex);

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

    return &svcIt->second;
}

bool ServiceManager::WakeService(const std::string& name)
{
    std::lock_guard<std::mutex> lock(_mutex);

    auto it = _services.find(name);
    if (it == _services.end())
    {
        std::cerr << "Unknown service: " << name << std::endl;
        return false;
    }

    ServiceState& state = it->second;

    if (state.awake)
    {
        return true;
    }

    Logger::Info("Waking service: " + name);

    bool started = false;
    if (state.config.type == ServiceType::Docker)
    {
        started = StartDocker(state);
    }
    else
    {
        started = StartProcess(state);
    }

    if (!started)
    {
        Logger::Error("Failed to start service: " + name);
        return false;
    }

    if (!HealthCheck(state))
    {
        Logger::Error("Health check failed for: " + name);
        return false;
    }

    state.awake = true;
    state.lastActivity = std::chrono::steady_clock::now();

    Logger::Info("Service awake: " + name);
    return true;
}

void ServiceManager::SleepService(const std::string& name)
{
    std::lock_guard<std::mutex> lock(_mutex);

    auto it = _services.find(name);
    if (it == _services.end())
    {
        return;
    }

    ServiceState& state = it->second;

    if (!state.awake)
    {
        return;
    }

    Logger::Info("Sleeping service: " + name);

    if (state.config.type == ServiceType::Docker)
    {
        StopDocker(state);
    }
    else
    {
        StopProcess(state);
    }

    state.awake = false;
    Logger::Info("Service asleep: " + name);
}

bool ServiceManager::IsAwake(const std::string& name)
{
    std::lock_guard<std::mutex> lock(_mutex);

    auto it = _services.find(name);
    if (it == _services.end())
    {
        return false;
    }

    return it->second.awake;
}

void ServiceManager::TouchService(const std::string& name)
{
    std::lock_guard<std::mutex> lock(_mutex);

    auto it = _services.find(name);
    if (it != _services.end())
    {
        it->second.lastActivity = std::chrono::steady_clock::now();
    }
}

// --- Docker lifecycle ---

bool ServiceManager::StartDocker(ServiceState& state)
{
    std::string checkCmd = "docker ps -a --filter name=" + state.config.container
                         + " --format '{{.Status}}'";
    std::string status = Exec(checkCmd);

    if (status.find("Up") != std::string::npos)
    {
        Logger::Info("Docker container " + state.config.container + " is already running.");
        return true;
    }

    if (!status.empty())
    {
        Logger::Info("Starting existing docker container: " + state.config.container);
        std::string startCmd = "docker start " + state.config.container;
        int ret = system(startCmd.c_str());
        if (ret != 0) {
            Logger::Error("Failed to start existing docker container: " + state.config.container);
        }
        return (ret == 0);
    }

    Logger::Info("Running new docker container: " + state.config.container + " from image: " + state.config.image);
    std::string runCmd = "docker run -d --name " + state.config.container
                       + " -p 127.0.0.1:" + std::to_string(state.config.port)
                       + ":" + std::to_string(state.config.port)
                       + " " + state.config.image;
    int ret = system(runCmd.c_str());
    if (ret != 0) {
        Logger::Error("Failed to run new docker container: " + state.config.container);
    }
    return (ret == 0);
}

void ServiceManager::StopDocker(ServiceState& state)
{
    Logger::Info("Stopping docker container: " + state.config.container);
    std::string cmd = "docker stop " + state.config.container;
    int ret = system(cmd.c_str());
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
#ifdef _WIN32
        _execl("cmd.exe", "cmd.exe", "/c", state.config.command.c_str(), nullptr);
#else
        execl("/bin/sh", "sh", "-c", state.config.command.c_str(), nullptr);
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
            std::lock_guard<std::mutex> lock(_mutex);
            for (auto& [name, state] : _services)
            {
                if (!state.awake)
                {
                    continue;
                }

                auto idle = std::chrono::duration_cast<std::chrono::minutes>(
                    now - state.lastActivity
                ).count();

                if (idle >= _sleepMinutes)
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
