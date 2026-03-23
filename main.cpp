#include "include/VigConfig.h"
#include "include/ServiceManager.h"
#include "include/ProxyServer.h"
#include "include/Logger.h"
#include "include/DashboardServer.h"

#include <iostream>
#include <csignal>
#include <string>

static ProxyServer* g_server = nullptr;
static DashboardServer* g_dash = nullptr;

static void SignalHandler(int sig)
{
    std::cout << "\nShutting down (signal " << sig << ")" << std::endl;
    if (g_server)
    {
        g_server->Stop();
    }
    if (g_dash)
    {
        g_dash->Stop();
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
    int sleepTimeoutMin = 10;
    std::string logFile = "/var/log/vigilant.log";
    int dashPort = 9001;
    std::string certFile = "";
    std::string keyFile = "";

    for (int i = 1; i < argc; ++i)
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
        else if (arg == "-dash" && i + 1 < argc)
        {
            dashPort = std::stoi(argv[++i]);
        }
        else if (arg == "--cert" && i + 1 < argc)
        {
            certFile = argv[++i];
        }
        else if (arg == "--key" && i + 1 < argc)
        {
            keyFile = argv[++i];
        }
        else if (arg == "-h" || arg == "--help")
        {
            Logger::Info("Usage: " + std::string(argv[0]) + " [-d <dir>] [-p <port>] [-t <minutes>] [-l <filepath>] [-dash <port>] [--cert <filepath>] [--key <filepath>] [-h|--help]");
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
    Logger::Info("Listen port: " + std::to_string(listenPort));
    Logger::Info("Dashboard port: " + std::to_string(dashPort));
    if (!certFile.empty() && !keyFile.empty()) {
        Logger::Info("SSL Certificate: " + certFile);
        Logger::Info("SSL Key: " + keyFile);
    }
    Logger::Info("Sleep timeout: " + std::to_string(sleepTimeoutMin) + " min");
    Logger::Info("Log file: " + logFile);

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

    if (certFile.empty() && keyFile.empty())
    {
        for (const auto& svc : services)
        {
            if (!svc.cert.empty() && !svc.key.empty())
            {
                certFile = svc.cert;
                keyFile = svc.key;
                Logger::Info("Using SSL Certificate from service " + svc.name + ": " + certFile);
                Logger::Info("Using SSL Key from service " + svc.name + ": " + keyFile);
                break;
            }
        }
    }

    ServiceManager manager(sleepTimeoutMin); // Restored constructor argument
    for (const auto& svc : services) // Changed s to svc
    {
        manager.Register(svc);
        Logger::Info("Registered service: " + svc.name + " (" + svc.domain + " -> localhost:" + std::to_string(svc.port) + ")"); // Added logging for registered services
    }

    manager.StartReaper(); // Removed timeout argument

    DashboardServer dashServer(dashPort);
    g_dash = &dashServer;
    dashServer.Start();

    ProxyServer server(listenPort, manager, certFile, keyFile);
    g_server = &server;

    std::signal(SIGINT, SignalHandler);
    std::signal(SIGTERM, SignalHandler); // Used std::signal

    Logger::Info("Starting proxy server..."); // Added logging
    server.Start();

    Logger::Info("Vigilant shutting down."); // Added logging
    manager.StopReaper();

    // std::cout << "Vigilant stopped" << std::endl; // Removed, replaced by Logger::Info
    return 0;
}