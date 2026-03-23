#include "../include/ProxyServer.h"
#include "../include/Logger.h"
#include "../include/StatsManager.h"

#include <iostream>
#include <sstream>
#include <chrono>

ProxyServer::ProxyServer(int listenPort, ServiceManager& manager, const std::string& certPath, const std::string& keyPath)
    : _listenPort(listenPort)
    , _manager(manager)
{
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
    if (!certPath.empty() && !keyPath.empty()) {
        Logger::Info("HTTPS Enabled: Binding Native SSL Responder");
        _server = std::make_unique<httplib::SSLServer>(certPath.c_str(), keyPath.c_str());
        if (!_server->is_valid()) {
            Logger::Error("Failed to initialize SSL Server! Please verify your certificate and key files exist and correspond.");
            exit(1);
        }
        return;
    }
#else
    if (!certPath.empty() || !keyPath.empty()) {
        Logger::Error("Vigilant was compiled without OpenSSL! Ignoring --cert and --key flags. Falling back to HTTP.");
    }
#endif
    _server = std::make_unique<httplib::Server>();
}

std::string ProxyServer::ExtractDomain(const httplib::Request& req)
{
    auto it = req.headers.find("X-Forwarded-Host");
    if (it != req.headers.end())
    {
        return it->second;
    }

    it = req.headers.find("Host");
    if (it != req.headers.end())
    {
        std::string host = it->second;
        auto colon = host.find(':');
        if (colon != std::string::npos)
        {
            return host.substr(0, colon);
        }
        return host;
    }

    return "";
}

bool ProxyServer::CheckRateLimit(const std::string& ip, int limit)
{
    if (limit <= 0) return true;

    std::lock_guard<std::mutex> lock(_rateMutex);
    auto& history = _rateLimits[ip];
    auto now = std::chrono::steady_clock::now();

    while (!history.empty() && std::chrono::duration_cast<std::chrono::seconds>(now - history.front()).count() > 60)
    {
        history.pop_front();
    }

    if (history.size() >= limit)
    {
        return false;
    }

    history.push_back(now);
    return true;
}

struct RequestLogger {
    const httplib::Request& req;
    httplib::Response& res;
    std::string domain;
    std::chrono::steady_clock::time_point start;

    RequestLogger(const httplib::Request& r, httplib::Response& s) 
        : req(r), res(s), start(std::chrono::steady_clock::now()) {}

    ~RequestLogger() {
        auto end = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
        Logger::Info("[PROXY] " + req.method + " " + (domain.empty() ? "*" : domain) + req.path + " -> " + std::to_string(res.status) + " (" + std::to_string(elapsed) + "ms)");
        
        StatsManager::Instance().RecordRequest(req.method, domain, req.path, res.status, elapsed);
        if (res.status == 429) StatsManager::Instance().RecordBlock();
        if (!res.body.empty()) StatsManager::Instance().RecordBytes(res.body.size());
    }
};

