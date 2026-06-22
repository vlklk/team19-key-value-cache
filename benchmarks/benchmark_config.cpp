#include "benchmark_config.h"

#include <charconv>
#include <iostream>
#include <sstream>
#include <string_view>

namespace {

BenchmarkConfig DefaultBenchmarkConfig() {
    BenchmarkConfig config;
    config.policies = {
        PolicyType::Lru,
        PolicyType::Lfu,
        PolicyType::Random,
        PolicyType::NoEviction,
    };
    config.profiles = {
        WorkloadProfile::Uniform,
        WorkloadProfile::Zipf,
        WorkloadProfile::SequentialLoop,
        WorkloadProfile::ReadHeavy,
        WorkloadProfile::WriteHeavy,
        WorkloadProfile::Burst,
    };
    config.cache_sizes = {1024};
    config.key_space_sizes = {10000};
    config.value_sizes = {64};
    config.request_counts = {10000};
    config.get_ratios = {70};
    config.ttls = {std::chrono::milliseconds{0}};
    config.read_threads = {1};
    config.write_threads = {1};
    config.runs_per_config = 1;
    config.seed = 69420;
    return config;
}

std::vector<std::string_view> Split(std::string_view value) {
    std::vector<std::string_view> result;
    size_t start = 0;
    while (start <= value.size()) {
        size_t comma = value.find(',', start);
        if (comma == std::string_view::npos) {
            result.push_back(value.substr(start));
            break;
        }
        result.push_back(value.substr(start, comma - start));
        start = comma + 1;
    }
    return result;
}

std::expected<size_t, std::string> ParseSize(std::string_view value, std::string_view name,
                                             bool zero_allowed) {
    if (value.empty()) { 
        return std::unexpected("empty value for --" + std::string(name)); 
    }

    size_t result = 0;
    auto begin = value.data();
    auto end = value.data() + value.size();
    auto parse_result = std::from_chars(begin, end, result);
    if (parse_result.ec != std::errc{} || parse_result.ptr != end) {
        return std::unexpected("invalid number for --" + std::string(name) + ": " +
                               std::string(value));
    }
    if (!zero_allowed && result == 0) {
        return std::unexpected("--" + std::string(name) + " must be positive");
    }

    return result;
}

std::expected<uint64_t, std::string> ParseSeed(std::string_view value) {
    if (value.empty()) { return std::unexpected("empty value for --seed"); }

    uint64_t result = 0;
    auto begin = value.data();
    auto end = value.data() + value.size();
    auto parse_result = std::from_chars(begin, end, result);
    if (parse_result.ec != std::errc{} || parse_result.ptr != end) {
        return std::unexpected("invalid number for --seed: " + std::string(value));
    }

    return result;
}

template <typename T, typename Parser>
std::expected<std::vector<T>, std::string> ParseList(std::string_view value, Parser parser) {
    std::vector<T> result;

    for (std::string_view part : Split(value)) {
        auto parsed = parser(part);

        if (!parsed.has_value()) { 
            return std::unexpected(parsed.error()); 
        }

        result.push_back(parsed.value());
    }

    return result;
}

std::expected<std::vector<size_t>, std::string> ParseSizeList(std::string_view value,
    std::string_view name, bool zero_allowed) {
        
    return ParseList<size_t>(value, [name, zero_allowed](std::string_view part) {
        return ParseSize(part, name, zero_allowed);
    });
}

std::expected<std::vector<std::chrono::milliseconds>, std::string> ParseTtlList(
    std::string_view value) {
    return ParseList<std::chrono::milliseconds>(value, [](std::string_view part)
        -> std::expected<std::chrono::milliseconds, std::string> {
        auto parsed = ParseSize(part, "ttl-ms", true);

        if (!parsed.has_value()) { return std::unexpected(parsed.error()); }

        return std::chrono::milliseconds{parsed.value()};
    });
}

std::expected<PolicyType, std::string> ParsePolicy(std::string_view value) {
    if (value == "lru") { 
        return PolicyType::Lru; 
    }
    if (value == "lfu") { 
        return PolicyType::Lfu; 
    }
    if (value == "random") { 
        return PolicyType::Random; 
    }
    if (value == "noeviction") { 
        return PolicyType::NoEviction; 
    }
    return std::unexpected("unknown policy: " + std::string(value));
}

std::expected<std::vector<PolicyType>, std::string> ParsePolicies(std::string_view value) {
    if (value == "all") {
        return std::vector<PolicyType>{
            PolicyType::Lru,
            PolicyType::Lfu,
            PolicyType::Random,
            PolicyType::NoEviction,
        };
    }

    return ParseList<PolicyType>(value, ParsePolicy);
}

std::expected<WorkloadProfile, std::string> ParseProfile(std::string_view value) {
    if (value == "uniform") { 
        return WorkloadProfile::Uniform; 
    }
    if (value == "zipf") { 
        return WorkloadProfile::Zipf; 
    }
    if (value == "sequential-loop") { 
        return WorkloadProfile::SequentialLoop; 
    }
    if (value == "sequential_loop") { 
        return WorkloadProfile::SequentialLoop; 
    }
    if (value == "read-heavy") { 
        return WorkloadProfile::ReadHeavy; 
    }
    if (value == "read_heavy") { 
        return WorkloadProfile::ReadHeavy; 
    }
    if (value == "write-heavy") { 
        return WorkloadProfile::WriteHeavy; 
    }
    if (value == "write_heavy") { 
        return WorkloadProfile::WriteHeavy; 
    }
    if (value == "burst") { 
        return WorkloadProfile::Burst; 
    }

    return std::unexpected("unknown profile: " + std::string(value));
}

std::expected<std::vector<WorkloadProfile>, std::string> ParseProfiles(std::string_view value) {
    if (value == "all") {
        return std::vector<WorkloadProfile>{
            WorkloadProfile::Uniform,
            WorkloadProfile::Zipf,
            WorkloadProfile::SequentialLoop,
            WorkloadProfile::ReadHeavy,
            WorkloadProfile::WriteHeavy,
            WorkloadProfile::Burst,
        };
    }

    return ParseList<WorkloadProfile>(value, ParseProfile);
}

bool IsCacheBenchmarkFlag(std::string_view name) {
    return name == "policy" || name == "profile" || name == "cache-size" ||
           name == "key-space" || name == "value-size" || name == "requests" ||
           name == "get-ratio" || name == "ttl-ms" || name == "read-threads" ||
           name == "write-threads" || name == "runs" || name == "seed";
}

std::expected<void, std::string> ParseOption(BenchmarkConfig& config, 
    std::string_view name, std::string_view value) {

    if (name == "policy") {
        auto parsed = ParsePolicies(value);
        if (!parsed.has_value()) { 
            return std::unexpected(parsed.error()); 
        }
        config.policies = std::move(parsed.value());
        return {};
    }
    if (name == "profile") {
        auto parsed = ParseProfiles(value);
        if (!parsed.has_value()) { 
            return std::unexpected(parsed.error()); 
        }
        config.profiles = std::move(parsed.value());
        return {};
    }
    if (name == "cache-size") {
        auto parsed = ParseSizeList(value, name, false);
        if (!parsed.has_value()) { 
            return std::unexpected(parsed.error()); 
        }
        config.cache_sizes = std::move(parsed.value());
        return {};
    }
    if (name == "key-space") {
        auto parsed = ParseSizeList(value, name, false);
        if (!parsed.has_value()) { 
            return std::unexpected(parsed.error()); 
        }
        config.key_space_sizes = std::move(parsed.value());
        return {};
    }
    if (name == "value-size") {
        auto parsed = ParseSizeList(value, name, false);
        if (!parsed.has_value()) { 
            return std::unexpected(parsed.error()); 
        }
        config.value_sizes = std::move(parsed.value());
        return {};
    }
    if (name == "requests") {
        auto parsed = ParseSizeList(value, name, false);
        if (!parsed.has_value()) { 
            return std::unexpected(parsed.error()); 
        }
        config.request_counts = std::move(parsed.value());
        return {};
    }
    if (name == "get-ratio") {
        auto parsed = ParseSizeList(value, name, true);
        if (!parsed.has_value()) { 
            return std::unexpected(parsed.error()); 
        }

        for (size_t ratio : parsed.value()) {
            if (ratio > 100) { 
                return std::unexpected("--get-ratio must be between 0 and 100"); 
            }
        }

        config.get_ratios = std::move(parsed.value());
        return {};
    }
    if (name == "ttl-ms") {
        auto parsed = ParseTtlList(value);
        if (!parsed.has_value()) { 
            return std::unexpected(parsed.error()); 
        }
        config.ttls = std::move(parsed.value());
        return {};
    }
    if (name == "read-threads") {
        auto parsed = ParseSizeList(value, name, true);
        if (!parsed.has_value()) { 
            return std::unexpected(parsed.error()); 
        }
        config.read_threads = std::move(parsed.value());
        return {};
    }
    if (name == "write-threads") {
        auto parsed = ParseSizeList(value, name, true);
        if (!parsed.has_value()) { 
            return std::unexpected(parsed.error()); 
        }
        config.write_threads = std::move(parsed.value());
        return {};
    }
    if (name == "runs") {
        auto parsed = ParseSize(value, name, false);
        if (!parsed.has_value()) { 
            return std::unexpected(parsed.error()); 
        }
        config.runs_per_config = parsed.value();
        return {};
    }
    if (name == "seed") {
        auto parsed = ParseSeed(value);
        if (!parsed.has_value()) { 
            return std::unexpected(parsed.error()); 
        }
        config.seed = parsed.value();
        return {};
    }

    return std::unexpected("unknown benchmark option: --" + std::string(name));
}

}  // namespace

