#pragma once

#include <cstddef>
#include <expected>
#include <mutex>
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

    double HitRatio() const {
        size_t count = cache_hits + cache_misses;

        if (count == 0) { return 0.0; }

        return static_cast<double>(cache_hits) / static_cast<double>(count);
    }
};


class CacheRequestSimulator {
public:
    CacheRequestSimulator(Cache& cache, TxtDatabase& database)
        : cache_(cache), database_(database) {}

    std::optional<std::string> Get(const std::string& key) {
        auto cached = cache_.Get(key);

        if (cached.has_value()) {
            AddCacheHit();
            return cached;
        }

        AddCacheMiss();

        auto stored = database_.Get(key);

        if (!stored.has_value()) {
            AddDatabaseMiss();
            return std::nullopt;
        }

        AddDatabaseHit();

        auto result = cache_.Put(key, *stored);

        if (!result.has_value()) { return std::nullopt; }

        return stored;
    }

    std::expected<void, std::string> Set(const std::string& key, const std::string& value) {
        auto database_result = database_.Set(key, value);

        if (!database_result.has_value()) {
            return std::unexpected(database_result.error());
        }

        auto cache_result = cache_.Put(key, value);

        if (!cache_result.has_value()) {
            return std::unexpected(cache_result.error());
        }

        AddWrite();

        return {};
    }

    bool Erase(const std::string& key) {
        bool erased_from_database = database_.Erase(key);
        bool erased_from_cache = cache_.Erase(key);

        if (erased_from_database || erased_from_cache) { AddErase(); }

        return erased_from_database || erased_from_cache;
    }

    CacheRequestSimulatorStats Stats() const {
        std::lock_guard<std::mutex> lock(mutex_);

        return stats_;
    }

private:
    void AddCacheHit() {
        std::lock_guard<std::mutex> lock(mutex_);
        ++stats_.cache_hits;
    }

    void AddCacheMiss() {
        std::lock_guard<std::mutex> lock(mutex_);
        ++stats_.cache_misses;
    }

    void AddDatabaseHit() {
        std::lock_guard<std::mutex> lock(mutex_);
        ++stats_.database_hits;
    }

    void AddDatabaseMiss() {
        std::lock_guard<std::mutex> lock(mutex_);
        ++stats_.database_misses;
    }

    void AddWrite() {
        std::lock_guard<std::mutex> lock(mutex_);
        ++stats_.writes;
    }

    void AddErase() {
        std::lock_guard<std::mutex> lock(mutex_);
        ++stats_.erases;
    }

    Cache& cache_;
    TxtDatabase& database_;
    CacheRequestSimulatorStats stats_;
    mutable std::mutex mutex_;
};
