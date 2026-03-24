cmake_minimum_required(VERSION 3.10)
project(Vigilant)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(${CMAKE_SOURCE_DIR}/include)

add_executable(vigilant
    main.cpp
    src/VigConfig.cpp
    src/ServiceManager.cpp
    src/ProxyServer.cpp
    src/Logger.cpp
    src/StatsManager.cpp
    src/DashboardServer.cpp
    src/CLI.cpp
)

find_package(Threads REQUIRED)
find_package(OpenSSL)
target_link_libraries(vigilant PRIVATE Threads::Threads)

if(OPENSSL_FOUND)
    message(STATUS "OpenSSL found -> Enabling Native HTTPS features.")
    target_compile_definitions(vigilant PRIVATE CPPHTTPLIB_OPENSSL_SUPPORT)
    target_link_libraries(vigilant PRIVATE OpenSSL::SSL OpenSSL::Crypto)
else()
    message(WARNING "OpenSSL missing -> Native HTTPS capabilities will be disabled.")
endif()

add_executable(vigilant_tests
    tests/test_main.cpp
    tests/test_vigconfig.cpp
    tests/test_cli.cpp
    tests/test_traffic.cpp
    src/VigConfig.cpp
    src/CLI.cpp
    src/Logger.cpp
    src/StatsManager.cpp
)
target_include_directories(vigilant_tests PRIVATE include)
target_link_libraries(vigilant_tests PRIVATE Threads::Threads)

if(OPENSSL_FOUND)
    target_compile_definitions(vigilant_tests PRIVATE CPPHTTPLIB_OPENSSL_SUPPORT)
    target_link_libraries(vigilant_tests PRIVATE OpenSSL::SSL OpenSSL::Crypto)
endif()