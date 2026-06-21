#pragma once

#include <cstddef>

class CacheStats {
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

        if (count == 0) {
            return 0.0;
        }

        return static_cast<double>(hits) / static_cast<double>(count);
    }
};
