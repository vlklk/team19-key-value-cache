#include <benchmark/benchmark.h>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include "benchmark_config.h"
#include "cache/cache.h"
#include "workload.h"

namespace {

std::optional<std::chrono::seconds> ToCacheTtl(std::chrono::milliseconds ttl) {
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(ttl);
    if (seconds.count() == 0) {
        seconds = std::chrono::seconds{1};
    }
    return seconds;
}

void WarmupCache(Cache& cache, const BenchmarkScenario& scenario, 
    const std::string& value, std::optional<std::chrono::seconds> ttl) {

    size_t warmup_count = std::min(scenario.cache_size, scenario.key_space_size);
    for (size_t i = 0; i < warmup_count; ++i) {
        auto result = cache.Put("key:" + std::to_string(i), value, ttl);
        benchmark::DoNotOptimize(result);
    }
}

void BMCacheWorkload(benchmark::State& state, BenchmarkScenario scenario) {
    CacheStats stats;
    size_t put_errors = 0;

    for (auto _ : state) {
        state.PauseTiming();

        CacheConfig cache_config;
        cache_config.max_count = scenario.cache_size;
        cache_config.policy = scenario.policy;

        auto cache_result = MakeCache(cache_config);
        if (!cache_result.has_value()) {
            state.ResumeTiming();
            state.SkipWithError(cache_result.error().c_str());
            return;
        }

        auto cache = std::move(cache_result.value());
        std::string value(scenario.value_size, 'x');
        auto ttl = ToCacheTtl(scenario.ttl);
        std::vector<Operation> operations = GenerateWorkload(scenario);

        WarmupCache(*cache, scenario, value, ttl);

        state.ResumeTiming();

        size_t current_put_errors = 0;
        for (const Operation& operation : operations) {
            if (operation.type == OperationType::Get) {
                auto result = cache->Get(operation.key);
                benchmark::DoNotOptimize(result);
            } else {
                auto result = cache->Put(operation.key, value, ttl);
                benchmark::DoNotOptimize(result);

                if (!result.has_value()) {
                    ++current_put_errors;
                }
            }
        }

        state.PauseTiming();
        stats = cache->Stats();
        put_errors = current_put_errors;
        state.ResumeTiming();
    }

    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) *
                            static_cast<int64_t>(scenario.request_count));
    state.counters["hit_ratio"] = stats.HitRatio();
    state.counters["evictions"] = static_cast<double>(stats.evictions);
    state.counters["memory_estimate"] = static_cast<double>(stats.used_memory);
    state.counters["put_errors"] = static_cast<double>(put_errors);
}

}  // namespace

int main(int argc, char** argv) {
    auto config_result = ParseBenchmarkConfig(&argc, argv);
    if (!config_result.has_value()) {
        std::cerr << config_result.error() << '\n';
        PrintBenchmarkConfigHelp();
        return 1;
    }

    BenchmarkConfig config = std::move(config_result.value());
    if (config.help_requested) {
        PrintBenchmarkConfigHelp();
        return 0;
    }

    benchmark::Initialize(&argc, argv);
    if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
        return 1;
    }

    for (const BenchmarkScenario& scenario : MakeBenchmarkScenarios(config)) {
        benchmark::RegisterBenchmark(BuildBenchmarkName(scenario).c_str(), 
            BMCacheWorkload, scenario)
            ->Iterations(1)
            ->Repetitions(static_cast<int>(config.runs_per_config))
            ->UseRealTime();
    }

    benchmark::RunSpecifiedBenchmarks();
    benchmark::Shutdown();

    return 0;
}