std::expected<BenchmarkConfig, std::string> ParseBenchmarkConfig(int* argc, char** argv) {
    BenchmarkConfig config = DefaultBenchmarkConfig();
    std::vector<char*> benchmark_args;
    benchmark_args.push_back(argv[0]);

    for (int i = 1; i < *argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "--help") {
            config.help_requested = true;
            continue;
        }
        if (!arg.starts_with("--")) {
            benchmark_args.push_back(argv[i]);
            continue;
        }

        size_t equals = arg.find('=');
        std::string_view name = arg.substr(2, equals == std::string_view::npos
                                                  ? std::string_view::npos
                                                  : equals - 2);

        if (!IsCacheBenchmarkFlag(name)) {
            benchmark_args.push_back(argv[i]);
            continue;
        }
        if (equals == std::string_view::npos) { 
            return std::unexpected("expected --name=value for benchmark option: " + std::string(arg)); 
        }
        std::string_view value = arg.substr(equals + 1);
        auto parsed = ParseOption(config, name, value);

        if (!parsed.has_value()) { 
            return std::unexpected(parsed.error()); 
        }
    }

    for (size_t i = 0; i < benchmark_args.size(); ++i) {
        argv[i] = benchmark_args[i];
    }

    *argc = static_cast<int>(benchmark_args.size());
    return config;
}

