//test file to test ssl cert/key loading and validation in VigConfig
#include "../include/catch.hpp"
#include "../include/VigConfig.h"
#include "test_helpers.h"

#include <filesystem>
#include <stdexcept>

TEST_CASE("VigConfig SSL cert/key loading and validation", "[VigConfig]")
{
    using namespace test_helpers;
    auto tempDir = MakeTempDir("ssl_test");

    // Create valid cert and key files
    auto certPath = tempDir / "test_cert.pem";
    auto keyPath = tempDir / "test_key.pem";
    WriteFile(certPath, "-----BEGIN CERTIFICATE-----\nMIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQ...\n-----END CERTIFICATE-----");
    WriteFile(keyPath, "-----BEGIN PRIVATE KEY-----\nMIIEvQIBADANBgkqhkiG9w0BAQEFAASCBKcwggSjAgEAAoIBAQD...\n-----END PRIVATE KEY-----");

    // Create a .vig file referencing the cert and key
    auto vigPath = tempDir / "service.vig";
    WriteFile(vigPath, "name=TestService\ntype=process\ncommand=/bin/test\ndomain=test.local\nport=443\ncert=" + certPath.string() + "\nkey=" + keyPath.string());

    // Load the config
    VigService config = ParseVigFile(vigPath.string());
    REQUIRE_NOTHROW(config);

    // Validate the loaded service
    REQUIRE(config.name == "TestService");
    REQUIRE(config.domain == "test.local");
    REQUIRE(config.port == 443);
    REQUIRE(config.cert == certPath.string());
    REQUIRE(config.key == keyPath.string());

    // Cleanup
    CleanupDir(tempDir);
}