#include "../include/CLI.h"
#include "../include/VigConfig.h"

#include <iostream>
#include <fstream>
#include <filesystem>
#include <system_error>
#include <cctype>
#include <cstdlib>
#include <array>
#include <vector>

namespace fs = std::filesystem;

namespace CLI {

static std::string EscapeShellArg(const std::string& value)
{
#ifdef _WIN32
    std::string out = "\"";
    for (char c : value) {
        if (c == '"') {
            out += "\\\"";
        } else {
            out += c;
        }
    }
    out += "\"";
    return out;
#else
    std::string out = "'";
    for (char c : value) {
        if (c == '\'') {
            out += "'\\''";
        } else {
            out += c;
        }
    }
    out += "'";
    return out;
#endif
}

static std::string BuildCommand(const std::string& executable, const std::vector<std::string>& args)
{
    std::string cmd = EscapeShellArg(executable);
    for (const auto& arg : args) {
        cmd += " ";
        cmd += EscapeShellArg(arg);
    }
    return cmd;
}

static int RunCommand(const std::string& executable, const std::vector<std::string>& args)
{
    const std::string cmd = BuildCommand(executable, args);
    return std::system(cmd.c_str());
}

static std::string RunCommandCapture(const std::string& executable, const std::vector<std::string>& args)
{
    const std::string cmd = BuildCommand(executable, args);
    std::array<char, 256> buffer{};
    std::string result;

#ifdef _WIN32
    FILE* pipe = _popen(cmd.c_str(), "r");
#else
    FILE* pipe = popen(cmd.c_str(), "r");
#endif
    if (!pipe) {
        return "";
    }

    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        result += buffer.data();
    }

#ifdef _WIN32
    _pclose(pipe);
#else
    pclose(pipe);
#endif
    return result;
}

static std::string Trim(const std::string& input)
{
    size_t start = input.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return "";
    }
    size_t end = input.find_last_not_of(" \t\r\n");
    return input.substr(start, end - start + 1);
}

static bool IsSafeRepoUrl(const std::string& repoUrl)
{
    if (repoUrl.empty() || repoUrl.size() > 2048) {
        return false;
    }

    if (repoUrl.find("http://") == 0 || repoUrl.find("https://") == 0 || repoUrl.find("git@") == 0) {
        for (char c : repoUrl) {
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '"' || c == '\'' ||
                c == '`' || c == '|' || c == '&' || c == ';' || c == '<' || c == '>' ||
                c == '$' || c == '(' || c == ')') {
                return false;
            }
        }
        return true;
    }

    return false;
}

static std::string SanitizeName(std::string value)
{
    for (char& c : value) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (!std::isalnum(static_cast<unsigned char>(c))) {
            c = '-';
        }
    }
    while (!value.empty() && value.front() == '-') {
        value.erase(value.begin());
    }
    while (!value.empty() && value.back() == '-') {
        value.pop_back();
    }
    return value.empty() ? "app" : value;
}

static std::string ExtractRepoName(const std::string& repoUrl)
{
    std::string source = repoUrl;
    size_t atPos = source.rfind(':');
    size_t slashPos = source.find_last_of('/');
    size_t splitPos = std::string::npos;

    if (slashPos != std::string::npos) {
        splitPos = slashPos;
    }
    if (atPos != std::string::npos && (splitPos == std::string::npos || atPos > splitPos)) {
        splitPos = atPos;
    }

    std::string name = (splitPos == std::string::npos) ? source : source.substr(splitPos + 1);
    if (name.size() > 4 && name.substr(name.size() - 4) == ".git") {
        name = name.substr(0, name.size() - 4);
    }

    return SanitizeName(name);
}

static bool EnsureDirectoryWritable(const fs::path& dir)
{
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) {
        return false;
    }

    const fs::path probe = dir / ".vigilant_write_probe";
    std::ofstream out(probe, std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }
    out << "ok";
    out.close();

    fs::remove(probe, ec);
    return true;
}

static fs::path ResolveWritableConfigDir(const std::string& requestedDir)
{
    const fs::path requested(requestedDir);
    if (EnsureDirectoryWritable(requested)) {
        return requested;
    }

    throw std::runtime_error(
        "Config directory '" + requested.string() + "' is not writable. "
        "Use sudo for /etc/vigilant/services or pass -d to a writable directory."
    );
}

