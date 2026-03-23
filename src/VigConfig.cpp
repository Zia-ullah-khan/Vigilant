#include "../include/VigConfig.h"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <algorithm>
#include <filesystem>
#include "../include/Logger.h"

namespace fs = std::filesystem;

static std::string Trim(const std::string& str)
{
    auto start = str.find_first_not_of(" \t\r\n");
    if (start == std::string::npos)
    {
        return "";
    }
    auto end = str.find_last_not_of(" \t\r\n");
    return str.substr(start, end - start + 1);
}

static std::unordered_map<std::string, std::string> ParseKeyValues(const std::string& path)
{
    std::unordered_map<std::string, std::string> kv;
    std::ifstream file(path);

    if (!file.is_open())
    {
        throw std::runtime_error("Cannot open vig file: " + path);
    }

    std::string line;
    while (std::getline(file, line))
    {
        line = Trim(line);
        if (line.empty() || line[0] == '#')
        {
            continue;
        }

        auto eq = line.find('=');
        if (eq == std::string::npos)
        {
            continue;
        }

        std::string key = Trim(line.substr(0, eq));
        std::string value = Trim(line.substr(eq + 1));
        kv[key] = value;
    }

    return kv;
}

VigService ParseVigFile(const std::string& path)
{
    auto kv = ParseKeyValues(path);
    VigService svc;

    auto require = [&](const std::string& key) -> const std::string&
    {
        auto it = kv.find(key);
        if (it == kv.end())
        {
            throw std::runtime_error("Missing required key '" + key + "' in " + path);
        }
        return it->second;
    };

    auto optional = [&](const std::string& key, const std::string& fallback) -> std::string
    {
        auto it = kv.find(key);
        return (it != kv.end()) ? it->second : fallback;
    };

    svc.name = require("name");
    svc.domain = require("domain");
    svc.port = std::stoi(require("port"));

    std::string typeStr = require("type");
    if (typeStr == "docker")
    {
        svc.type = ServiceType::Docker;
        svc.image = require("image");
        svc.container = require("container");
    }
    else if (typeStr == "process")
    {
        svc.type = ServiceType::Process;
        svc.command = require("command");
        svc.pidFile = optional("pidfile", "/var/run/" + svc.name + ".pid");
    }
    else
    {
        throw std::runtime_error("Unknown service type '" + typeStr + "' in " + path);
    }

    svc.healthPath = optional("health", "/");
    svc.timeout = std::stoi(optional("timeout", "30"));
    svc.rateLimit = std::stoi(optional("ratelimit", "0"));

    return svc;
}

std::vector<VigService> LoadAllServices(const std::string& directory)
{
    std::vector<VigService> services;

    if (!fs::exists(directory))
    {
        throw std::runtime_error("Service directory does not exist: " + directory);
    }

    for (const auto& entry : fs::directory_iterator(directory))
    {
        if (entry.path().extension() == ".vig")
        {
            Logger::Info("Loading service config: " + entry.path().string());
            services.push_back(ParseVigFile(entry.path().string()));
        }
    }

    if (services.empty())
    {
        Logger::Info("Warning: no .vig files found in " + directory);
    }

    return services;
}
