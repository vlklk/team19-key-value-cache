#pragma once

#include <expected>
#include <memory>
#include <string>

#include "eviction_policy.h"
#include "lfu_policy.h"
#include "lru_policy.h"
#include "noeviction_policy.h"
#include "random_policy.h"

enum class PolicyType {
    Lru,
    Lfu,
    Random,
    NoEviction,
};

inline std::expected<std::unique_ptr<EvictionPolicy>, std::string> MakePolicy(PolicyType type) {
    switch (type) {
        case PolicyType::Lru:
            return std::make_unique<LruPolicy>();
        case PolicyType::Lfu:
            return std::make_unique<LfuPolicy>();
        case PolicyType::Random:
            return std::make_unique<RandomPolicy>();
        case PolicyType::NoEviction:
            return std::make_unique<NoEvictionPolicy>();
    }

    return std::unexpected("unknown eviction policy");
}