enum class RuntimeType
{
    Unknown,
    Node,
    Python
};

static RuntimeType DetectRuntime(const fs::path& repoDir)
{
    if (fs::exists(repoDir / "package.json")) {
        return RuntimeType::Node;
    }
    if (fs::exists(repoDir / "requirements.txt")) {
        return RuntimeType::Python;
    }
    return RuntimeType::Unknown;
}

static bool IsValidCertDomain(const std::string& domain)
{
    if (domain.empty() || domain.size() > 253) {
        return false;
    }

    for (char c : domain) {
        const auto u = static_cast<unsigned char>(c);
        if (!(std::isalnum(u) || c == '.' || c == '-')) {
            return false;
        }
    }

    if (domain.front() == '.' || domain.front() == '-' ||
        domain.back() == '.' || domain.back() == '-') {
        return false;
    }

    if (domain.find("..") != std::string::npos) {
        return false;
    }

    return true;
}

static bool IsSafeWebrootPath(const std::string& path)
{
    for (char c : path) {
        if (c == ';' || c == '|' || c == '&' || c == '`' || c == '$' ||
            c == '\r' || c == '\n' || c == '<' || c == '>') {
            return false;
        }
    }
    return true;
}

static bool IsSafeCertEmail(const std::string& email)
{
    if (email.empty() || email.size() > 254) {
        return false;
    }

    for (char c : email) {
        const auto u = static_cast<unsigned char>(c);
        if (!(std::isalnum(u) || c == '@' || c == '.' || c == '_' || c == '+' || c == '-')) {
            return false;
        }
    }

    return email.find('@') != std::string::npos;
}

static bool IsValidEnvPair(const std::string& pair)
{
    const size_t eq = pair.find('=');
    if (eq == std::string::npos || eq == 0) {
        return false;
    }

    const std::string key = pair.substr(0, eq);
    for (char c : key) {
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_')) {
            return false;
        }
    }

    return true;
}

static std::string BuildProcessCommand(RuntimeType runtime, const fs::path& buildDir, int port, const std::vector<std::string>& envVars)
{
    std::string cmd;
#ifdef _WIN32
    cmd = "cd /d \"" + buildDir.string() + "\" && set PORT=" + std::to_string(port) + " && ";
#else
    cmd = "cd \"" + buildDir.string() + "\" && PORT=" + std::to_string(port) + " ";
#endif

    for (const auto& env : envVars) {
        if (!IsValidEnvPair(env)) {
            continue;
        }
#ifdef _WIN32
        const size_t eq = env.find('=');
        cmd += "set " + env.substr(0, eq) + "=" + env.substr(eq + 1) + " && ";
#else
        cmd += env + " ";
#endif
    }

    if (runtime == RuntimeType::Node) {
#ifdef _WIN32
        cmd += "npm start";
#else
        cmd += "npm start";
#endif
    } else {
#ifdef _WIN32
        cmd += "python app.py";
#else
        cmd += "python3 app.py";
#endif
    }

    return cmd;
}

static std::string BuildPidFilePath(const std::string& serviceName)
{
    std::error_code ec;
    fs::path pidDir = fs::temp_directory_path(ec) / "vigilant" / "pids";
    fs::create_directories(pidDir, ec);
    return (pidDir / (serviceName + ".pid")).string();
}

int Deploy(const std::string& vigFile, const std::string& configDir)
{
    if (!fs::exists(vigFile)) {
        std::cerr << "Error: File not found: " << vigFile << std::endl;
        return 1;
    }

    try {
        fs::path effectiveConfigDir = ResolveWritableConfigDir(configDir);
        auto svc = ParseVigFile(vigFile);
        fs::path dest = effectiveConfigDir / (svc.name + ".vig");
        fs::copy_file(vigFile, dest, fs::copy_options::overwrite_existing);
        
        std::cout << "Successfully deployed " << svc.name << " to " << dest.string() << "\n";
        std::cout << "The daemon will automatically spin it up shortly." << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error deploying service: " << e.what() << std::endl;
        return 1;
    }
}

