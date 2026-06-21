#pragma once

#include <list>
#include <optional>
#include <string>
#include <unordered_map>

#include "eviction_policy.h"

class LruPolicy : public EvictionPolicy {
public:
    void OnInsert(const std::string& key) override {
        OnErase(key);
        order_.push_front(key);
        position_[key] = order_.begin();
    }

    void OnAccess(const std::string& key) override {
        auto it = position_.find(key);

        if (it == position_.end()) {
            return;
        }

        order_.erase(it->second);
        order_.push_front(key);
        position_[key] = order_.begin();
    }

    void OnErase(const std::string& key) override {
        auto it = position_.find(key);

        if (it == position_.end()) {
            return;
        }

        order_.erase(it->second);
        position_.erase(it);
    }

    std::optional<std::string> Victim() override {
        if (order_.empty()) {
            return std::nullopt;
        }

        return order_.back();
    }

    void Clear() override {
        order_.clear();
        position_.clear();
    }

private:
    std::list<std::string> order_;
    std::unordered_map<std::string, std::list<std::string>::iterator> position_;
};
