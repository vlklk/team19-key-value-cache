#pragma once

#include <chrono>
#include <expected>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

#include "policies/eviction_policy.h"
#include "policies/noeviction_policy.h"
#include "policies/policy_base.h"

struct CacheEntry {
public:
    std::string value;
    std::optional<std::chrono::steady_clock::time_point> ends = std::nullopt;
    size_t memory = 0;
};

struct CacheConfig {
public:
    size_t max_memory = 0;
    size_t max_count = 0;
    std::optional<std::chrono::seconds> ttl = std::nullopt;
    PolicyType policy = PolicyType::Lru;
};

struct CacheStats {
public:
    size_t hits = 0;
    size_t misses = 0;
    size_t puts = 0;
    size_t erases = 0;
    size_t evictions = 0;
    size_t expired = 0;
    size_t used_memory = 0;
    size_t size = 0;

    double HitRatio() const {
        size_t count = hits + misses;

        if (count == 0) { return 0.0; }
        return static_cast<double>(hits) / static_cast<double>(count);
    }
};

class Cache {

private:
    CacheConfig config_;
    std::unique_ptr<EvictionPolicy> policy_;
    std::unordered_map<std::string, CacheEntry> data_;

    size_t used_memory_ = 0;
    CacheStats stats_;

    mutable std::mutex mutex_;

    CacheEntry* Find(const std::string& key) {
        auto it = data_.find(key);

        if (it == data_.end()) { return nullptr; }

        if (IsExpired(it->second)) {
            DeleteKey(key);
            ++stats_.expired;
            return nullptr;
        }

        return &it->second;
    }

    bool IsExpired(const CacheEntry& entry) const {
        if (!entry.ends.has_value()) { return false; }

        return entry.ends.value() <= std::chrono::steady_clock::now();
    }

    void DeleteOldKeys() {
        auto it = data_.begin();

        while (it != data_.end()) {
            if (IsExpired(it->second)) {
                used_memory_ -= it->second.memory;
                policy_->OnErase(it->first);
                it = data_.erase(it);
                ++stats_.expired;
            } else {
                ++it;
            }
        }
    }

    void DeleteKey(const std::string& key) {
        auto it = data_.find(key);

        if (it == data_.end()) { return; }

        used_memory_ -= it->second.memory;
        policy_->OnErase(key);
        data_.erase(it);
    }

    size_t EstimateMemory(const std::string& key, const std::string& value) const {
        return key.size() + value.size() + sizeof(CacheEntry);
    }

    bool NeedEviction(size_t new_memory) const {
        if (config_.max_memory != 0 && used_memory_ + new_memory > config_.max_memory) {
            return true;
        }

        if (config_.max_count != 0 && data_.size() + 1 > config_.max_count) { return true; }

        return false;
    }

    std::expected<void, std::string> MakeSpace(size_t new_memory) {
        while (NeedEviction(new_memory)) {
            std::optional<std::string> victim = policy_->Victim();

            if (!victim.has_value()) {
                return std::unexpected("eviction policy does not allow insert");
            }

            if (!data_.contains(victim.value())) {
                policy_->OnErase(victim.value());
                continue;
            }

            DeleteKey(victim.value());
            ++stats_.evictions;
        }

        return {};
    }

public:
    Cache(CacheConfig config, std::unique_ptr<EvictionPolicy> policy)
        : config_(std::move(config)), policy_(std::move(policy)) {

        if (!policy_) { policy_ = std::make_unique<NoEvictionPolicy>(); }
    }

    std::expected<void, std::string> Put(const std::string& key, const std::string& value,
                                         std::optional<std::chrono::seconds> ttl = std::nullopt) {
        std::lock_guard<std::mutex> lock(mutex_);
        DeleteOldKeys();

        if (key.empty()) { return std::unexpected("empty key"); }

        std::optional<std::chrono::seconds> real_ttl = ttl;

        if (!real_ttl.has_value()) { real_ttl = config_.ttl; }

        if (real_ttl.has_value() && real_ttl.value().count() <= 0) {
            return std::unexpected("ttl must be positive");
        }

        size_t new_memory = EstimateMemory(key, value);

        if (config_.max_memory != 0 && new_memory > config_.max_memory) {
            return std::unexpected("value is bigger than max memory");
        }

        std::optional<CacheEntry> old_entry = std::nullopt;
        auto it = data_.find(key);

        if (it != data_.end()) {
            old_entry = std::move(it->second);
            used_memory_ -= old_entry.value().memory;
            policy_->OnErase(key);
            data_.erase(it);
        }

        auto free_result = MakeSpace(new_memory);

        if (!free_result.has_value()) {
            if (old_entry.has_value()) {
                used_memory_ += old_entry.value().memory;
                data_[key] = std::move(old_entry.value());
                policy_->OnInsert(key);
            }

            return std::unexpected(free_result.error());
        }

        CacheEntry entry;
        entry.value = value;
        entry.memory = new_memory;

        if (real_ttl.has_value()) {
            entry.ends = std::chrono::steady_clock::now() + real_ttl.value();
        }

        data_[key] = std::move(entry);
        used_memory_ += new_memory;
        policy_->OnInsert(key);

        ++stats_.puts;
        return {};
    }

    std::optional<std::string> Get(const std::string& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        CacheEntry* entry = Find(key);

        if (!entry) {
            ++stats_.misses;
            return std::nullopt;
        }

        ++stats_.hits;
        policy_->OnAccess(key);

        return entry->value;
    }

    bool Erase(const std::string& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        CacheEntry* entry = Find(key);

        if (!entry) { return false; }

        DeleteKey(key);
        ++stats_.erases;

        return true;
    }

    void Clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        data_.clear();
        used_memory_ = 0;
        policy_->Clear();
    }

    size_t Size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return data_.size();
    }

    size_t UsedMemory() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return used_memory_;
    }

    CacheStats Stats() const {
        std::lock_guard<std::mutex> lock(mutex_);
        CacheStats res = stats_;
        res.used_memory = used_memory_;
        res.size = data_.size();
        return res;
    }
};

inline std::expected<std::unique_ptr<Cache>, std::string> MakeCache(CacheConfig config) {
    auto policy = MakePolicy(config.policy);

    if (!policy.has_value()) { return std::unexpected(policy.error()); }

    return std::make_unique<Cache>(std::move(config), std::move(policy.value()));
}
