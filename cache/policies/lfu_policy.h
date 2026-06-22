#pragma once

#include <cstddef>
#include <list>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>

#include "eviction_policy.h"

class LfuPolicy : public EvictionPolicy {
public:
    void OnInsert(const std::string& key) override {
        OnErase(key);

        auto& bucket = freqs_[1];
        bucket.push_front(key);
        nodes_[key] = Node{1, bucket.begin()};
    }

    void OnAccess(const std::string& key) override {
        auto it = nodes_.find(key);

        if (it == nodes_.end()) { return; }

        size_t freq = it->second.freq;
        auto bucket_it = freqs_.find(freq);

        bucket_it->second.erase(it->second.pos);

        if (bucket_it->second.empty()) { freqs_.erase(bucket_it); }

        auto& next = freqs_[freq + 1];
        next.push_front(key);

        it->second.freq = freq + 1;
        it->second.pos = next.begin();
    }

    void OnErase(const std::string& key) override {
        auto it = nodes_.find(key);

        if (it == nodes_.end()) { return; }

        auto bucket_it = freqs_.find(it->second.freq);
        bucket_it->second.erase(it->second.pos);

        if (bucket_it->second.empty()) { freqs_.erase(bucket_it); }

        nodes_.erase(it);
    }

    std::optional<std::string> Victim() override {
        if (freqs_.empty()) { return std::nullopt; }

        return freqs_.begin()->second.back();
    }

    void Clear() override {
        freqs_.clear();
        nodes_.clear();
    }

private:
    struct Node {
        size_t freq = 0;
        std::list<std::string>::iterator pos;
    };

    std::map<size_t, std::list<std::string>> freqs_;
    std::unordered_map<std::string, Node> nodes_;
};
