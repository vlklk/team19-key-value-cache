#include "workload.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <random>
#include <vector>

namespace {

std::string MakeKey(size_t index) {
    return "key:" + std::to_string(index);
}

std::vector<size_t> MakeKeyPermutation(size_t key_space_size, uint64_t seed) {
    std::vector<size_t> permutation(key_space_size);
    std::iota(permutation.begin(), permutation.end(), size_t{0});

    std::mt19937_64 permutation_generator(seed ^ 0x9E3779B97F4A7C15ULL);
    std::shuffle(permutation.begin(), permutation.end(), permutation_generator);

    return permutation;
}

size_t HotKeyCount(const BenchmarkScenario& scenario) {
    size_t by_key_space = std::max<size_t>(1, scenario.key_space_size / 100);
    size_t by_cache_size = std::max<size_t>(1, scenario.cache_size / 10);
    return std::min({scenario.key_space_size, by_key_space, by_cache_size});
}

std::vector<double> MakeZipfWeights(size_t key_space_size) {
    constexpr double alpha = 1.2;
    std::vector<double> weights;
    weights.reserve(key_space_size);

    for (size_t i = 0; i < key_space_size; ++i) {
        weights.push_back(1.0 / std::pow(static_cast<double>(i + 1), alpha));
    }

    return weights;
}

OperationType MakeOperationType(size_t index, size_t get_ratio) {
    if (index % 100 < get_ratio) {
        return OperationType::Get;
    }

    return OperationType::Put;
}

size_t UniformKey(std::mt19937_64& generator, size_t key_space_size) {
    std::uniform_int_distribution<size_t> distribution(0, key_space_size - 1);
    return distribution(generator);
}

size_t BurstKey(std::mt19937_64& generator, const BenchmarkScenario& scenario) {
    constexpr size_t hot_probability_percent = 80;

    std::uniform_int_distribution<size_t> hot_or_cold(0, 99);
    size_t hot_key_count = HotKeyCount(scenario);

    if (hot_or_cold(generator) < hot_probability_percent) {
        std::uniform_int_distribution<size_t> distribution(0, hot_key_count - 1);
        return distribution(generator);
    }

    return UniformKey(generator, scenario.key_space_size);
}

size_t KeyForOperation(std::mt19937_64& generator, 
    std::discrete_distribution<size_t>& zipf_distribution,
    const BenchmarkScenario& scenario, size_t index) {
    switch (scenario.profile) {
        case WorkloadProfile::Uniform:
        case WorkloadProfile::ReadHeavy:
        case WorkloadProfile::WriteHeavy:
            return UniformKey(generator, scenario.key_space_size);
        case WorkloadProfile::Zipf:
            return zipf_distribution(generator);
        case WorkloadProfile::SequentialLoop:
            return index % scenario.key_space_size;
        case WorkloadProfile::Burst:
            return BurstKey(generator, scenario);
    }

    return 0;
}

}  // namespace

std::vector<Operation> GenerateWorkload(const BenchmarkScenario& scenario) {
    std::vector<Operation> operations;
    operations.reserve(scenario.request_count);

    std::mt19937_64 generator(scenario.seed);
    std::vector<double> zipf_weights = MakeZipfWeights(scenario.key_space_size);
    std::discrete_distribution<size_t> zipf_distribution(zipf_weights.begin(), zipf_weights.end());
    std::vector<size_t> key_permutation =
        MakeKeyPermutation(scenario.key_space_size, scenario.seed);
    size_t get_ratio = scenario.get_ratio;

    for (size_t i = 0; i < scenario.request_count; ++i) {
        size_t key_rank = KeyForOperation(generator, zipf_distribution, scenario, i);
        operations.push_back(Operation{
            MakeOperationType(i, get_ratio),
            MakeKey(key_permutation[key_rank]),
        });
    }

    return operations;
}
