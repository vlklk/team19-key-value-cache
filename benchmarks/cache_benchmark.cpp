#include <benchmark/benchmark.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include "benchmark_config.h"
#include "cache/cache.h"
#include "workload.h"

namespace {

std::optional<std::chrono::seconds> ToCacheTtl(std::chrono::milliseconds ttl) {
    if (ttl == std::chrono::milliseconds::zero()) {
        return std::nullopt;
    }

    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(ttl);
    if (seconds.count() == 0) {
        seconds = std::chrono::seconds{1};
    }
    return seconds;
}

double PercentileNs(std::vector<double> values, double percentile) {
    if (values.empty()) {
        return 0.0;
    }

    std::sort(values.begin(), values.end());
    size_t index = static_cast<size_t>(
        std::ceil(percentile * static_cast<double>(values.size()))) - 1;
    index = std::min(index, values.size() - 1);

    return values[index];
}

struct PartitionedOperations {
    std::vector<const Operation*> gets;
    std::vector<const Operation*> puts;
};

PartitionedOperations PartitionOperations(const std::vector<Operation>& operations) {
    PartitionedOperations result;
    result.gets.reserve(operations.size());
    result.puts.reserve(operations.size());

    for (const Operation& operation : operations) {
        if (operation.type == OperationType::Get) {
            result.gets.push_back(&operation);
        } else {
            result.puts.push_back(&operation);
        }
    }

    return result;
}

std::vector<double> MergeLatencies(const std::vector<std::vector<double>>& per_thread_latencies) {
    size_t total_count = 0;
    for (const auto& latencies : per_thread_latencies) {
        total_count += latencies.size();
    }

    std::vector<double> result;
    result.reserve(total_count);
    for (const auto& latencies : per_thread_latencies) {
        result.insert(result.end(), latencies.begin(), latencies.end());
    }

    return result;
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
    std::vector<double> get_latencies_ns;

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
        PartitionedOperations partitioned = PartitionOperations(operations);
        get_latencies_ns.clear();

        if (!partitioned.gets.empty() && scenario.read_threads == 0) {
            state.ResumeTiming();
            state.SkipWithError("get operations require --read-threads > 0");
            return;
        }
        if (!partitioned.puts.empty() && scenario.write_threads == 0) {
            state.ResumeTiming();
            state.SkipWithError("put operations require --write-threads > 0");
            return;
        }

        WarmupCache(*cache, scenario, value, ttl);

        std::atomic<bool> start{false};
        std::atomic<size_t> ready_count{0};
        std::atomic<size_t> next_get{0};
        std::atomic<size_t> next_put{0};
        std::atomic<size_t> current_put_errors{0};
        std::vector<std::vector<double>> per_reader_latencies(scenario.read_threads);
        std::vector<std::thread> workers;
        workers.reserve(scenario.read_threads + scenario.write_threads);

        auto wait_for_start = [&]() {
            ready_count.fetch_add(1, std::memory_order_release);
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
        };

        for (size_t reader_id = 0; reader_id < scenario.read_threads; ++reader_id) {
            workers.emplace_back([&, reader_id] {
                wait_for_start();

                auto& latencies = per_reader_latencies[reader_id];
                latencies.reserve(partitioned.gets.size() / scenario.read_threads + 1);

                while (true) {
                    size_t index = next_get.fetch_add(1, std::memory_order_relaxed);
                    if (index >= partitioned.gets.size()) {
                        break;
                    }

                    const Operation& operation = *partitioned.gets[index];
                    auto started = std::chrono::steady_clock::now();
                    auto result = cache->Get(operation.key);
                    auto finished = std::chrono::steady_clock::now();
                    benchmark::DoNotOptimize(result);
                    latencies.push_back(static_cast<double>(
                        std::chrono::duration_cast<std::chrono::nanoseconds>(finished - started).count()));
                }
            });
        }

        for (size_t writer_id = 0; writer_id < scenario.write_threads; ++writer_id) {
            workers.emplace_back([&] {
                wait_for_start();

                while (true) {
                    size_t index = next_put.fetch_add(1, std::memory_order_relaxed);
                    if (index >= partitioned.puts.size()) {
                        break;
                    }

                    const Operation& operation = *partitioned.puts[index];
                    auto result = cache->Put(operation.key, value, ttl);
                    benchmark::DoNotOptimize(result);

                    if (!result.has_value()) {
                        current_put_errors.fetch_add(1, std::memory_order_relaxed);
                    }
                }
            });
        }

        while (ready_count.load(std::memory_order_acquire) < workers.size()) {
            std::this_thread::yield();
        }

        state.ResumeTiming();

        start.store(true, std::memory_order_release);
        for (std::thread& worker : workers) {
            worker.join();
        }

        state.PauseTiming();
        get_latencies_ns = MergeLatencies(per_reader_latencies);
        stats = cache->Stats();
        put_errors = current_put_errors.load(std::memory_order_relaxed);
        state.ResumeTiming();
    }

    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) *
                            static_cast<int64_t>(scenario.request_count));
    state.counters["hits"] = static_cast<double>(stats.hits);
    state.counters["misses"] = static_cast<double>(stats.misses);
    state.counters["hit_ratio"] = stats.HitRatio();
    state.counters["get_p50_ns"] = PercentileNs(get_latencies_ns, 0.50);
    state.counters["get_p99_ns"] = PercentileNs(get_latencies_ns, 0.99);
    state.counters["puts"] = static_cast<double>(stats.puts);
    state.counters["evictions"] = static_cast<double>(stats.evictions);
    state.counters["expired"] = static_cast<double>(stats.expired);
    state.counters["cache_size"] = static_cast<double>(stats.size);
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
