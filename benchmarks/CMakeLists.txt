include(FetchContent)

set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    googlebenchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG v1.8.5
)
FetchContent_MakeAvailable(googlebenchmark)

add_executable(cache_benchmarks cache_benchmark.cpp benchmark_config.cpp)

target_link_libraries(cache_benchmarks PRIVATE cache benchmark::benchmark)
