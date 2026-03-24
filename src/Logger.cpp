#include "../include/Logger.h"
#include "../include/StatsManager.h"

#include <iostream>
#include <fstream>
#include <mutex>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <cstdlib>

static std::ofstream g_file;
static std::mutex g_mutex;
static std::string g_activeLogPath;

static std::string BuildFallbackLogPath()
{
#ifdef _WIN32
    const char* localAppData = std::getenv("LOCALAPPDATA");
    if (localAppData && localAppData[0] != '\0')
    {
        return (std::filesystem::path(localAppData) / "Vigilant" / "logs" / "vigilant.log").string();
    }

    const char* userProfile = std::getenv("USERPROFILE");
    if (userProfile && userProfile[0] != '\0')
    {
        return (std::filesystem::path(userProfile) / "AppData" / "Local" / "Vigilant" / "logs" / "vigilant.log").string();
    }

    return "vigilant.log";
#else
    const char* xdgStateHome = std::getenv("XDG_STATE_HOME");
    if (xdgStateHome && xdgStateHome[0] != '\0')
    {
        return (std::filesystem::path(xdgStateHome) / "vigilant" / "vigilant.log").string();
    }

    const char* home = std::getenv("HOME");
    if (home && home[0] != '\0')
    {
        return (std::filesystem::path(home) / ".local" / "state" / "vigilant" / "vigilant.log").string();
    }

    return "/tmp/vigilant.log";
#endif
}

static bool OpenLogFile(const std::string& filepath)
{
    try
    {
        std::filesystem::path p(filepath);
        if (p.has_parent_path())
        {
            std::filesystem::create_directories(p.parent_path());
        }

        g_file.open(filepath, std::ios::app);
        if (g_file.is_open())
        {
            g_activeLogPath = filepath;
            return true;
        }
    }
    catch (...)
    {
        // Fallback handling below will log the failure path.
    }

    return false;
}

static std::string Timestamp()
{
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", std::localtime(&t));
    return std::string(buf);
}

void Logger::Init(const std::string& filepath)
{
    if (OpenLogFile(filepath))
    {
        return;
    }

    std::cerr << "Failed to open log file: " << filepath << ". Trying a user-local fallback path." << std::endl;

    const std::string fallbackPath = BuildFallbackLogPath();
    if (fallbackPath != filepath && OpenLogFile(fallbackPath))
    {
        std::cerr << "Logging to fallback file: " << fallbackPath << std::endl;
        return;
    }

    if (fallbackPath == filepath)
    {
        std::cerr << "Failed to open log file: " << filepath << ". Falling back to console only." << std::endl;
        return;
    }

    std::cerr << "Failed to open fallback log file: " << fallbackPath << ". Falling back to console only." << std::endl;
}

bool Logger::IsFileLoggingEnabled()
{
    return g_file.is_open();
}

std::string Logger::GetActiveLogPath()
{
    return g_activeLogPath;
}

void Logger::Info(const std::string& msg)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    std::string line = "[" + Timestamp() + "] [INFO] " + msg;
    std::cout << line << std::endl;
    if (g_file.is_open())
    {
        g_file << line << std::endl;
        g_file.flush();
    }
    StatsManager::Instance().RecordLog("INFO", msg);
}

void Logger::Warn(const std::string& msg)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    std::string line = "[" + Timestamp() + "] [WARN]  " + msg;
    std::cout << line << std::endl;
    if (g_file.is_open())
    {
        g_file << line << std::endl;
        g_file.flush();
    }

    StatsManager::Instance().RecordLog("WARN", msg);
}

void Logger::Error(const std::string& msg)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    std::string line = "[" + Timestamp() + "] [ERROR] " + msg;
    std::cerr << line << std::endl;
    if (g_file.is_open())
    {
        g_file << line << std::endl;
        g_file.flush();
    }
    StatsManager::Instance().RecordLog("ERROR", msg);
}
