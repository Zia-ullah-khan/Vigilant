#pragma once

#include "ServiceManager.h"
#include "httplib.h"

#include <string>
#include <atomic>
#include <unordered_map>
#include <deque>
#include <chrono>
#include <mutex>
#include <memory>

class ProxyServer
{
public:
    ProxyServer(int listenPort, ServiceManager& manager, const std::string& certPath = "", const std::string& keyPath = "");

    void Start();
    void Stop();

private:
    void HandleRequest(const httplib::Request& req, httplib::Response& res);
    std::string ExtractDomain(const httplib::Request& req);
    bool CheckRateLimit(const std::string& ip, int limit);

    int _listenPort;
    ServiceManager& _manager;
    std::unique_ptr<httplib::Server> _server;

    std::unordered_map<std::string, std::deque<std::chrono::steady_clock::time_point>> _rateLimits;
    std::mutex _rateMutex;
};
