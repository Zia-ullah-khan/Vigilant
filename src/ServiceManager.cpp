#include "../include/ServiceManager.h"
#include "../include/httplib.h"

#include <iostream>
#include <cstdlib>
#include <cstdio>
#include <csignal>
#include <fstream>
#include <array>
#include <sstream>
#include <sys/wait.h>
#include <unistd.h>

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
inline pid_t fork() { return -1; }
inline void setsid() {}
inline int execl(const char*, ...) { return -1; }
inline void _exit(int status) { exit(status); }
#endif

static std::string Exec(const std::string& cmd)
{
    std::array<char, 256> buffer;
    std::string result;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd.c_str(), "r"), pclose);

    if (!pipe)
    {
        return "";
    }

    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe.get()) != nullptr)
    {
        result += buffer.data();
    }

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

    std::cout << "Waking service: " << name << std::endl;

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
        std::cerr << "Failed to start service: " << name << std::endl;
        return false;
    }

    if (!HealthCheck(state))
    {
        std::cerr << "Health check failed for: " << name << std::endl;
        return false;
    }

    state.awake = true;
    state.lastActivity = std::chrono::steady_clock::now();

    std::cout << "Service awake: " << name << std::endl;
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

    std::cout << "Sleeping service: " << name << std::endl;

    if (state.config.type == ServiceType::Docker)
    {
        StopDocker(state);
    }
    else
    {
        StopProcess(state);
    }

    state.awake = false;
    std::cout << "Service asleep: " << name << std::endl;
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
        return true;
    }

    if (!status.empty())
    {
        std::string startCmd = "docker start " + state.config.container;
        int ret = system(startCmd.c_str());
        return (ret == 0);
    }

    std::string runCmd = "docker run -d --name " + state.config.container
                       + " -p 127.0.0.1:" + std::to_string(state.config.port)
                       + ":" + std::to_string(state.config.port)
                       + " " + state.config.image;
    int ret = system(runCmd.c_str());
    return (ret == 0);
}

void ServiceManager::StopDocker(ServiceState& state)
{
    std::string cmd = "docker stop " + state.config.container;
    system(cmd.c_str());
}

// --- Process lifecycle ---

bool ServiceManager::StartProcess(ServiceState& state)
{
    pid_t pid = fork();

    if (pid < 0)
    {
        std::cerr << "Fork failed for: " << state.config.name << std::endl;
        return false;
    }

    if (pid == 0)
    {
        setsid();
        execl("/bin/sh", "sh", "-c", state.config.command.c_str(), nullptr);
        _exit(1);
    }

    state.pid = pid;

    std::ofstream pidFile(state.config.pidFile);
    if (pidFile.is_open())
    {
        pidFile << pid;
        pidFile.close();
    }

    return true;
}

void ServiceManager::StopProcess(ServiceState& state)
{
    if (state.pid > 0)
    {
        kill(state.pid, SIGTERM);

        int retries = 10;
        while (retries-- > 0)
        {
            int status;
            pid_t result = waitpid(state.pid, &status, WNOHANG);
            if (result != 0)
            {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }

        kill(state.pid, SIGKILL);
        waitpid(state.pid, nullptr, WNOHANG);
        state.pid = 0;
    }

    if (!state.config.pidFile.empty())
    {
        std::remove(state.config.pidFile.c_str());
    }
}

// --- Health check ---

bool ServiceManager::HealthCheck(const ServiceState& state)
{
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
                return true;
            }
        }
        catch (...)
        {
        }

        std::this_thread::sleep_for(std::chrono::seconds(interval));
        elapsed += interval;
    }

    return false;
}

// --- Reaper ---

void ServiceManager::StartReaper()
{
    _reaperRunning = true;
    _reaperThread = std::thread(&ServiceManager::ReaperLoop, this);
    std::cout << "Reaper started (" << _sleepMinutes << " min timeout)" << std::endl;
}

void ServiceManager::StopReaper()
{
    _reaperRunning = false;
    if (_reaperThread.joinable())
    {
        _reaperThread.join();
    }
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