void ProxyServer::HandleRequest(const httplib::Request& req, httplib::Response& res)
{
    RequestLogger logger(req, res);
    
    std::string acmePrefix = "/.well-known/acme-challenge/";
    if (req.path.rfind(acmePrefix, 0) == 0)
    {
        std::string filename = req.path.substr(acmePrefix.length());
        std::string filepath = "/var/www/html/.well-known/acme-challenge/" + filename;
        
        std::ifstream acmeFile(filepath, std::ios::binary);
        if (acmeFile.is_open())
        {
            std::stringstream buffer;
            buffer << acmeFile.rdbuf();
            res.set_content(buffer.str(), "text/plain");
            res.status = 200;
            Logger::Info("[ACME] Let's Encrypt validation served: " + filename);
        }
        else
        {
            res.status = 404;
            Logger::Error("[ACME] Failed to find validation file: " + filepath);
        }
        return;
    }

    std::string domain = ExtractDomain(req);
    logger.domain = domain;

    if (domain.empty())
    {
        res.status = 400;
        res.set_content(R"({"error":"missing host header"})", "application/json");
        return;
    }

    ServiceState* state = _manager.FindByDomain(domain);

    if (!state)
    {
        res.status = 404;
        res.set_content(R"({"error":"unknown service","domain":")" + domain + R"("})", "application/json");
        return;
    }

    std::string ip = req.get_header_value("X-Forwarded-For");
    if (ip.empty()) ip = req.get_header_value("X-Real-IP");
    if (ip.empty()) ip = req.remote_addr;

    if (!CheckRateLimit(ip + ":" + domain, state->config.rateLimit))
    {
        Logger::Info("Rate limit exceeded for " + ip + " on " + domain);
        res.status = 429;
        res.set_content(R"({"error":"Too Many Requests"})", "application/json");
        return;
    }

    if (!state->awake)
    {
        Logger::Info("Request for sleeping service: " + state->config.name);

        if (!_manager.WakeService(state->config.name))
        {
            res.status = 503;
            res.set_content(
                R"({"error":"service failed to start","service":")" + state->config.name + R"("})",
                "application/json"
            );
            return;
        }
    }

    _manager.TouchService(state->config.name);

    httplib::Client client("localhost", state->config.port);
    client.set_connection_timeout(10);
    client.set_read_timeout(30);

    httplib::Headers forwardHeaders;
    for (const auto& [key, value] : req.headers)
    {
        std::string lower = key;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

        if (lower == "host" || lower == "connection" || lower == "transfer-encoding")
        {
            continue;
        }

        forwardHeaders.emplace(key, value);
    }
    forwardHeaders.emplace("X-Forwarded-For", req.remote_addr);
    forwardHeaders.emplace("X-Forwarded-Host", domain);

    // Preserve query strings by forwarding the full request target when available.
    const std::string& backendTarget = req.target.empty() ? req.path : req.target;

    httplib::Result result = httplib::Result(nullptr, httplib::Error::Unknown);

    if (req.method == "GET")
    {
        result = client.Get(backendTarget, forwardHeaders);
    }
    else if (req.method == "POST")
    {
        result = client.Post(backendTarget, forwardHeaders, req.body, req.get_header_value("Content-Type"));
    }
    else if (req.method == "PUT")
    {
        result = client.Put(backendTarget, forwardHeaders, req.body, req.get_header_value("Content-Type"));
    }
    else if (req.method == "DELETE")
    {
        result = client.Delete(backendTarget, forwardHeaders);
    }
    else if (req.method == "PATCH")
    {
        result = client.Patch(backendTarget, forwardHeaders, req.body, req.get_header_value("Content-Type"));
    }
    else if (req.method == "OPTIONS")
    {
        result = client.Options(backendTarget, forwardHeaders);
    }
    else
    {
        res.status = 405;
        res.set_content(R"({"error":"method not supported"})", "application/json");
        return;
    }

    if (!result)
    {
        res.status = 502;
        res.set_content(
            R"({"error":"backend unreachable","service":")" + state->config.name + R"("})",
            "application/json"
        );
        return;
    }

    res.status = result->status;
    for (const auto& [key, value] : result->headers)
    {
        std::string lower = key;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

        if (lower == "transfer-encoding" || lower == "connection")
        {
            continue;
        }

        res.set_header(key, value);
    }
    res.set_content(result->body, result->get_header_value("Content-Type"));
}

