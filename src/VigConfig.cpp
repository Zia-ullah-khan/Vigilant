#include "../include/VigConfig.h"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <algorithm>
#include <filesystem>
#include "../include/Logger.h"
#include "../include/httplib.h"
#include "../include/httplib_vendor.h"

#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
#include <openssl/ssl.h>
#include <openssl/err.h>
#endif

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

    svc.cert = optional("cert", "");
    svc.key = optional("key", "");

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

    //validate vig services
    for (const auto& svc : services) 
    {
        if (svc.name.empty()) {
            Logger::Error("Service with empty name in config directory. Skipping.");
            continue;
        }
        if (svc.domain.empty()) {
            Logger::Error("Service '" + svc.name + "' has empty domain. Skipping.");
            continue;
        }
        if (svc.port <= 0 || svc.port > 65535) {
            Logger::Error("Service '" + svc.name + "' has invalid port: " + std::to_string(svc.port) + ". Skipping.");
            continue;
        }
        if (svc.type == ServiceType::Docker) {
            if (svc.image.empty()) {
                Logger::Error("Docker service '" + svc.name + "' missing image. Skipping.");
                continue;
            }
            if (svc.container.empty()) {
                Logger::Error("Docker service '" + svc.name + "' missing container name. Skipping.");
                continue;
            }
        } else if (svc.type == ServiceType::Process) {
            if (svc.command.empty()) {
                Logger::Error("Process service '" + svc.name + "' missing command. Skipping.");
                continue;
            }
        }
        if (!svc.cert.empty() && !svc.key.empty()) {
            if (!fs::exists(svc.cert)) {
                Logger::Error("Service '" + svc.name + "' SSL cert file does not exist: " + svc.cert + ". Skipping.");
                continue;
            }
            if (!fs::exists(svc.key)) {
                Logger::Error("Service '" + svc.name + "' SSL key file does not exist: " + svc.key + ". Skipping.");
                continue;
            }
        }

        //validate SSL cert/key pairs
        if ((!svc.cert.empty() && svc.key.empty()) || (svc.cert.empty() && !svc.key.empty())) {
            Logger::Error("Service '" + svc.name + "' has incomplete SSL configuration. Both cert and key must be provided. Skipping.");
            continue;
        }
        // check if SSL files are valid and correct and loadable by the server
        if (!svc.cert.empty() && !svc.key.empty()) {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
            try {
                SSL_CTX* ctx = SSL_CTX_new(TLS_server_method());
                if (!ctx) {
                    throw std::runtime_error("Failed to create SSL context");
                }

                if (SSL_CTX_use_certificate_chain_file(ctx, svc.cert.c_str()) <= 0) {
                    SSL_CTX_free(ctx);
                    throw std::runtime_error("Failed to load certificate file: " + std::string(ERR_error_string(ERR_get_error(), nullptr)));
                }

                if (SSL_CTX_use_PrivateKey_file(ctx, svc.key.c_str(), SSL_FILETYPE_PEM) <= 0) {
                    SSL_CTX_free(ctx);
                    throw std::runtime_error("Failed to load private key file: " + std::string(ERR_error_string(ERR_get_error(), nullptr)));
                }

                if (!SSL_CTX_check_private_key(ctx)) {
                    SSL_CTX_free(ctx);
                    throw std::runtime_error("Private key does not match the certificate");
                }

                SSL_CTX_free(ctx);
                Logger::Info("Service '" + svc.name + "' SSL cert/key pair validated successfully.");
            } catch (const std::exception& ex) {
                Logger::Error("Service '" + svc.name + "' has invalid SSL cert/key: " + std::string(ex.what()) + ". Skipping.");
                continue;
            }
#else
            Logger::Warn("Service '" + svc.name + "' has SSL configuration but OpenSSL support is not enabled. Skipping SSL validation.");
#endif
        }
    }

    return services;
}
