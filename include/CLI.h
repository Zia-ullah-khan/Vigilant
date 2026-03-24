#pragma once
#include <string>
#include <vector>

namespace CLI {

struct DeployOptions {
    std::string repoUrl;
    std::string branch;
    std::string tag;
    std::string commit;
    std::string domain;
    int port = 8080;
    std::string dockerfile;
    std::string context;
    std::string container;
    std::vector<std::string> buildArgs;
    std::vector<std::string> envVars;
};

    int Deploy(const std::string& vigFile, const std::string& configDir);
    int DeployGit(const DeployOptions& options, const std::string& configDir);
    int List(const std::string& configDir);
    int Remove(const std::string& serviceName, const std::string& configDir);
    int Logs(const std::string& serviceName, const std::string& configDir);
}
