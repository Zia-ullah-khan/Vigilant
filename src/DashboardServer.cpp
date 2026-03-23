#include "../include/DashboardServer.h"
#include "../include/DashboardHTML.h"
#include "../include/StatsManager.h"
#include "../include/Logger.h"

DashboardServer::DashboardServer(int port) : _port(port) {}

DashboardServer::~DashboardServer()
{
    Stop();
}

void DashboardServer::Start()
{
    _server.Get("/", [](const httplib::Request& req, httplib::Response& res) {
        res.set_content(DASHBOARD_HTML, "text/html");
    });
    
    _server.Get("/api/stats", [](const httplib::Request& req, httplib::Response& res) {
        res.set_content(StatsManager::Instance().GetStatsJSON(), "application/json");
        // Don't log /api/stats to avoid flooding the logs
    });

    _running = true;
    _thread = std::thread([this]() {
        Logger::Info("Dashboard listening on 0.0.0.0:" + std::to_string(_port));
        _server.listen("0.0.0.0", _port);
    });
}

void DashboardServer::Stop()
{
    if (_running)
    {
        _server.stop(); // cpp-httplib's graceful stop
        if (_thread.joinable())
        {
            _thread.join();
        }
        _running = false;
    }
}
