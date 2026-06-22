#include <benchmark/benchmark.h>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include "benchmark_config.h"
#include "cache/cache.h"

namespace {

std::optional<std::chrono::seconds> ToCacheTtl(std::chrono::milliseconds ttl) {
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(ttl);
    if (seconds.count() == 0) { 
        seconds = std::chrono::seconds{1}; 
    }

    return seconds;
}

void BMCachePutGet(benchmark::State& state, BenchmarkScenario scenario) {
    CacheStats stats;

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

        auto put_result = cache->Put("key", value, ttl);

        if (!put_result.has_value()) {
            state.ResumeTiming();
            state.SkipWithError(put_result.error().c_str());
            return;
        }

        state.ResumeTiming();

        for (size_t i = 0; i < scenario.request_count; ++i) {
            if (i % 100 < scenario.get_ratio) {
                auto result = cache->Get("key");
                benchmark::DoNotOptimize(result);
            } else {
                auto result = cache->Put("key", value, ttl);
                benchmark::DoNotOptimize(result);

                if (!result.has_value()) {
                    state.SkipWithError(result.error().c_str());
                    return;
                }
            }
        }

        state.PauseTiming();
        stats = cache->Stats();
        state.ResumeTiming();
    }

    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) *
                            static_cast<int64_t>(scenario.request_count));
    state.counters["hit_ratio"] = stats.HitRatio();
    state.counters["evictions"] = static_cast<double>(stats.evictions);
    state.counters["memory_estimate"] = static_cast<double>(stats.used_memory);
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
        benchmark::RegisterBenchmark(BuildBenchmarkName(scenario).c_str(), BMCachePutGet, scenario)
            ->Iterations(1)
            ->Repetitions(static_cast<int>(config.runs_per_config))
            ->UseRealTime();
    }

    benchmark::RunSpecifiedBenchmarks();
    benchmark::Shutdown();

    return 0;
}