int DeployGit(const DeployOptions& options, const std::string& configDir)
{
    if (!IsSafeRepoUrl(options.repoUrl)) {
        std::cerr << "Error: Invalid or unsafe repository URL.\n";
        return 1;
    }

    if (!options.branch.empty() && !options.tag.empty()) {
        std::cerr << "Error: Use either --branch or --tag, not both.\n";
        return 1;
    }

    if (options.port <= 0 || options.port > 65535) {
        std::cerr << "Error: Invalid port. Expected 1..65535.\n";
        return 1;
    }

    fs::path effectiveConfigDir;
    try {
        effectiveConfigDir = ResolveWritableConfigDir(configDir);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    std::string name = ExtractRepoName(options.repoUrl);

    std::cout << "Starting Git deployment for " << name << "...\n";

    fs::path buildDir = fs::temp_directory_path() / "vigilant_build" / name;

    if (fs::exists(buildDir)) {
        std::error_code ec;
        fs::remove_all(buildDir, ec);
    }
    fs::create_directories(buildDir);

    std::vector<std::string> cloneArgs = {"clone"};
    if (!options.branch.empty()) {
        cloneArgs.push_back("--branch");
        cloneArgs.push_back(options.branch);
    } else if (!options.tag.empty()) {
        cloneArgs.push_back("--branch");
        cloneArgs.push_back(options.tag);
    }
    cloneArgs.push_back(options.repoUrl);
    cloneArgs.push_back(buildDir.string());

    std::cout << "Cloning repository: " << options.repoUrl << "...\n";
    int res = RunCommand("git", cloneArgs);
    if (res != 0) {
        std::cerr << "Error: Failed to clone repository.\n";
        return 1;
    }

    if (!options.commit.empty()) {
        res = RunCommand("git", {"-C", buildDir.string(), "checkout", options.commit});
        if (res != 0) {
            std::cerr << "Error: Failed to checkout commit '" << options.commit << "'.\n";
            return 1;
        }
    }

    std::string resolvedCommit = Trim(RunCommandCapture("git", {"-C", buildDir.string(), "rev-parse", "HEAD"}));
    if (resolvedCommit.empty()) {
        resolvedCommit = options.commit;
    }

    RuntimeType runtime = DetectRuntime(buildDir);

    fs::path dockerfile;
    if (!options.dockerfile.empty()) {
        dockerfile = buildDir / fs::path(options.dockerfile);
        if (!fs::exists(dockerfile)) {
            std::cerr << "Error: Dockerfile not found at " << dockerfile.string() << "\n";
            return 1;
        }
    } else {
        dockerfile = buildDir / "Dockerfile";
    }

    if (!fs::exists(dockerfile)) {
        std::cout << "No Dockerfile detected. Looking for supported auto-build runtimes...\n";

        if (runtime == RuntimeType::Node) {
            std::cout << "Node.js project detected. Generating native Dockerfile...\n";
            std::ofstream ofs(dockerfile);
            ofs << "FROM node:18-alpine\n"
                   "WORKDIR /app\n"
                   "COPY package*.json ./\n"
                   "RUN if [ -f package-lock.json ]; then npm ci; else npm install; fi\n"
                   "COPY . .\n"
                   "RUN npm run build --if-present\n"
                   "EXPOSE 8080\n"
                   "CMD [\"npm\", \"start\"]\n";
            ofs.close();
        } else if (runtime == RuntimeType::Python) {
            std::cout << "Python project detected. Generating native Dockerfile...\n";
            std::ofstream ofs(dockerfile);
            ofs << "FROM python:3.10-slim\n"
                   "WORKDIR /app\n"
                   "COPY requirements.txt* ./\n"
                   "RUN if [ -f requirements.txt ]; then pip install -r requirements.txt; fi\n"
                   "COPY . .\n"
                   "EXPOSE 8080\n"
                   "CMD [\"python\", \"app.py\"]\n";
            ofs.close();
        } else {
            std::cerr << "Error: No Dockerfile found and auto-build runtime could not be detected.\n";
            std::cerr << "Please include a Dockerfile, package.json, or requirements.txt.\n";
            return 1;
        }
    }

    std::string shortSha = resolvedCommit.empty() ? "latest" : resolvedCommit.substr(0, std::min<size_t>(12, resolvedCommit.size()));
    std::string imageName = name + "-img-" + shortSha;
    std::vector<std::string> buildArgs = {"build", "-t", imageName, "-t", name + "-img"};
    if (!options.dockerfile.empty()) {
        buildArgs.push_back("-f");
        buildArgs.push_back(dockerfile.string());
    }
    for (const auto& arg : options.buildArgs) {
        buildArgs.push_back("--build-arg");
        buildArgs.push_back(arg);
    }

    fs::path contextDir = buildDir;
    if (!options.context.empty()) {
        contextDir = buildDir / fs::path(options.context);
    }
    if (!fs::exists(contextDir)) {
        std::cerr << "Error: Build context does not exist: " << contextDir.string() << "\n";
        return 1;
    }
    buildArgs.push_back(contextDir.string());

    bool dockerReady = (RunCommand("docker", {"version", "--format", "{{.Server.Version}}"}) == 0);
    bool useDocker = dockerReady;

    if (dockerReady) {
        std::cout << "Building Docker image: " << imageName << "...\n";
        res = RunCommand("docker", buildArgs);
        if (res != 0) {
            if (runtime == RuntimeType::Unknown) {
                std::cerr << "Error: Failed to build Docker image.\n";
                return 1;
            }
            std::cout << "Docker build failed. Falling back to process deployment.\n";
            useDocker = false;
        }
    } else {
        if (runtime == RuntimeType::Unknown) {
            std::cerr << "Error: Docker is unavailable and no process runtime was detected.\n";
            return 1;
        }
        std::cout << "Docker is unavailable. Falling back to process deployment.\n";
        useDocker = false;
    }

    if (!useDocker && runtime == RuntimeType::Node) {
        std::cout << "Preparing Node.js dependencies for process mode...\n";
        if (fs::exists(buildDir / "package-lock.json")) {
            res = RunCommand("npm", {"--prefix", buildDir.string(), "ci"});
        } else {
            res = RunCommand("npm", {"--prefix", buildDir.string(), "install"});
        }
        if (res != 0) {
            std::cerr << "Error: Failed to install Node.js dependencies for process mode.\n";
            return 1;
        }

        std::cout << "Running Node.js build for process mode (if script exists)...\n";
        res = RunCommand("npm", {"--prefix", buildDir.string(), "run", "build", "--if-present"});
        if (res != 0) {
            std::cerr << "Error: Node.js build failed for process mode.\n";
            return 1;
        }
    }

    std::string domain = options.domain.empty() ? (name + ".rfas.software") : options.domain;
    std::string container = options.container.empty() ? (name + "-container") : options.container;

    std::string metadataBranch = options.branch.empty() ? "" : options.branch;
    std::string metadataTag = options.tag.empty() ? "" : options.tag;
    std::string metadataCommit = options.commit.empty() ? resolvedCommit : options.commit;

    std::string vigContent;
    if (useDocker) {
        vigContent =
            "name = " + name + "\n"
            "domain = " + domain + "\n"
            "port = " + std::to_string(options.port) + "\n"
            "type = docker\n"
            "image = " + imageName + "\n"
            "container = " + container + "\n"
            "sourcerepo = " + options.repoUrl + "\n"
            "sourcebranch = " + metadataBranch + "\n"
            "sourcetag = " + metadataTag + "\n"
            "sourcecommit = " + metadataCommit + "\n"
            "buildcontext = " + (options.context.empty() ? "." : options.context) + "\n"
            "dockerfile = " + (options.dockerfile.empty() ? "Dockerfile" : options.dockerfile) + "\n";
    } else {
        std::string command = BuildProcessCommand(runtime, buildDir, options.port, options.envVars);
        vigContent =
            "name = " + name + "\n"
            "domain = " + domain + "\n"
            "port = " + std::to_string(options.port) + "\n"
            "type = process\n"
            "command = " + command + "\n"
            "pidfile = " + BuildPidFilePath(name) + "\n"
            "sourcerepo = " + options.repoUrl + "\n"
            "sourcebranch = " + metadataBranch + "\n"
            "sourcetag = " + metadataTag + "\n"
            "sourcecommit = " + metadataCommit + "\n";
    }

    for (const auto& env : options.envVars) {
        vigContent += "env = " + env + "\n";
    }

    fs::path targetVig = effectiveConfigDir / (name + ".vig");
    fs::path tempVig = targetVig;
    tempVig += ".tmp";

    std::ofstream ofs(tempVig);
    if (!ofs.is_open()) {
        std::cerr << "Error: Failed to write configuration file at " << tempVig.string() << "\n";
        return 1;
    }
    ofs << vigContent;
    ofs.close();

    std::error_code ec;
    fs::remove(targetVig, ec);
    fs::rename(tempVig, targetVig, ec);
    if (ec) {
        std::cerr << "Error: Failed to atomically update configuration file: " << ec.message() << "\n";
        std::error_code cleanupEc;
        fs::remove(tempVig, cleanupEc);
        return 1;
    }

    std::cout << "\nSuccessfully deployed " << name << "!\n";
    std::cout << "Config file: " << targetVig.string() << "\n";
    if (useDocker) {
        std::cout << "Routing domain: " << domain << " -> container " << container << "\n";
    } else {
        std::cout << "Routing domain: " << domain << " -> process in " << buildDir.string() << "\n";
    }
    if (!resolvedCommit.empty()) {
        std::cout << "Resolved commit: " << resolvedCommit << "\n";
    }
    std::cout << "Vigilant will automatically spin it up on the next request.\n";

    return 0;
}

int List(const std::string& configDir)
{
    if (!fs::exists(configDir)) {
        std::cerr << "Error: Config directory not found: " << configDir << std::endl;
        return 1;
    }

    std::cout << "Deployed Services:\n------------------\n";
    int count = 0;
    for (const auto& entry : fs::directory_iterator(configDir)) {
        if (entry.path().extension() == ".vig") {
            try {
                auto svc = ParseVigFile(entry.path().string());
                std::cout << svc.name << "\t" << svc.domain << "\t-> localhost:" << svc.port << "\n";
                count++;
            } catch (...) {
                std::cout << entry.path().filename().string() << "\t(Invalid format)\n";
            }
        }
    }
    
    if (count == 0) {
        std::cout << "No services deployed.\n";
    }
    
    return 0;
}

int Remove(const std::string& serviceName, const std::string& configDir)
{
    fs::path target = fs::path(configDir) / (serviceName + ".vig");
    
    if (!fs::exists(target)) {
        std::cerr << "Error: Service configuration not found: " << target.string() << std::endl;
        return 1;
    }

    std::error_code ec;
    if (fs::remove(target, ec)) {
        std::cout << "Successfully removed " << serviceName << ".\n";
        std::cout << "The daemon will spin it down automatically." << std::endl;
        return 0;
    } else {
        std::cerr << "Failed to remove service: " << ec.message() << std::endl;
        return 1;
    }
}

int Logs(const std::string& name, const std::string& configDir)
{
    fs::path targetVig = fs::path(configDir) / (name + ".vig");
    if (!fs::exists(targetVig)) {
        std::cerr << "Error: Service '" << name << "' is not deployed.\n";
        return 1;
    }

    std::vector<VigService> svcs;
    try {
        svcs = LoadAllServices(configDir);
    } catch (const std::exception& e) {
        std::cerr << "Error: Failed to read service configs: " << e.what() << "\n";
        return 1;
    }
    VigService targetSvc;
    bool found = false;
    for (const auto& svc : svcs) {
        if (svc.name == name) {
            targetSvc = svc;
            found = true;
            break;
        }
    }

    if (!found) {
        std::cerr << "Error: Service '" << name << "' found but could not be parsed.\n";
        return 1;
    }

    if (targetSvc.type == ServiceType::Docker) {
        RunCommand("docker", {"logs", "-n", "100", targetSvc.container});
    } else {
        std::error_code ec;
        std::string logPath = (fs::temp_directory_path(ec) / "vigilant" / "logs" / (name + ".log")).string();
        if (!fs::exists(logPath)) {
            std::cout << "No logs found for process '" << name << "'. It may not have printed anything yet.\n";
            return 0;
        }
#ifdef _WIN32
        RunCommand("powershell", {"-Command", "Get-Content -Path \"" + logPath + "\" -Tail 100"});
#else
        RunCommand("tail", {"-n", "100", logPath});
#endif
    }

    return 0;
}

static int ManageDaemon(const std::string& action)
{
#ifdef _WIN32
    std::cerr << "Error: 'vigilant " << action << "' is only supported on Unix-like systems with systemd.\n";
    return 1;
#else
    const int rc = RunCommand("systemctl", {action, "vigilant"});
    if (rc != 0) {
        std::cerr << "Error: Failed to " << action << " vigilant service. "
                  << "If this is a system service, retry with sudo.\n";
        return 1;
    }

    std::cout << "Successfully requested service " << action << ": vigilant\n";
    return 0;
#endif
}

int StartDaemon()
{
    return ManageDaemon("start");
}

int RestartDaemon()
{
    return ManageDaemon("restart");
}

int IssueCertificate(const CertOptions& options)
{
    if (options.domains.empty()) {
        std::cerr << "Error: At least one domain is required (e.g. vigilant cert api.example.com).\n";
        return 1;
    }

    for (const auto& domain : options.domains) {
        if (!IsValidCertDomain(domain)) {
            std::cerr << "Error: Invalid domain '" << domain << "'.\n";
            return 1;
        }
    }

    if (!options.email.empty() && !IsSafeCertEmail(options.email)) {
        std::cerr << "Error: Invalid --email value.\n";
        return 1;
    }

    if (!options.email.empty() && options.unsafeRegisterWithoutEmail) {
        std::cerr << "Error: Use either --email or --unsafe-register, not both.\n";
        return 1;
    }

    if (!IsSafeWebrootPath(options.webroot)) {
        std::cerr << "Error: Invalid --webroot path.\n";
        return 1;
    }

#ifdef _WIN32
    std::cerr << "Error: 'vigilant cert' requires Certbot on Linux or macOS.\n";
    return 1;
#else
    const fs::path webroot(options.webroot);
    const fs::path challengeDir = webroot / ".well-known" / "acme-challenge";
    std::error_code ec;
    fs::create_directories(challengeDir, ec);
    if (ec) {
        std::cerr << "Error: Cannot create ACME webroot " << challengeDir.string()
                  << ": " << ec.message() << "\n";
        std::cerr << "Hint: run with sudo or pass --webroot pointing at a writable directory.\n";
        return 1;
    }

    std::vector<std::string> args = {"certonly", "--webroot", "-w", options.webroot};
    for (const auto& domain : options.domains) {
        args.push_back("-d");
        args.push_back(domain);
    }

    if (options.staging) {
        args.push_back("--staging");
    }
    if (options.dryRun) {
        args.push_back("--dry-run");
    }
    if (options.forceRenewal) {
        args.push_back("--force-renewal");
    }

    if (!options.email.empty()) {
        args.push_back("--non-interactive");
        args.push_back("--agree-tos");
        args.push_back("-m");
        args.push_back(options.email);
    } else if (options.unsafeRegisterWithoutEmail) {
        args.push_back("--non-interactive");
        args.push_back("--agree-tos");
        args.push_back("--register-unsafely-without-email");
    }

    std::cout << "Issuing certificate via Certbot (HTTP-01 webroot).\n";
    std::cout << "Webroot: " << options.webroot << " (tokens must be served under /.well-known/acme-challenge/)\n";
    std::cout << "Domains: ";
    for (size_t i = 0; i < options.domains.size(); ++i) {
        if (i > 0) {
            std::cout << ", ";
        }
        std::cout << options.domains[i];
    }
    std::cout << "\n\n";

    const int rc = RunCommand("certbot", args);
    if (rc != 0) {
        return rc;
    }

    const std::string& primary = options.domains.front();
    std::cout << "\nTypical paths for .vig TLS keys:\n"
              << "  cert = /etc/letsencrypt/live/" << primary << "/fullchain.pem\n"
              << "  key = /etc/letsencrypt/live/" << primary << "/privkey.pem\n";

    return 0;
#endif
}

}
