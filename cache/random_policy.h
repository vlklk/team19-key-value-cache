#pragma once

#include <optional>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#include "eviction_policy.h"

class RandomPolicy : public EvictionPolicy {
public:
    void OnInsert(const std::string& key) override {
        if (position_.contains(key)) {
            return;
        }

        position_[key] = keys_.size();
        keys_.push_back(key);
    }

    void OnAccess(const std::string&) override {
    }

    void OnErase(const std::string& key) override {
        auto it = position_.find(key);

        if (it == position_.end()) {
            return;
        }

        size_t index = it->second;
        std::string last = keys_.back();

        keys_[index] = last;
        position_[last] = index;

        keys_.pop_back();
        position_.erase(it);
    }

    std::optional<std::string> Victim() override {
        if (keys_.empty()) {
            return std::nullopt;
        }

        std::uniform_int_distribution<size_t> dist(0, keys_.size() - 1);
        return keys_[dist(gen_)];
    }

    void Clear() override {
        keys_.clear();
        position_.clear();
    }

private:
    std::vector<std::string> keys_;
    std::unordered_map<std::string, size_t> position_;
    std::mt19937 gen_{std::random_device{}()};
};
