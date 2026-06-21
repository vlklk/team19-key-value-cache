#include "cache/cache.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <memory>
#include <thread>

namespace {

std::unique_ptr<Cache> MakeOrDie(CacheConfig config) {
    auto result = MakeCache(std::move(config));
    EXPECT_TRUE(result.has_value());
    return std::move(result.value());
}

CacheConfig Config(PolicyType policy) {
    CacheConfig config;
    config.policy = policy;
    return config;
}

} // namespace

TEST(SingleThread, PutThenGetReturnsValue) {
    auto cache = MakeOrDie(Config(PolicyType::Lru));
    ASSERT_TRUE(cache->Put("key", "value").has_value());

    auto value = cache->Get("key");
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(*value, "value");
}

TEST(SingleThread, GetMissingReturnsNullopt) {
    auto cache = MakeOrDie(Config(PolicyType::Lru));
    EXPECT_FALSE(cache->Get("absent").has_value());
    EXPECT_EQ(cache->Stats().misses, 1u);
}

TEST(SingleThread, EmptyKeyIsRejected) {
    auto cache = MakeOrDie(Config(PolicyType::Lru));
    EXPECT_FALSE(cache->Put("", "value").has_value());
    EXPECT_EQ(cache->Size(), 0u);
}

TEST(SingleThread, UpdatingKeyKeepsSingleEntry) {
    auto cache = MakeOrDie(Config(PolicyType::Lru));
    ASSERT_TRUE(cache->Put("k", "a").has_value());
    ASSERT_TRUE(cache->Put("k", "bb").has_value());

    EXPECT_EQ(cache->Size(), 1u);
    EXPECT_EQ(*cache->Get("k"), "bb");
    EXPECT_EQ(cache->UsedMemory(), 1u + 2u + sizeof(CacheEntry));
}

TEST(SingleThread, EraseRemovesEntry) {
    auto cache = MakeOrDie(Config(PolicyType::Lru));
    ASSERT_TRUE(cache->Put("k", "v").has_value());

    EXPECT_TRUE(cache->Erase("k"));
    EXPECT_FALSE(cache->Erase("k"));
    EXPECT_EQ(cache->Size(), 0u);
    EXPECT_EQ(cache->Stats().erases, 1u);
}

TEST(SingleThread, ClearEmptiesCache) {
    auto cache = MakeOrDie(Config(PolicyType::Lru));
    ASSERT_TRUE(cache->Put("a", "1").has_value());
    ASSERT_TRUE(cache->Put("b", "2").has_value());

    cache->Clear();
    EXPECT_EQ(cache->Size(), 0u);
    EXPECT_EQ(cache->UsedMemory(), 0u);
}

TEST(SingleThread, LruEvictsLeastRecentlyUsed) {
    CacheConfig config = Config(PolicyType::Lru);
    config.max_count = 2;
    auto cache = MakeOrDie(config);

    ASSERT_TRUE(cache->Put("a", "1").has_value());
    ASSERT_TRUE(cache->Put("b", "2").has_value());
    ASSERT_TRUE(cache->Get("a").has_value());
    ASSERT_TRUE(cache->Put("c", "3").has_value());

    EXPECT_TRUE(cache->Get("a").has_value());
    EXPECT_FALSE(cache->Get("b").has_value());
    EXPECT_TRUE(cache->Get("c").has_value());
    EXPECT_EQ(cache->Stats().evictions, 1u);
}

TEST(SingleThread, LfuEvictsLeastFrequentlyUsed) {
    CacheConfig config = Config(PolicyType::Lfu);
    config.max_count = 2;
    auto cache = MakeOrDie(config);

    ASSERT_TRUE(cache->Put("a", "1").has_value());
    ASSERT_TRUE(cache->Put("b", "2").has_value());
    cache->Get("a");
    cache->Get("a");
    cache->Get("b");
    ASSERT_TRUE(cache->Put("c", "3").has_value());

    EXPECT_TRUE(cache->Get("a").has_value());
    EXPECT_FALSE(cache->Get("b").has_value());
    EXPECT_TRUE(cache->Get("c").has_value());
}

TEST(SingleThread, RandomEvictsWhenFull) {
    CacheConfig config = Config(PolicyType::Random);
    config.max_count = 2;
    auto cache = MakeOrDie(config);

    ASSERT_TRUE(cache->Put("a", "1").has_value());
    ASSERT_TRUE(cache->Put("b", "2").has_value());
    ASSERT_TRUE(cache->Put("c", "3").has_value());

    EXPECT_EQ(cache->Size(), 2u);
    EXPECT_EQ(cache->Stats().evictions, 1u);
}

TEST(SingleThread, NoEvictionRejectsWhenFull) {
    CacheConfig config = Config(PolicyType::NoEviction);
    config.max_count = 1;
    auto cache = MakeOrDie(config);

    ASSERT_TRUE(cache->Put("a", "1").has_value());
    EXPECT_FALSE(cache->Put("b", "2").has_value());
    EXPECT_TRUE(cache->Get("a").has_value());
    EXPECT_EQ(cache->Size(), 1u);
}

TEST(SingleThread, ValueLargerThanMaxMemoryIsRejected) {
    CacheConfig config = Config(PolicyType::Lru);
    config.max_memory = sizeof(CacheEntry) + 4;
    auto cache = MakeOrDie(config);

    EXPECT_FALSE(cache->Put("longkey", "longvalue").has_value());
    EXPECT_EQ(cache->Size(), 0u);
}

TEST(SingleThread, NonPositiveTtlIsRejected) {
    auto cache = MakeOrDie(Config(PolicyType::Lru));
    EXPECT_FALSE(cache->Put("k", "v", std::chrono::seconds(0)).has_value());
    EXPECT_FALSE(cache->Put("k", "v", std::chrono::seconds(-1)).has_value());
}

TEST(SingleThread, EntryExpiresAfterTtl) {
    auto cache = MakeOrDie(Config(PolicyType::Lru));
    ASSERT_TRUE(cache->Put("k", "v", std::chrono::seconds(1)).has_value());
    EXPECT_TRUE(cache->Get("k").has_value());

    std::this_thread::sleep_for(std::chrono::milliseconds(1100));

    EXPECT_FALSE(cache->Get("k").has_value());
    EXPECT_GE(cache->Stats().expired, 1u);
}

TEST(SingleThread, StatsCountHitsMissesAndRatio) {
    auto cache = MakeOrDie(Config(PolicyType::Lru));
    ASSERT_TRUE(cache->Put("k", "v").has_value());
    cache->Get("k");
    cache->Get("absent");

    auto stats = cache->Stats();
    EXPECT_EQ(stats.hits, 1u);
    EXPECT_EQ(stats.misses, 1u);
    EXPECT_EQ(stats.puts, 1u);
    EXPECT_DOUBLE_EQ(stats.HitRatio(), 0.5);
}

TEST(SingleThread, MakeCacheSupportsEveryPolicy) {
    for (PolicyType policy :
         {PolicyType::Lru, PolicyType::Lfu, PolicyType::Random, PolicyType::NoEviction}) {
        auto cache = MakeOrDie(Config(policy));
        EXPECT_TRUE(cache->Put("k", "v").has_value());
        EXPECT_TRUE(cache->Get("k").has_value());
    }
}
