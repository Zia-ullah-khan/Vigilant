#include "include/VigConfig.h"
#include "include/ServiceManager.h"
#include "include/ProxyServer.h"

#include <iostream>
#include <csignal>
#include <string>

static ProxyServer* g_server = nullptr;

static void SignalHandler(int sig)
{
    std::cout << "\nShutting down (signal " << sig << ")" << std::endl;
    if (g_server)
    {
        g_server->Stop();
    }
}

static void PrintUsage(const char* program)
{
    std::cout << "Usage: " << program << " [options]\n"
              << "  -d <dir>    Service config directory (default: /etc/vigilant/services)\n"
              << "  -p <port>   Listen port (default: 9000)\n"
              << "  -t <min>    Inactivity timeout in minutes (default: 10)\n"
              << std::endl;
}

int main(int argc, char* argv[])
{
    std::string configDir = "/etc/vigilant/services";
    int listenPort = 9000;
    int sleepMinutes = 10;

    for (int i = 1; i < argc; i++)
    {
        std::string arg = argv[i];

        if (arg == "-d" && i + 1 < argc)
        {
            configDir = argv[++i];
        }
        else if (arg == "-p" && i + 1 < argc)
        {
            listenPort = std::stoi(argv[++i]);
        }
        else if (arg == "-t" && i + 1 < argc)
        {
            sleepMinutes = std::stoi(argv[++i]);
        }
        else if (arg == "-h" || arg == "--help")
        {
            PrintUsage(argv[0]);
            return 0;
        }
    }

    std::cout << "--- Vigilant v2.0 ---" << std::endl;
    std::cout << "Config directory: " << configDir << std::endl;
    std::cout << "Listen port: " << listenPort << std::endl;
    std::cout << "Sleep timeout: " << sleepMinutes << " min" << std::endl;

    std::vector<VigService> services;
    try
    {
        services = LoadAllServices(configDir);
    }
    catch (const std::exception& e)
    {
        std::cerr << "Failed to load services: " << e.what() << std::endl;
        return 1;
    }

    if (services.empty())
    {
        std::cerr << "No services found, nothing to do" << std::endl;
        return 1;
    }

    ServiceManager manager(sleepMinutes);
    for (const auto& svc : services)
    {
        manager.Register(svc);
    }

    manager.StartReaper();

    ProxyServer server(listenPort, manager);
    g_server = &server;

    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);

    server.Start();

    manager.StopReaper();

    std::cout << "Vigilant stopped" << std::endl;
    return 0;
}