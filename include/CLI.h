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

struct CertOptions {
    std::vector<std::string> domains;
    std::string webroot = "/var/www/html";
    bool staging = false;
    bool dryRun = false;
    bool forceRenewal = false;
    std::string email;
    bool unsafeRegisterWithoutEmail = false;
};

    int Deploy(const std::string& vigFile, const std::string& configDir);
    int IssueCertificate(const CertOptions& options);
    int DeployGit(const DeployOptions& options, const std::string& configDir);
    int List(const std::string& configDir);
    int Remove(const std::string& serviceName, const std::string& configDir);
    int Logs(const std::string& serviceName, const std::string& configDir);
    int StartDaemon();
    int RestartDaemon();
}
