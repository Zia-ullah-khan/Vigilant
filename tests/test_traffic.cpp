#define CATCH_CONFIG_MAIN
#include "../include/catch.hpp"
#include "../include/httplib.h"

#include <thread>
#include <vector>
#include <atomic>
#include <chrono>

const std::string VIGILANT_URL = "localhost";
const int VIGILANT_PORT = 9000;

void MakeRequest(const std::string& domain, int& successCount, int& failCount) {
    httplib::Client client(VIGILANT_URL, VIGILANT_PORT);
    client.set_connection_timeout(5);
    client.set_read_timeout(10);
    
    httplib::Headers headers = {
        {"Host", domain}
    };
    
    if (auto res = client.Get("/", headers)) {
        if (res->status == 200) {
            successCount++;
        } else {
            failCount++;
        }
    } else {
        failCount++;
    }
}

void TrafficBurst(const std::string& domain, int totalRequests, int numThreads) {
    std::vector<std::thread> workers;
    std::atomic<int> success(0);
    std::atomic<int> fails(0);
    
    int reqsPerThread = totalRequests / numThreads;
    
    for (int i = 0; i < numThreads; i++) {
        workers.emplace_back([&]() {
            int localSuccess = 0;
            int localFail = 0;
            for (int r = 0; r < reqsPerThread; r++) {
                MakeRequest(domain, localSuccess, localFail);
            }
            success += localSuccess;
            fails += localFail;
        });
    }
    
    for (auto& t : workers) {
        t.join();
    }
    
    REQUIRE(fails == 0);
    REQUIRE(success == totalRequests);
}

TEST_CASE("Vigilant Load Testing", "[load]") {
    
    // Note: Vigilant must be running for these integration tests to pass.
    
    SECTION("Low Traffic") {
        int success = 0, fails = 0;
        MakeRequest("24control.rfas.software", success, fails);
        REQUIRE(success == 1);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        MakeRequest("24control.rfas.software", success, fails);
        REQUIRE(success == 2);
    }
    
    SECTION("Normal Traffic - Concurrency") {
        TrafficBurst("24control.rfas.software", 20, 4);
    }
    
    SECTION("High Traffic - DDoS Simulation") {
        TrafficBurst("24control.rfas.software", 200, 20);
    }
}
