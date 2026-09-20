cmake_minimum_required(VERSION 3.20)

project(insider VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(CURL REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

add_executable(insider
    src/main.cpp
    src/activity_collector.cpp
    src/http_client.cpp
)

target_include_directories(insider PRIVATE include)

target_link_libraries(insider PRIVATE
    CURL::libcurl
    nlohmann_json::nlohmann_json
)

target_compile_definitions(insider PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _WIN32_WINNT=0x0601
)

if(MSVC)
    target_compile_options(insider PRIVATE /W4 /permissive-)
else()
    target_compile_options(insider PRIVATE -Wall -Wextra -Wpedantic)
endif()
