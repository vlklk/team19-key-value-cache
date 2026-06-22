cmake_minimum_required(VERSION 3.16)
project(team19_key_value_cache LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(cache INTERFACE)
target_include_directories(cache INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

enable_testing()
add_subdirectory(tests)

option(CACHE_BUILD_BENCHMARKS "Build cache benchmarks" ON)
if(CACHE_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()
