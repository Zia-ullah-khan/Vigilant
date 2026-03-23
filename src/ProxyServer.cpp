#include "../include/ProxyServer.h"

#include <iostream>
#include <sstream>
#include <chrono>

ProxyServer::ProxyServer(int listenPort, ServiceManager& manager)
    : _listenPort(listenPort)
    , _manager(manager)
{
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
        std::cout << "[PROXY] " << req.method << " " 
                  << (domain.empty() ? "*" : domain) << req.path 
                  << " -> " << res.status << " (" << elapsed << "ms)" << std::endl;
    }
};

void ProxyServer::HandleRequest(const httplib::Request& req, httplib::Response& res)
{
    RequestLogger logger(req, res);
    
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

    if (!state->awake)
    {
        std::cout << "Request for sleeping service: " << state->config.name << std::endl;

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

    httplib::Result result = httplib::Result(nullptr, httplib::Error::Unknown);

    if (req.method == "GET")
    {
        result = client.Get(req.path, forwardHeaders);
    }
    else if (req.method == "POST")
    {
        result = client.Post(req.path, forwardHeaders, req.body, req.get_header_value("Content-Type"));
    }
    else if (req.method == "PUT")
    {
        result = client.Put(req.path, forwardHeaders, req.body, req.get_header_value("Content-Type"));
    }
    else if (req.method == "DELETE")
    {
        result = client.Delete(req.path, forwardHeaders);
    }
    else if (req.method == "PATCH")
    {
        result = client.Patch(req.path, forwardHeaders, req.body, req.get_header_value("Content-Type"));
    }
    else if (req.method == "OPTIONS")
    {
        result = client.Options(req.path, forwardHeaders);
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

void ProxyServer::Start()
{
    _server.Get(".*", [this](const httplib::Request& req, httplib::Response& res)
    {
        HandleRequest(req, res);
    });

    _server.Post(".*", [this](const httplib::Request& req, httplib::Response& res)
    {
        HandleRequest(req, res);
    });

    _server.Put(".*", [this](const httplib::Request& req, httplib::Response& res)
    {
        HandleRequest(req, res);
    });

    _server.Delete(".*", [this](const httplib::Request& req, httplib::Response& res)
    {
        HandleRequest(req, res);
    });

    _server.Patch(".*", [this](const httplib::Request& req, httplib::Response& res)
    {
        HandleRequest(req, res);
    });

    _server.Options(".*", [this](const httplib::Request& req, httplib::Response& res)
    {
        HandleRequest(req, res);
    });

    std::cout << "Vigilant listening on 0.0.0.0:" << _listenPort << std::endl;
    _server.listen("0.0.0.0", _listenPort);
}

void ProxyServer::Stop()
{
    _server.stop();
}
