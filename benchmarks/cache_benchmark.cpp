#include <benchmark/benchmark.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <filesystem>
#include <vector>

#include "benchmark_config.h"
#include "cache/cache.h"
#include "database/txt_database.h"
#include "simulator/cache_request_simulator.h"
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

std::string MakeKey(size_t index) {
    return "key:" + std::to_string(index);
}

std::expected<std::filesystem::path, std::string> CreateDatabaseFile(
    const BenchmarkScenario& scenario, const std::string& value) {
    static std::atomic<size_t> database_id{0};

    std::error_code error;
    std::filesystem::path directory = std::filesystem::temp_directory_path(error);
    if (error) {
        return std::unexpected(error.message());
    }

    std::filesystem::path path = directory / (
        "cache_benchmark_simulator_" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
        "_" + std::to_string(database_id.fetch_add(1, std::memory_order_relaxed)) +
        ".txt");

    std::ofstream output(path, std::ios::trunc);
    if (!output.is_open()) {
        return std::unexpected("cannot create simulator database file: " + path.string());
    }

    for (size_t i = 0; i < scenario.key_space_size; ++i) {
        output << MakeKey(i) << '=' << value << '\n';
    }

    if (!output.good()) {
        return std::unexpected("cannot write simulator database file: " + path.string());
    }

    return path;
}

struct DatabaseFileGuard {
    explicit DatabaseFileGuard(std::filesystem::path path) : path(std::move(path)) {}

    ~DatabaseFileGuard() {
        if (!path.empty()) {
            std::error_code error;
            std::filesystem::remove(path, error);
        }
    }

    std::filesystem::path path;
};

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

CacheRequestSimulatorStats WorkloadSimulatorStats(const CacheRequestSimulatorStats& before,
                                                   const CacheRequestSimulatorStats& after) {
    CacheRequestSimulatorStats result;
    result.cache_hits = after.cache_hits - before.cache_hits;
    result.cache_misses = after.cache_misses - before.cache_misses;
    result.database_hits = after.database_hits - before.database_hits;
    result.database_misses = after.database_misses - before.database_misses;
    result.writes = after.writes - before.writes;
    result.erases = after.erases - before.erases;
    result.cache_store_errors = after.cache_store_errors - before.cache_store_errors;
    return result;
}

CacheStats WorkloadCacheStats(const CacheStats& before, const CacheStats& after) {
    CacheStats result;
    result.hits = after.hits - before.hits;
    result.misses = after.misses - before.misses;
    result.puts = after.puts - before.puts;
    result.erases = after.erases - before.erases;
    result.evictions = after.evictions - before.evictions;
    result.expired = after.expired - before.expired;
    result.used_memory = after.used_memory;
    result.size = after.size;
    return result;
}

void WarmupSimulator(CacheRequestSimulator& simulator, const BenchmarkScenario& scenario) {
    size_t warmup_count = std::min(scenario.cache_size, scenario.key_space_size);
    for (size_t i = 0; i < warmup_count; ++i) {
        auto result = simulator.Get(MakeKey(i));
        benchmark::DoNotOptimize(result);
    }
}

