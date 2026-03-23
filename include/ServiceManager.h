#pragma once

#include "VigConfig.h"

#include <string>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <chrono>
#include <thread>

#ifdef _WIN32
typedef int pid_t;
#else
#include <sys/types.h>
#endif

struct ServiceState
{
    VigService config;
    bool awake = false;
    std::chrono::steady_clock::time_point lastActivity;
    pid_t pid = 0;
};

class ServiceManager
{
public:
    ServiceManager(int sleepMinutes = 10);
    ~ServiceManager();

    void Register(const VigService& svc);
    bool WakeService(const std::string& name);
    void SleepService(const std::string& name);
    bool IsAwake(const std::string& name);
    void TouchService(const std::string& name);

    void StartReaper();
    void StopReaper();

    ServiceState* FindByDomain(const std::string& domain);

private:
    bool StartDocker(ServiceState& state);
    bool StartProcess(ServiceState& state);
    void StopDocker(ServiceState& state);
    void StopProcess(ServiceState& state);
    bool HealthCheck(const ServiceState& state);
    void ReaperLoop();

    std::unordered_map<std::string, ServiceState> _services;
    std::unordered_map<std::string, std::string> _domainToName;
    std::mutex _mutex;

    int _sleepMinutes;
    std::atomic<bool> _reaperRunning{false};
    std::thread _reaperThread;
};
