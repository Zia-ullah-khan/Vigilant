#include "../include/catch.hpp"
#include "../include/VigConfig.h"
#include "test_helpers.h"

#include <filesystem>
#include <stdexcept>

TEST_CASE("ParseVigFile parses process service with defaults", "[config]")
{
    const auto dir = test_helpers::MakeTempDir("config_process");
    const auto file = dir / "svc.vig";
    test_helpers::WriteFile(file,
        "name = api\n"
        "domain = api.example.com\n"
        "port = 3001\n"
        "type = process\n"
        "command = node server.js\n");

    const auto svc = ParseVigFile(file.string());

    REQUIRE(svc.name == "api");
    REQUIRE(svc.domain == "api.example.com");
    REQUIRE(svc.port == 3001);
    REQUIRE(svc.type == ServiceType::Process);
    REQUIRE(svc.command == "node server.js");
    REQUIRE(svc.healthPath == "/");
    REQUIRE(svc.timeout == 30);
    REQUIRE(svc.rateLimit == 0);

    test_helpers::CleanupDir(dir);
}

TEST_CASE("ParseVigFile parses docker service", "[config]")
{
    const auto dir = test_helpers::MakeTempDir("config_docker");
    const auto file = dir / "svc.vig";
    test_helpers::WriteFile(file,
        "name = web\n"
        "domain = web.example.com\n"
        "port = 8080\n"
        "type = docker\n"
        "image = web-image\n"
        "container = web-container\n"
        "health = /health\n"
        "timeout = 45\n"
        "ratelimit = 120\n");

    const auto svc = ParseVigFile(file.string());

    REQUIRE(svc.type == ServiceType::Docker);
    REQUIRE(svc.image == "web-image");
    REQUIRE(svc.container == "web-container");
    REQUIRE(svc.healthPath == "/health");
    REQUIRE(svc.timeout == 45);
    REQUIRE(svc.rateLimit == 120);

    test_helpers::CleanupDir(dir);
}

TEST_CASE("ParseVigFile throws for invalid or missing fields", "[config]")
{
    const auto dir = test_helpers::MakeTempDir("config_invalid");

    SECTION("missing required key")
    {
        const auto file = dir / "missing.vig";
        test_helpers::WriteFile(file,
            "name = broken\n"
            "domain = broken.example.com\n"
            "type = process\n"
            "command = ./run.sh\n");

        REQUIRE_THROWS_AS(ParseVigFile(file.string()), std::runtime_error);
    }

    SECTION("unknown service type")
    {
        const auto file = dir / "bad_type.vig";
        test_helpers::WriteFile(file,
            "name = broken\n"
            "domain = broken.example.com\n"
            "port = 3000\n"
            "type = vm\n");

        REQUIRE_THROWS_AS(ParseVigFile(file.string()), std::runtime_error);
    }

    test_helpers::CleanupDir(dir);
}

TEST_CASE("LoadAllServices loads only vig files", "[config]")
{
    const auto dir = test_helpers::MakeTempDir("config_loadall");

    test_helpers::WriteFile(dir / "one.vig",
        "name = one\n"
        "domain = one.example.com\n"
        "port = 3001\n"
        "type = process\n"
        "command = ./one\n");

    test_helpers::WriteFile(dir / "two.vig",
        "name = two\n"
        "domain = two.example.com\n"
        "port = 3002\n"
        "type = docker\n"
        "image = two-img\n"
        "container = two-ctr\n");

    test_helpers::WriteFile(dir / "ignore.txt", "not a service\n");

    const auto services = LoadAllServices(dir.string());

    REQUIRE(services.size() == 2);

    test_helpers::CleanupDir(dir);
}
