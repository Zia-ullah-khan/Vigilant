#include "include/VigConfig.h"
#include "include/ServiceManager.h"
#include "include/ProxyServer.h"
#include "include/Logger.h"

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
    int sleepTimeoutMin = 10; // Renamed from sleepMinutes
    std::string logFile = "/var/log/vigilant.log"; // Added logFile

    for (int i = 1; i < argc; ++i) // Changed i++ to ++i
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
        else if (arg == "-t" && i + 1 < argc) // Corrected condition
        {
            sleepTimeoutMin = std::stoi(argv[++i]); // Changed sleepMinutes to sleepTimeoutMin
        }
        else if (arg == "-l" && i + 1 < argc) // Added -l for log file
        {
            logFile = argv[++i];
        }
        else if (arg == "-h" || arg == "--help")
        {
            // Replaced PrintUsage call with Logger::Info
            Logger::Info("Usage: " + std::string(argv[0]) + " [-d <dir>] [-p <port>] [-t <minutes>] [-l <filepath>] [-h|--help]");
            return 0;
        }
        else // Added handling for unknown arguments
        {
            std::cerr << "Unknown argument: " << arg << "\n";
            std::cerr << "Usage: " << argv[0] << " [-d <dir>] [-p <port>] [-t <minutes>] [-l <filepath>] [-h|--help]\n";
            return 1;
        }
    }

    Logger::Init(logFile); // Initialize Logger
    Logger::Info("--- Vigilant v2.0 ---"); // Substituted cout
    Logger::Info("Config directory: " + configDir); // Substituted cout
    Logger::Info("Listen port: " + std::to_string(listenPort)); // Substituted cout
    Logger::Info("Sleep timeout: " + std::to_string(sleepTimeoutMin) + " min"); // Substituted cout
    Logger::Info("Log file: " + logFile); // Added log file info

    std::vector<VigService> services;
    try
    {
        services = LoadAllServices(configDir);
    }
    catch (const std::exception& e)
    {
        Logger::Error("Failed to load services: " + std::string(e.what())); // Substituted cerr
        return 1;
    }

    if (services.empty())
    {
        Logger::Error("No services configured. Exiting."); // Substituted cerr
        return 1;
    }

    ServiceManager manager(sleepTimeoutMin); // Restored constructor argument
    for (const auto& svc : services) // Changed s to svc
    {
        manager.Register(svc);
        Logger::Info("Registered service: " + svc.name + " (" + svc.domain + " -> localhost:" + std::to_string(svc.port) + ")"); // Added logging for registered services
    }

    manager.StartReaper(); // Removed timeout argument

    ProxyServer server(listenPort, manager);
    g_server = &server; // Corrected assignment to g_server

    std::signal(SIGINT, SignalHandler); // Used std::signal
    std::signal(SIGTERM, SignalHandler); // Used std::signal

    Logger::Info("Starting proxy server..."); // Added logging
    server.Start();

    Logger::Info("Vigilant shutting down."); // Added logging
    manager.StopReaper();

    // std::cout << "Vigilant stopped" << std::endl; // Removed, replaced by Logger::Info
    return 0;
}