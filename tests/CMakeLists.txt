include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.15.2
)
FetchContent_MakeAvailable(googletest)


add_executable(cache_tests
    single_thread_tests.cpp
    concurrency_tests.cpp
    txt_database_tests.cpp
    cache_simulator_tests.cpp
)

target_link_libraries(cache_tests PRIVATE cache GTest::gtest_main)
target_compile_options(cache_tests PRIVATE -Wall -Wextra)

target_compile_options(cache_tests PRIVATE)
target_link_options(cache_tests PRIVATE)


include(GoogleTest)
gtest_discover_tests(cache_tests)
