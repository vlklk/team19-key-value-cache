#include <benchmark/benchmark.h>
#include <cstddef>
#include <cstdlib>
#include <string>
#include <utility>
#include "cache/cache.h"

namespace {

void BMCachePutGet(benchmark::State& state) {
    CacheConfig config;
    config.max_count = static_cast<size_t>(state.range(0));
    config.policy = PolicyType::Lru;

    auto cache_result = MakeCache(config);

    if (!cache_result.has_value()) {
        state.SkipWithError(cache_result.error().c_str());
        return;
    }

    auto cache = std::move(cache_result.value());

    for (auto _ : state) {
        auto put_result = cache->Put("key", "value");

        if (!put_result.has_value()) {
            state.SkipWithError(put_result.error().c_str());
            return;
        }

        auto value = cache->Get("key");
        benchmark::DoNotOptimize(value);
    }

    CacheStats stats = cache->Stats();
    state.counters["hit_ratio"] = stats.HitRatio();
    state.counters["evictions"] = static_cast<double>(stats.evictions);
    state.counters["memory_estimate"] = static_cast<double>(stats.used_memory);
}

}

BENCHMARK(BMCachePutGet)->Arg(1024);

int main(int argc, char** argv) {
    benchmark::Initialize(&argc, argv);

    if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
        return 1;
    }

    benchmark::RunSpecifiedBenchmarks();
    benchmark::Shutdown();

    return 0;
}
