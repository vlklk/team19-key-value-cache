#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <vector>

#include "cache/policies/policy_base.h"

enum class WorkloadProfile {
    Uniform,
    Zipf,
    SequentialLoop,
    ReadHeavy,
    WriteHeavy,
    Burst,
};

struct BenchmarkConfig {
    std::vector<PolicyType> policies;
    std::vector<WorkloadProfile> profiles;
    std::vector<size_t> cache_sizes;
    std::vector<size_t> key_space_sizes;
    std::vector<size_t> value_sizes;
    std::vector<size_t> request_counts;
    std::vector<std::chrono::milliseconds> durations;
    std::vector<size_t> get_ratios;
    std::vector<std::chrono::milliseconds> ttls;
    std::vector<size_t> threads;
    size_t miss_penalty_ns = 0;
    size_t runs_per_config = 1;
    uint64_t seed = 42;
    bool get_ratios_overridden = false;
    bool help_requested = false;
};

struct BenchmarkScenario {
    PolicyType policy = PolicyType::Lru;
    WorkloadProfile profile = WorkloadProfile::Uniform;
    size_t cache_size = 0;
    size_t key_space_size = 0;
    size_t value_size = 0;
    size_t request_count = 0;
    std::chrono::milliseconds duration{0};
    size_t get_ratio = 0;
    std::chrono::milliseconds ttl{0};
    size_t threads = 0;
    size_t miss_penalty_ns = 0;
    uint64_t seed = 0;
};

std::expected<BenchmarkConfig, std::string> ParseBenchmarkConfig(int* argc, char** argv);
std::vector<BenchmarkScenario> MakeBenchmarkScenarios(const BenchmarkConfig& config);
std::string BuildBenchmarkName(const BenchmarkScenario& scenario);
std::string ToString(PolicyType policy);
std::string ToString(WorkloadProfile profile);
void PrintBenchmarkConfigHelp();