void BMSimulatorWorkload(benchmark::State& state, BenchmarkScenario scenario) {
    CacheRequestSimulatorStats simulator_stats;
    CacheStats cache_stats;
    size_t set_errors = 0;
    size_t get_null_results = 0;
    size_t processed_operations = 0;
    std::vector<double> get_latencies_ns;

    for (auto _ : state) {
        state.PauseTiming();

        CacheConfig cache_config;
        cache_config.max_count = scenario.cache_size;
        cache_config.policy = scenario.policy;
        cache_config.ttl = ToCacheTtl(scenario.ttl);

        auto cache_result = MakeCache(cache_config);
        if (!cache_result.has_value()) {
            state.ResumeTiming();
            state.SkipWithError(cache_result.error().c_str());
            return;
        }

        auto cache = std::move(cache_result.value());
        std::string value(scenario.value_size, 'x');

        auto database_path_result = CreateDatabaseFile(scenario, value);
        if (!database_path_result.has_value()) {
            state.ResumeTiming();
            state.SkipWithError(database_path_result.error().c_str());
            return;
        }

        DatabaseFileGuard database_file(database_path_result.value());
        TxtDatabase database(database_file.path);
        CacheRequestSimulator simulator(*cache, database);

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

        WarmupSimulator(simulator, scenario);
        CacheRequestSimulatorStats baseline_simulator_stats = simulator.Stats();
        CacheStats baseline_cache_stats = cache->Stats();

        std::atomic<bool> start{false};
        std::atomic<size_t> ready_count{0};
        std::atomic<size_t> next_get{0};
        std::atomic<size_t> next_put{0};
        std::atomic<size_t> current_set_errors{0};
        std::atomic<size_t> current_get_null_results{0};
        std::atomic<size_t> current_processed_operations{0};
        const bool duration_mode = scenario.duration > std::chrono::milliseconds::zero();
        std::chrono::steady_clock::time_point stop_at;
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
                if (partitioned.gets.empty()) {
                    return;
                }

                auto& latencies = per_reader_latencies[reader_id];
                latencies.reserve(partitioned.gets.size() / scenario.read_threads + 1);

                while (true) {
                    size_t index = next_get.fetch_add(1, std::memory_order_relaxed);
                    if (!duration_mode && index >= partitioned.gets.size()) {
                        break;
                    }
                    if (duration_mode && std::chrono::steady_clock::now() >= stop_at) {
                        break;
                    }
                    index %= partitioned.gets.size();

                    const Operation& operation = *partitioned.gets[index];
                    auto started = std::chrono::steady_clock::now();
                    auto result = simulator.Get(operation.key);
                    auto finished = std::chrono::steady_clock::now();
                    benchmark::DoNotOptimize(result);
                    latencies.push_back(static_cast<double>(
                        std::chrono::duration_cast<std::chrono::nanoseconds>(
                            finished - started).count()));
                    current_processed_operations.fetch_add(1, std::memory_order_relaxed);

                    if (!result.has_value()) {
                        current_get_null_results.fetch_add(1, std::memory_order_relaxed);
                    }
                }
            });
        }

        for (size_t writer_id = 0; writer_id < scenario.write_threads; ++writer_id) {
            workers.emplace_back([&] {
                wait_for_start();
                if (partitioned.puts.empty()) {
                    return;
                }

                while (true) {
                    size_t index = next_put.fetch_add(1, std::memory_order_relaxed);
                    if (!duration_mode && index >= partitioned.puts.size()) {
                        break;
                    }
                    if (duration_mode && std::chrono::steady_clock::now() >= stop_at) {
                        break;
                    }
                    index %= partitioned.puts.size();

                    const Operation& operation = *partitioned.puts[index];
                    auto result = simulator.Set(operation.key, value);
                    benchmark::DoNotOptimize(result);
                    current_processed_operations.fetch_add(1, std::memory_order_relaxed);

                    if (!result.has_value()) {
                        current_set_errors.fetch_add(1, std::memory_order_relaxed);
                    }
                }
            });
        }

        while (ready_count.load(std::memory_order_acquire) < workers.size()) {
            std::this_thread::yield();
        }

        state.ResumeTiming();

        if (duration_mode) {
            stop_at = std::chrono::steady_clock::now() + scenario.duration;
        }
        start.store(true, std::memory_order_release);
        for (std::thread& worker : workers) {
            worker.join();
        }

        state.PauseTiming();
        get_latencies_ns = MergeLatencies(per_reader_latencies);
        simulator_stats = WorkloadSimulatorStats(baseline_simulator_stats, simulator.Stats());
        cache_stats = WorkloadCacheStats(baseline_cache_stats, cache->Stats());
        set_errors = current_set_errors.load(std::memory_order_relaxed);
        get_null_results = current_get_null_results.load(std::memory_order_relaxed);
        processed_operations += current_processed_operations.load(std::memory_order_relaxed);
        state.ResumeTiming();
    }

    state.SetItemsProcessed(static_cast<int64_t>(processed_operations));
    state.counters["hits"] = static_cast<double>(simulator_stats.cache_hits);
    state.counters["misses"] = static_cast<double>(simulator_stats.cache_misses);
    state.counters["hit_ratio"] = simulator_stats.HitRatio();
    state.counters["get_p50_ns"] = PercentileNs(get_latencies_ns, 0.50);
    state.counters["get_p99_ns"] = PercentileNs(get_latencies_ns, 0.99);
    state.counters["database_hits"] = static_cast<double>(simulator_stats.database_hits);
    state.counters["database_misses"] = static_cast<double>(simulator_stats.database_misses);
    state.counters["writes"] = static_cast<double>(simulator_stats.writes);
    state.counters["puts"] = static_cast<double>(cache_stats.puts);
    state.counters["cache_store_errors"] = static_cast<double>(simulator_stats.cache_store_errors);
    state.counters["erases"] = static_cast<double>(simulator_stats.erases);
    state.counters["evictions"] = static_cast<double>(cache_stats.evictions);
    state.counters["expired"] = static_cast<double>(cache_stats.expired);
    state.counters["cache_size"] = static_cast<double>(cache_stats.size);
    state.counters["memory_estimate"] = static_cast<double>(cache_stats.used_memory);
    state.counters["set_errors"] = static_cast<double>(set_errors);
    state.counters["put_errors"] = static_cast<double>(simulator_stats.cache_store_errors);
    state.counters["get_null_results"] = static_cast<double>(get_null_results);
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
            BMSimulatorWorkload, scenario)
            ->Iterations(1)
            ->Repetitions(static_cast<int>(config.runs_per_config))
            ->UseRealTime();
    }

    benchmark::RunSpecifiedBenchmarks();
    benchmark::Shutdown();

    return 0;
}