std::vector<BenchmarkScenario> MakeBenchmarkScenarios(const BenchmarkConfig& config) {
    std::vector<BenchmarkScenario> scenarios;

    for (PolicyType policy : config.policies) {
        for (WorkloadProfile profile : config.profiles) {
            for (size_t cache_size : config.cache_sizes) {
                for (size_t key_space_size : config.key_space_sizes) {
                    for (size_t value_size : config.value_sizes) {
                        for (size_t request_count : config.request_counts) {
                            for (size_t get_ratio : config.get_ratios) {
                                for (auto ttl : config.ttls) {
                                    for (size_t read_threads : config.read_threads) {
                                        for (size_t write_threads : config.write_threads) {
                                            scenarios.push_back(BenchmarkScenario{
                                                policy,
                                                profile,
                                                cache_size,
                                                key_space_size,
                                                value_size,
                                                request_count,
                                                get_ratio,
                                                ttl,
                                                read_threads,
                                                write_threads,
                                                config.seed,
                                            });
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    return scenarios;
}

std::string BuildBenchmarkName(const BenchmarkScenario& scenario) {
    std::ostringstream out;
    out << "BMCachePutGet";
    out << "/policy:" << ToString(scenario.policy);
    out << "/profile:" << ToString(scenario.profile);
    out << "/cache:" << scenario.cache_size;
    out << "/keys:" << scenario.key_space_size;
    out << "/value:" << scenario.value_size;
    out << "/requests:" << scenario.request_count;
    out << "/get:" << scenario.get_ratio;
    out << "/ttl_ms:" << scenario.ttl.count();
    out << "/read_threads:" << scenario.read_threads;
    out << "/write_threads:" << scenario.write_threads;
    return out.str();
}

std::string ToString(PolicyType policy) {
    switch (policy) {
        case PolicyType::Lru:
            return "lru";
        case PolicyType::Lfu:
            return "lfu";
        case PolicyType::Random:
            return "random";
        case PolicyType::NoEviction:
            return "noeviction";
    }

    return "unknown";
}

std::string ToString(WorkloadProfile profile) {
    switch (profile) {
        case WorkloadProfile::Uniform:
            return "uniform";
        case WorkloadProfile::Zipf:
            return "zipf";
        case WorkloadProfile::SequentialLoop:
            return "sequential_loop";
        case WorkloadProfile::ReadHeavy:
            return "read_heavy";
        case WorkloadProfile::WriteHeavy:
            return "write_heavy";
        case WorkloadProfile::Burst:
            return "burst";
    }

    return "unknown";
}

void PrintBenchmarkConfigHelp() {
    std::cout << "Cache benchmark options:\n"
              << "  --policy=lru|lfu|random|noeviction|all\n"
              << "  --profile=uniform|zipf|sequential-loop|read-heavy|write-heavy|burst|all\n"
              << "  --cache-size=N[,N...]\n"
              << "  --key-space=N[,N...]\n"
              << "  --value-size=N[,N...]\n"
              << "  --requests=N[,N...]\n"
              << "  --get-ratio=N[,N...] where 0 <= N <= 100\n"
              << "  --ttl-ms=N[,N...] where 0 is disabled\n"
              << "  --read-threads=N[,N...]\n"
              << "  --write-threads=N[,N...]\n"
              << "  --runs=N\n"
              << "  --seed=N\n"
              << "  --help\n";
}
