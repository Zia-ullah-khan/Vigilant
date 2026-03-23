#pragma once

#include "ServiceManager.h"
#include "httplib.h"

#include <string>
#include <atomic>

class ProxyServer
{
public:
    ProxyServer(int listenPort, ServiceManager& manager);

    void Start();
    void Stop();

private:
    void HandleRequest(const httplib::Request& req, httplib::Response& res);
    std::string ExtractDomain(const httplib::Request& req);

    int _listenPort;
    ServiceManager& _manager;
    httplib::Server _server;
};
