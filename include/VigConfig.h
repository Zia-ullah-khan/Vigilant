#pragma once

#include <string>
#include <vector>
#include <unordered_map>

enum class ServiceType
{
    Docker,
    Process
};

struct VigService
{
    std::string name;
    std::string domain;
    int port = 0;
    ServiceType type = ServiceType::Process;

    // Docker fields
    std::string image;
    std::string container;

    // Process fields
    std::string command;
    std::string pidFile;

    // Health check
    std::string healthPath = "/";
    int timeout = 30;

    // Features
    int rateLimit = 0; // Requests per minute, 0 = unlimited
};

VigService ParseVigFile(const std::string& path);
std::vector<VigService> LoadAllServices(const std::string& directory);
