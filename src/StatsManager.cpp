#include "../include/StatsManager.h"
#include <sstream>
#include <ctime>

std::string StatsManager::GetTimestamp()
{
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%H:%M:%S", std::localtime(&t));
    return std::string(buf);
}

std::string StatsManager::EscapeJSON(const std::string& input)
{
    std::string output;
    for (char c : input)
    {
        if (c == '"') output += "\\\"";
        else if (c == '\\') output += "\\\\";
        else if (c == '\b') output += "\\b";
        else if (c == '\f') output += "\\f";
        else if (c == '\n') output += "\\n";
        else if (c == '\r') output += "\\r";
        else if (c == '\t') output += "\\t";
        else output += c;
    }
    return output;
}

void StatsManager::RecordRequest(const std::string& method, const std::string& domain, const std::string& path, int status, int latencyMs)
{
    _totalRequests++;
    
    std::lock_guard<std::mutex> lock(_mutex);
    _recentRequests.push_back({GetTimestamp(), method, domain.empty() ? "*" : domain, path, status, latencyMs});
    if (_recentRequests.size() > 100)
    {
        _recentRequests.pop_front();
    }
}

void StatsManager::RecordBlock()
{
    _blockedRequests++;
}

void StatsManager::RecordBytes(uint64_t bytes)
{
    _bytesTransferred += bytes;
}

void StatsManager::RecordLog(const std::string& level, const std::string& msg)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _recentLogs.push_back({GetTimestamp(), level, msg});
    if (_recentLogs.size() > 150)
    {
        _recentLogs.pop_front();
    }
}

std::string StatsManager::GetStatsJSON()
{
    std::lock_guard<std::mutex> lock(_mutex);
    std::stringstream ss;
    ss << "{";
    ss << "\"totalRequests\":" << _totalRequests.load() << ",";
    ss << "\"blockedRequests\":" << _blockedRequests.load() << ",";
    ss << "\"bytesTransferred\":" << _bytesTransferred.load() << ",";
    
    ss << "\"recentRequests\":[";
    for (size_t i = 0; i < _recentRequests.size(); ++i)
    {
        const auto& r = _recentRequests[i];
        ss << "{\"time\":\"" << EscapeJSON(r.timestamp) 
           << "\",\"method\":\"" << EscapeJSON(r.method) 
           << "\",\"domain\":\"" << EscapeJSON(r.domain) 
           << "\",\"path\":\"" << EscapeJSON(r.path) 
           << "\",\"status\":" << r.status 
           << ",\"latencyMs\":" << r.latencyMs << "}";
        if (i < _recentRequests.size() - 1) ss << ",";
    }
    ss << "],";

    ss << "\"recentLogs\":[";
    for (size_t i = 0; i < _recentLogs.size(); ++i)
    {
        const auto& l = _recentLogs[i];
        ss << "{\"time\":\"" << EscapeJSON(l.timestamp) 
           << "\",\"level\":\"" << EscapeJSON(l.level) 
           << "\",\"message\":\"" << EscapeJSON(l.message) << "\"}";
        if (i < _recentLogs.size() - 1) ss << ",";
    }
    ss << "]}";
    return ss.str();
}
