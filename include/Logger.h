#pragma once

#include <string>

namespace Logger
{
    void Init(const std::string& filepath);
    bool IsFileLoggingEnabled();
    std::string GetActiveLogPath();
    void Info(const std::string& msg);
    void Warn(const std::string& msg);
    void Error(const std::string& msg);
}
