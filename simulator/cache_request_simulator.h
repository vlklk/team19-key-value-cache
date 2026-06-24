#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <expected>
#include <optional>
#include <string>

#include "cache/cache.h"
#include "database/txt_database.h"


struct CacheRequestSimulatorStats {
public:
    size_t cache_hits = 0;
    size_t cache_misses = 0;
    size_t database_hits = 0;
    size_t database_misses = 0;
    size_t writes = 0;
    size_t erases = 0;
    size_t cache_store_errors = 0;

    double HitRatio() const {
        size_t count = cache_hits + cache_misses;

        if (count == 0) { return 0.0; }

        return static_cast<double>(cache_hits) / static_cast<double>(count);
    }
};


class CacheRequestSimulator {
public:
  
    explicit CacheRequestSimulator(
        Cache& cache, TxtDatabase& database,
        std::chrono::nanoseconds backing_store_latency = std::chrono::nanoseconds::zero())
        : cache_(cache), database_(database), backing_store_latency_(backing_store_latency) {}

    std::optional<std::string> Get(const std::string& key) {
        auto cached = cache_.Get(key);

        if (cached.has_value()) {
            cache_hits_.fetch_add(1, std::memory_order_relaxed);
            return cached;
        }

        cache_misses_.fetch_add(1, std::memory_order_relaxed);

        BackingStoreDelay();
        auto stored = database_.Get(key);

        if (!stored.has_value()) {
            database_misses_.fetch_add(1, std::memory_order_relaxed);
            return std::nullopt;
        }

        database_hits_.fetch_add(1, std::memory_order_relaxed);

        auto result = cache_.Put(key, *stored);
        if (!result.has_value()) {
            cache_store_errors_.fetch_add(1, std::memory_order_relaxed);
        }

        return stored;
    }

    std::expected<void, std::string> Set(const std::string& key, const std::string& value) {
        BackingStoreDelay();
        auto database_result = database_.Set(key, value);

        if (!database_result.has_value()) {
            return std::unexpected(database_result.error());
        }

        auto cache_result = cache_.Put(key, value);
        if (!cache_result.has_value()) {
            cache_store_errors_.fetch_add(1, std::memory_order_relaxed);
        }

        writes_.fetch_add(1, std::memory_order_relaxed);

        return {};
    }

    bool Erase(const std::string& key) {
        bool erased_from_database = database_.Erase(key);
        bool erased_from_cache = cache_.Erase(key);

        if (erased_from_database || erased_from_cache) {
            erases_.fetch_add(1, std::memory_order_relaxed);
        }

        return erased_from_database || erased_from_cache;
    }

    CacheRequestSimulatorStats Stats() const {
        CacheRequestSimulatorStats stats;
        stats.cache_hits = cache_hits_.load(std::memory_order_relaxed);
        stats.cache_misses = cache_misses_.load(std::memory_order_relaxed);
        stats.database_hits = database_hits_.load(std::memory_order_relaxed);
        stats.database_misses = database_misses_.load(std::memory_order_relaxed);
        stats.writes = writes_.load(std::memory_order_relaxed);
        stats.erases = erases_.load(std::memory_order_relaxed);
        stats.cache_store_errors = cache_store_errors_.load(std::memory_order_relaxed);
        return stats;
    }

private:
    void BackingStoreDelay() const {
        if (backing_store_latency_ == std::chrono::nanoseconds::zero()) { return; }

        auto deadline = std::chrono::steady_clock::now() + backing_store_latency_;
        while (std::chrono::steady_clock::now() < deadline) {}
    }

    Cache& cache_;
    TxtDatabase& database_;
    std::chrono::nanoseconds backing_store_latency_;

    std::atomic<size_t> cache_hits_{0};
    std::atomic<size_t> cache_misses_{0};
    std::atomic<size_t> database_hits_{0};
    std::atomic<size_t> database_misses_{0};
    std::atomic<size_t> writes_{0};
    std::atomic<size_t> erases_{0};
    std::atomic<size_t> cache_store_errors_{0};
};
