#include "../include/Logger.h"

#include <iostream>
#include <fstream>
#include <mutex>
#include <chrono>
#include <ctime>

static std::ofstream g_file;
static std::mutex g_mutex;

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
    g_file.open(filepath, std::ios::app);
    if (!g_file.is_open())
    {
        std::cerr << "Failed to open log file: " << filepath << ". Falling back to console only." << std::endl;
    }
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
}
