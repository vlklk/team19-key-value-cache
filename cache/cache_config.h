#pragma once

#include <chrono>
#include <cstddef>
#include <optional>
#include <string>

class CacheConfig {
public:
    size_t max_memory = 0;
    size_t max_count = 0;
    std::optional<std::chrono::seconds> ttl = std::nullopt;
    std::string policy = "lru";
};
