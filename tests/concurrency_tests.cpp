#include "cache/cache.h"

#include <gtest/gtest.h>

#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

std::unique_ptr<Cache> Make(PolicyType policy, size_t max_count = 0, size_t max_memory = 0) {
    CacheConfig config;
    config.policy = policy;
    config.max_count = max_count;
    config.max_memory = max_memory;

    auto result = MakeCache(std::move(config));
    EXPECT_TRUE(result.has_value());
    return std::move(result.value());
}

void RunOnThreads(int thread_count, const std::function<void(int)>& body) {
    std::vector<std::thread> threads;
    threads.reserve(thread_count);
    for (int t = 0; t < thread_count; t++) { threads.emplace_back(body, t); }
    for (auto& thread : threads) { thread.join(); }
}

std::string Key(int thread_id, int index) {
    return "t" + std::to_string(thread_id) + "_k" + std::to_string(index);
}

std::string Value(int thread_id, int index) {
    return "v" + std::to_string(thread_id) + "_" + std::to_string(index);
}

} // namespace

TEST(Concurrency, DistinctKeysKeepSizeAndMemoryConsistent) {
    constexpr int kThreads = 8;
    constexpr int kPerThread = 2000;
    auto cache = Make(PolicyType::Lru);

    RunOnThreads(kThreads, [&](int t) {
        for (int i = 0; i < kPerThread; i++) { cache->Put(Key(t, i), Value(t, i)); }
    });

    size_t expected_memory = 0;
    for (int t = 0; t < kThreads; t++) {
        for (int i = 0; i < kPerThread; i++) {
            expected_memory += Key(t, i).size() + Value(t, i).size() + sizeof(CacheEntry);
        }
    }

    EXPECT_EQ(cache->Size(), static_cast<size_t>(kThreads) * kPerThread);
    EXPECT_EQ(cache->UsedMemory(), expected_memory);

    for (int t = 0; t < kThreads; t++) {
        for (int i = 0; i < kPerThread; i++) {
            auto value = cache->Get(Key(t, i));
            ASSERT_TRUE(value.has_value()) << "lost key " << Key(t, i);
            EXPECT_EQ(*value, Value(t, i));
        }
    }
}

TEST(Concurrency, SameKeyConvergesToSingleEntry) {
    constexpr int kThreads = 8;
    constexpr int kRepeats = 5000;
    const std::string key = "shared";
    const std::string value = "payload";
    auto cache = Make(PolicyType::Lru);

    RunOnThreads(kThreads, [&](int) {
        for (int i = 0; i < kRepeats; i++) { cache->Put(key, value); }
    });

    EXPECT_EQ(cache->Size(), 1u);
    EXPECT_EQ(cache->UsedMemory(), key.size() + value.size() + sizeof(CacheEntry));

    auto stored = cache->Get(key);
    ASSERT_TRUE(stored.has_value());
    EXPECT_EQ(*stored, value);
}

TEST(Concurrency, ReadersAndWritersStayWithinCapacity) {
    constexpr int kWriters = 4;
    constexpr int kReaders = 4;
    constexpr int kOps = 5000;
    constexpr size_t kKeySpace = 512;
    constexpr size_t kMaxCount = 256;
    auto cache = Make(PolicyType::Lru, kMaxCount);

    std::atomic<bool> start{false};
    std::vector<std::thread> threads;

    for (int w = 0; w < kWriters; w++) {
        threads.emplace_back([&, w] {
            while (!start.load(std::memory_order_acquire)) {}
            for (int i = 0; i < kOps; i++) { cache->Put(Key(w, i % kKeySpace), Value(w, i)); }
        });
    }
    for (int r = 0; r < kReaders; r++) {
        threads.emplace_back([&, r] {
            while (!start.load(std::memory_order_acquire)) {}
            for (int i = 0; i < kOps; i++) { cache->Get(Key(r % kWriters, i % kKeySpace)); }
        });
    }

    start.store(true, std::memory_order_release);
    for (auto& thread : threads) { thread.join(); }

    EXPECT_LE(cache->Size(), kMaxCount);
}

TEST(Concurrency, EvictionKeepsSizeWithinLimit) {
    constexpr int kThreads = 8;
    constexpr int kPerThread = 3000;
    constexpr size_t kMaxCount = 100;
    auto cache = Make(PolicyType::Lru, kMaxCount);

    RunOnThreads(kThreads, [&](int t) {
        for (int i = 0; i < kPerThread; i++) { cache->Put(Key(t, i), Value(t, i)); }
    });

    EXPECT_LE(cache->Size(), kMaxCount);
}

TEST(Concurrency, InterleavedPutAndErase) {
    constexpr int kThreads = 8;
    constexpr int kOps = 4000;
    constexpr int kKeysPerThread = 100;
    auto cache = Make(PolicyType::Lru);

    RunOnThreads(kThreads, [&](int t) {
        for (int i = 0; i < kOps; i++) {
            const int slot = i % kKeysPerThread;
            cache->Put(Key(t, slot), Value(t, i));
            if (i % 2 == 0) { cache->Erase(Key(t, slot)); }
        }
    });

    EXPECT_LE(cache->Size(), static_cast<size_t>(kThreads) * kKeysPerThread);
}
