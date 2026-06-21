#pragma once

#include <cctype>
#include <expected>
#include <memory>
#include <string>

#include "eviction_policy.h"
#include "lfu_policy.h"
#include "lru_policy.h"
#include "noeviction_policy.h"
#include "random_policy.h"

std::string ToLower(std::string value) {
    for (char& c : value) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    return value;
}

std::expected<std::unique_ptr<EvictionPolicy>, std::string> MakePolicy(const std::string& name) {
    std::string policy = ToLower(name);

    if (policy == "lru") {
        return std::make_unique<LruPolicy>();
    }

    if (policy == "lfu") {
        return std::make_unique<LfuPolicy>();
    }

    if (policy == "random") {
        return std::make_unique<RandomPolicy>();
    }

    if (policy == "noeviction") {
        return std::make_unique<NoEvictionPolicy>();
    }

    return std::unexpected("unknown eviction policy");
}
