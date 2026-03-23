#pragma once

#include <string>
#include <vector>
#include <deque>
#include <mutex>
#include <atomic>
#include <chrono>

struct RequestStat
{
    std::string timestamp;
    std::string method;
    std::string domain;
    std::string path;
    int status;
    int latencyMs;
};

struct LogEntry
{
    std::string timestamp;
    std::string level;
    std::string message;
};

class StatsManager
{
public:
    static StatsManager& Instance()
    {
        static StatsManager instance;
        return instance;
    }

    void RecordRequest(const std::string& method, const std::string& domain, const std::string& path, int status, int latencyMs);
    void RecordBlock();
    void RecordBytes(uint64_t bytes);
    void RecordLog(const std::string& level, const std::string& msg);

    std::string GetStatsJSON();

private:
    StatsManager() = default;

    std::atomic<uint64_t> _totalRequests{0};
    std::atomic<uint64_t> _blockedRequests{0};
    std::atomic<uint64_t> _bytesTransferred{0};

    std::deque<RequestStat> _recentRequests;
    std::deque<LogEntry> _recentLogs;
    std::mutex _mutex;

    std::string GetTimestamp();
    std::string EscapeJSON(const std::string& input);
};
