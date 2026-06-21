#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>

#include "eviction_policy.h"

class LfuInfo {
public:
    size_t count = 0;
    size_t time = 0;
};

class LfuPolicy : public EvictionPolicy {
public:
    void OnInsert(const std::string& key) override {
        ++time_;
        LfuInfo info;
        info.count = 1;
        info.time = time_;
        data_[key] = info;
    }

    void OnAccess(const std::string& key) override {
        auto it = data_.find(key);

        if (it == data_.end()) { return; }

        ++time_;
        ++it->second.count;
        it->second.time = time_;
    }

    void OnErase(const std::string& key) override { data_.erase(key); }

    std::optional<std::string> Victim() override {
        if (data_.empty()) { return std::nullopt; }

        auto best = data_.begin();

        for (auto it = data_.begin(); it != data_.end(); ++it) {
            if (it->second.count < best->second.count) {
                best = it;
            } else if (it->second.count == best->second.count &&
                       it->second.time < best->second.time) {
                best = it;
            }
        }

        return best->first;
    }

    void Clear() override {
        data_.clear();
        time_ = 0;
    }

private:
    std::unordered_map<std::string, LfuInfo> data_;
    size_t time_ = 0;
};
