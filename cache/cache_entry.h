#pragma once

#include <chrono>
#include <cstddef>
#include <optional>
#include <string>

class CacheEntry {
public:
    std::string value;
    std::optional<std::chrono::steady_clock::time_point> ends = std::nullopt;
    size_t memory = 0;
};
