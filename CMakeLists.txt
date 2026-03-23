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
)

target_link_libraries(vigilant PRIVATE pthread)

add_executable(vigilant_tests
    tests/test_traffic.cpp
)
target_link_libraries(vigilant_tests PRIVATE pthread)