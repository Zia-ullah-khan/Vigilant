#pragma once

#include "httplib.h"
#include <thread>
#include <atomic>

class DashboardServer
{
public:
    DashboardServer(int port);
    ~DashboardServer();

    void Start();
    void Stop();

private:
    int _port;
    httplib::Server _server;
    std::thread _thread;
    std::atomic<bool> _running{false};
};
