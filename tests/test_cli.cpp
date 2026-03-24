#include "../include/catch.hpp"
#include "../include/CLI.h"
#include "test_helpers.h"

#include <filesystem>
#include <iostream>
#include <sstream>
#include <stdexcept>

#ifdef _WIN32
#include <cstdlib>
#else
#include <cstdlib>
#include <unistd.h>
#endif

namespace {

class ScopedStreamCapture {
public:
    explicit ScopedStreamCapture(std::ostream& stream)
        : _stream(stream), _oldBuf(stream.rdbuf(_capture.rdbuf()))
    {
    }

    ~ScopedStreamCapture()
    {
        _stream.rdbuf(_oldBuf);
    }

    std::string Str() const
    {
        return _capture.str();
    }

private:
    std::ostream& _stream;
    std::streambuf* _oldBuf;
    std::ostringstream _capture;
};

std::filesystem::path CreateProcessVig(const std::filesystem::path& dir, const std::string& name)
{
    const auto file = dir / (name + ".vig");
    test_helpers::WriteFile(file,
        "name = " + name + "\n"
        "domain = " + name + ".example.com\n"
        "port = 3000\n"
        "type = process\n"
        "command = echo hello\n");
    return file;
}

class ScopedEnvVar {
public:
    ScopedEnvVar(const std::string& key, const std::string& value) : _key(key)
    {
        const char* existing = std::getenv(_key.c_str());
        if (existing != nullptr) {
            _hadOld = true;
            _oldValue = existing;
        }
        Set(value);
    }

    ~ScopedEnvVar()
    {
        if (_hadOld) {
            Set(_oldValue);
        } else {
#ifdef _WIN32
            _putenv_s(_key.c_str(), "");
#else
            unsetenv(_key.c_str());
#endif
        }
    }

private:
    void Set(const std::string& value)
    {
#ifdef _WIN32
        _putenv_s(_key.c_str(), value.c_str());
#else
        setenv(_key.c_str(), value.c_str(), 1);
#endif
    }

    std::string _key;
    bool _hadOld = false;
    std::string _oldValue;
};

} // namespace

TEST_CASE("CLI Deploy copies .vig into config directory", "[cli]")
{
    const auto root = test_helpers::MakeTempDir("cli_deploy");
    const auto sourceDir = root / "src";
    const auto configDir = root / "cfg";
    std::filesystem::create_directories(sourceDir);

    const auto sourceVig = CreateProcessVig(sourceDir, "alpha");

    const int rc = CLI::Deploy(sourceVig.string(), configDir.string());
    REQUIRE(rc == 0);
    REQUIRE(std::filesystem::exists(configDir / "alpha.vig"));

    test_helpers::CleanupDir(root);
}

TEST_CASE("CLI Deploy falls back to user-local config directory when requested path is unusable", "[cli]")
{
    const auto root = test_helpers::MakeTempDir("cli_deploy_fallback");
    const auto sourceDir = root / "src";
    const auto fakeHome = root / "home";
    const auto unusablePath = root / "blocked_config";
    std::filesystem::create_directories(sourceDir);
    std::filesystem::create_directories(fakeHome);

    // A regular file at config path makes create_directories fail.
    test_helpers::WriteFile(unusablePath, "not-a-directory\n");

    const auto sourceVig = CreateProcessVig(sourceDir, "fallbacksvc");

#ifdef _WIN32
    ScopedEnvVar homeVar("USERPROFILE", fakeHome.string());
#else
    ScopedEnvVar homeVar("HOME", fakeHome.string());
#endif

    const int rc = CLI::Deploy(sourceVig.string(), unusablePath.string());
    REQUIRE(rc == 0);

    const auto expected = fakeHome / ".vigilant" / "services" / "fallbacksvc.vig";
    REQUIRE(std::filesystem::exists(expected));

    test_helpers::CleanupDir(root);
}

TEST_CASE("CLI List prints deployed services", "[cli]")
{
    const auto root = test_helpers::MakeTempDir("cli_list");
    const auto configDir = root / "cfg";
    std::filesystem::create_directories(configDir);

    CreateProcessVig(configDir, "alpha");
    CreateProcessVig(configDir, "beta");

    ScopedStreamCapture capture(std::cout);
    const int rc = CLI::List(configDir.string());
    REQUIRE(rc == 0);

    const auto out = capture.Str();
    REQUIRE(out.find("alpha") != std::string::npos);
    REQUIRE(out.find("beta") != std::string::npos);

    test_helpers::CleanupDir(root);
}

TEST_CASE("CLI Remove deletes deployed service config", "[cli]")
{
    const auto root = test_helpers::MakeTempDir("cli_remove");
    const auto configDir = root / "cfg";
    std::filesystem::create_directories(configDir);

    CreateProcessVig(configDir, "gone");
    REQUIRE(std::filesystem::exists(configDir / "gone.vig"));

    const int rc = CLI::Remove("gone", configDir.string());
    REQUIRE(rc == 0);
    REQUIRE_FALSE(std::filesystem::exists(configDir / "gone.vig"));

    test_helpers::CleanupDir(root);
}

TEST_CASE("CLI DeployGit validates unsafe or conflicting input", "[cli]")
{
    const auto root = test_helpers::MakeTempDir("cli_deploy_git");
    const auto configDir = root / "cfg";
    std::filesystem::create_directories(configDir);

    SECTION("unsafe URL is rejected")
    {
        CLI::DeployOptions opts;
        opts.repoUrl = "https://github.com/example/repo.git; rm -rf /";
        const int rc = CLI::DeployGit(opts, configDir.string());
        REQUIRE(rc != 0);
    }

    SECTION("branch and tag together are rejected")
    {
        CLI::DeployOptions opts;
        opts.repoUrl = "https://github.com/example/repo.git";
        opts.branch = "main";
        opts.tag = "v1.0.0";
        const int rc = CLI::DeployGit(opts, configDir.string());
        REQUIRE(rc != 0);
    }

    SECTION("invalid port is rejected")
    {
        CLI::DeployOptions opts;
        opts.repoUrl = "https://github.com/example/repo.git";
        opts.port = 70000;
        const int rc = CLI::DeployGit(opts, configDir.string());
        REQUIRE(rc != 0);
    }

    test_helpers::CleanupDir(root);
}