void ProxyServer::HandleWebSocket(const httplib::Request& req, httplib::ws::WebSocket& client_ws)
{
    std::string domain = ExtractDomain(req);
    if (domain.empty()) {
        client_ws.close(httplib::ws::CloseStatus::PolicyViolation, "missing host header");
        return;
    }

    ServiceState* state = _manager.FindByDomain(domain);
    if (!state) {
        client_ws.close(httplib::ws::CloseStatus::PolicyViolation, "unknown service");
        return;
    }

    std::string ip = req.get_header_value("X-Forwarded-For");
    if (ip.empty()) ip = req.get_header_value("X-Real-IP");
    if (ip.empty()) ip = req.remote_addr;

    if (!CheckRateLimit(ip + ":" + domain, state->config.rateLimit)) {
        client_ws.close(httplib::ws::CloseStatus::PolicyViolation, "rate limit exceeded");
        return;
    }

    if (!state->awake) {
        if (!_manager.WakeService(state->config.name)) {
            client_ws.close(httplib::ws::CloseStatus::InternalError, "service failed to start");
            return;
        }
    }
    _manager.TouchService(state->config.name);

    httplib::Headers forwardHeaders;
    for (const auto& [key, value] : req.headers) {
        std::string lower = key;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        if (lower == "host" || lower == "connection" || lower == "upgrade" ||
            lower == "sec-websocket-key" || lower == "sec-websocket-version" ||
            lower == "sec-websocket-extensions" || lower == "sec-websocket-accept") {
            continue;
        }
        forwardHeaders.emplace(key, value);
    }
    forwardHeaders.emplace("X-Forwarded-For", req.remote_addr);
    forwardHeaders.emplace("X-Forwarded-Host", domain);

    const std::string& backendTarget = req.target.empty() ? req.path : req.target;
    std::string backendUrl = "ws://localhost:" + std::to_string(state->config.port) + backendTarget;

    Logger::Info("[PROXY-WS] Upgrading " + domain + req.path + " -> " + backendUrl);

    auto wsClient = std::make_shared<httplib::ws::WebSocketClient>(backendUrl, forwardHeaders);
    wsClient->set_read_timeout(0, 0);
    
    if (!wsClient->connect()) {
        Logger::Error("[PROXY-WS] Backend WebSocket offline for " + domain);
        client_ws.close(httplib::ws::CloseStatus::InternalError, "backend unreachable");
        return;
    }

    std::atomic<bool> active{true};

    std::thread backendToFrontend([wsClient, &client_ws, &active]() {
        std::string msg;
        while (active) {
            auto res = wsClient->read(msg);
            if (res == httplib::ws::ReadResult::Text) {
                if (!client_ws.send(msg)) break;
            } else if (res == httplib::ws::ReadResult::Binary) {
                if (!client_ws.send(msg.data(), msg.size())) break;
            } else {
                break;
            }
            msg.clear();
        }
        active = false;
        client_ws.close();
    });

    std::string msg;
    while (active) {
        auto res = client_ws.read(msg);
        if (res == httplib::ws::ReadResult::Text) {
            if (!wsClient->send(msg)) break;
        } else if (res == httplib::ws::ReadResult::Binary) {
            if (!wsClient->send(msg.data(), msg.size())) break;
        } else {
            break;
        }
        msg.clear();
    }
    
    active = false;
    wsClient->close(httplib::ws::CloseStatus::Normal, "proxy closing");

    if (backendToFrontend.joinable()) {
        backendToFrontend.join();
    }
    
    Logger::Info("[PROXY-WS] Closed connection for " + domain + req.path);
}

void ProxyServer::Start()
{
    _server->Get(".*", [this](const httplib::Request& req, httplib::Response& res)
    {
        HandleRequest(req, res);
    });

    _server->WebSocket(".*", [this](const httplib::Request& req, httplib::ws::WebSocket& ws)
    {
        HandleWebSocket(req, ws);
    });

    _server->Post(".*", [this](const httplib::Request& req, httplib::Response& res)
    {
        HandleRequest(req, res);
    });

    _server->Put(".*", [this](const httplib::Request& req, httplib::Response& res)
    {
        HandleRequest(req, res);
    });

    _server->Delete(".*", [this](const httplib::Request& req, httplib::Response& res)
    {
        HandleRequest(req, res);
    });

    _server->Patch(".*", [this](const httplib::Request& req, httplib::Response& res)
    {
        HandleRequest(req, res);
    });

    _server->Options(".*", [this](const httplib::Request& req, httplib::Response& res)
    {
        HandleRequest(req, res);
    });

    Logger::Info("Vigilant proxy bound to 0.0.0.0:" + std::to_string(_listenPort));
    _server->listen("0.0.0.0", _listenPort);
}

void ProxyServer::Stop()
{
    if (_server) _server->stop();
}
