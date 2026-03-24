#include "include/VigConfig.h"
#include "include/CLI.h"
#include "include/ServiceManager.h"
#include "include/ProxyServer.h"
#include "include/Logger.h"
#include "include/DashboardServer.h"

#include <iostream>
#include <csignal>
#include <string>
#include <filesystem>
#include <thread>
#include <atomic>

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
    std::cout << "Usage: " << program << " <command> [options]\n"
              << "\nCommands:\n"
              << "  server      Run the proxy daemon\n"
              << "  deploy      Deploy a git repo or .vig config (e.g. vigilant deploy https://github.com/user/app)\n"
              << "  ls          List deployed services\n"
              << "  logs        Fetch 100 recent log lines for a deployed service\n"
              << "  rm          Remove a deployed service (e.g. vigilant rm app)\n"
              << "\nDeploy Options:\n"
              << "  --branch <name>      Git branch to deploy\n"
              << "  --tag <name>         Git tag to deploy\n"
              << "  --commit <sha>       Git commit to deploy\n"
              << "  --domain <name>      Route domain override\n"
              << "  --port <num>         Service port (default: 8080)\n"
              << "  --dockerfile <path>  Dockerfile path relative to repo root\n"
              << "  --context <path>     Docker build context relative to repo root\n"
              << "  --container <name>   Docker container name override\n"
              << "  --build-arg <k=v>    Docker build arg (repeatable)\n"
              << "  --env <k=v>          Runtime env var (repeatable)\n"
              << "\nServer Options:\n"
              << "  -d <dir>    Service config directory (default: /etc/vigilant/services)\n"
              << "  -p <port>   Listen port (default: 9000)\n"
              << "  -dash <pt>  Dashboard listen port (default: 9001)\n"
              << "  -t <min>    Inactivity timeout in minutes (default: 10)\n"
              << "  -l <file>   Log file path\n"
              << "  --cert <f>  Global SSL certificate file\n"
              << "  --key <f>   Global SSL key file\n"
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

    // Pre-scan for config dir to use in CLI commands
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "-d" && i + 1 < argc) {
            configDir = argv[i+1];
        }
    }

    std::string cmd = (argc > 1) ? argv[1] : "server";
    int startIndex = 1;

    if (cmd == "deploy" || cmd == "ls" || cmd == "logs" || cmd == "rm" || cmd == "server") {
        startIndex = 2;
    } else if (cmd == "-h" || cmd == "--help") {
        PrintUsage(argv[0]);
        return 0;
    } else if (cmd[0] != '-') {
        std::cerr << "Unknown command: " << cmd << "\n";
        PrintUsage(argv[0]);
        return 1;
    } else {
        cmd = "server"; // assumed based on flags
    }

    if (cmd == "deploy") {
        if (argc <= startIndex) { std::cerr << "Missing file or URL to deploy.\n"; return 1; }
        std::string target = argv[startIndex];
        if (target.find("http://") == 0 || target.find("https://") == 0 || target.find("git@") == 0 || target.find(".git") != std::string::npos) {
            CLI::DeployOptions options;
            options.repoUrl = target;

            for (int i = startIndex + 1; i < argc; ++i) {
                std::string arg = argv[i];
                if (arg == "-d" && i + 1 < argc) {
                    configDir = argv[++i];
                } else if (arg == "--branch" && i + 1 < argc) {
                    options.branch = argv[++i];
                } else if (arg == "--tag" && i + 1 < argc) {
                    options.tag = argv[++i];
                } else if (arg == "--commit" && i + 1 < argc) {
                    options.commit = argv[++i];
                } else if (arg == "--domain" && i + 1 < argc) {
                    options.domain = argv[++i];
                } else if (arg == "--port" && i + 1 < argc) {
                    options.port = std::stoi(argv[++i]);
                } else if (arg == "--dockerfile" && i + 1 < argc) {
                    options.dockerfile = argv[++i];
                } else if (arg == "--context" && i + 1 < argc) {
                    options.context = argv[++i];
                } else if (arg == "--container" && i + 1 < argc) {
                    options.container = argv[++i];
                } else if (arg == "--build-arg" && i + 1 < argc) {
                    options.buildArgs.push_back(argv[++i]);
                } else if (arg == "--env" && i + 1 < argc) {
                    options.envVars.push_back(argv[++i]);
                } else if (arg == "-h" || arg == "--help") {
                    PrintUsage(argv[0]);
                    return 0;
                } else {
                    std::cerr << "Unknown deploy argument: " << arg << "\n";
                    return 1;
                }
            }

            return CLI::DeployGit(options, configDir);
        }
        return CLI::Deploy(target, configDir);
    } else if (cmd == "ls") {
        return CLI::List(configDir);
    } else if (cmd == "logs") {
        if (argc <= startIndex) { std::cerr << "Missing service name.\n"; return 1; }
        return CLI::Logs(argv[startIndex], configDir);
    } else if (cmd == "rm") {
        if (argc <= startIndex) { std::cerr << "Missing service name to remove.\n"; return 1; }
        return CLI::Remove(argv[startIndex], configDir);
    }

    for (int i = startIndex; i < argc; ++i)
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
    if (Logger::IsFileLoggingEnabled())
    {
        Logger::Info("Log file: " + Logger::GetActiveLogPath());
    }
    else
    {
        Logger::Warn("File logging disabled; using console output only.");
    }

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

    std::unordered_map<std::string, std::pair<std::string, std::string>> domainCerts;
    
    if (!certFile.empty() && !keyFile.empty()) {
        domainCerts["default"] = {certFile, keyFile};
        Logger::Info("Loaded global CLI certificate.");
    }

    for (const auto& svc : services) {
        if (!svc.cert.empty() && !svc.key.empty()) {
            domainCerts[svc.domain] = {svc.cert, svc.key};
            Logger::Info("Registered SNI certificate for " + svc.domain);
        }
    }

    ServiceManager manager(sleepTimeoutMin);
    for (const auto& svc : services)
    {
        manager.Register(svc);
        Logger::Info("Registered service: " + svc.name + " (" + svc.domain + " -> localhost:" + std::to_string(svc.port) + ")");
    }

    manager.StartReaper();

    DashboardServer dashServer(dashPort);
    g_dash = &dashServer;
    dashServer.Start();

    ProxyServer server(listenPort, manager, domainCerts);
    g_server = &server;

    std::signal(SIGINT, SignalHandler);
    std::signal(SIGTERM, SignalHandler); // Used std::signal

    std::atomic<bool> watcherRunning{true};
    std::thread configWatcher([&]() {
        auto getLatestTime = [](const std::string& dir) {
            std::filesystem::file_time_type latest = std::filesystem::file_time_type::min();
            bool found = false;
            if (std::filesystem::exists(dir)) {
                for (const auto& entry : std::filesystem::directory_iterator(dir)) {
                    if (entry.path().extension() == ".vig") {
                        auto ftime = std::filesystem::last_write_time(entry);
                        if (!found || ftime > latest) {
                            latest = ftime;
                            found = true;
                        }
                    }
                }
            }
            return latest;
        };

        auto lastWrite = getLatestTime(configDir);
        
        while (watcherRunning) {
            std::this_thread::sleep_for(std::chrono::seconds(5));
            if (!watcherRunning) break;
            
            try {
                auto currentWrite = getLatestTime(configDir);
                if (currentWrite > lastWrite) {
                    lastWrite = currentWrite;
                    Logger::Info("Config directory change detected, hot-reloading...");
                    auto newServices = LoadAllServices(configDir);
                    manager.ReloadConfigs(newServices);
                }
            } catch (const std::exception& e) {
                Logger::Error("Config watcher error: " + std::string(e.what()));
            }
        }
    });

    Logger::Info("Starting proxy server..."); // Added logging
    const bool proxyStarted = server.Start();

    watcherRunning = false;
    if (configWatcher.joinable()) configWatcher.join();

    if (!proxyStarted) {
        Logger::Error("Vigilant shutting down due to proxy startup failure.");
        dashServer.Stop();
        return 1;
    }

    Logger::Info("Vigilant shutting down."); // Added logging

    return 0;
}