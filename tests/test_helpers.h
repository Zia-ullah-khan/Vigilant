#pragma once

#include <filesystem>
#include <fstream>
#include <string>

namespace test_helpers {

inline std::filesystem::path MakeTempDir(const std::string& suffix)
{
    static int counter = 0;
    const auto base = std::filesystem::temp_directory_path() / "vigilant_tests";
    const auto dir = base / (suffix + "_" + std::to_string(++counter));
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

inline void WriteFile(const std::filesystem::path& path, const std::string& content)
{
    std::ofstream out(path, std::ios::trunc);
    out << content;
}

inline void CleanupDir(const std::filesystem::path& path)
{
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
}

} // namespace test_helpers
