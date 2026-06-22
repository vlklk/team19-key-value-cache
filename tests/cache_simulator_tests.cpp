#include "simulator/cache_request_simulator.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <utility>


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

std::filesystem::path MakeTempFile(const std::string& name) {
    auto path = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove(path);
    return path;
}

void WriteFile(const std::filesystem::path& path, const std::string& content) {
    std::ofstream output(path, std::ios::trunc);
    output << content;
}

} // namespace


TEST(CacheRequestSimulator, FirstGetReadsFromTxtDatabaseAfterCacheMiss) {
    auto path = MakeTempFile("cache_simulator_first_get.txt");
    WriteFile(path, "cat=meow\n");

    auto cache = MakeOrDie(Config(PolicyType::Lru));
    TxtDatabase database(path);
    CacheRequestSimulator simulator(*cache, database);

    auto value = simulator.Get("cat");

    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(*value, "meow");

    auto stats = simulator.Stats();

    EXPECT_EQ(stats.cache_hits, 0u);
    EXPECT_EQ(stats.cache_misses, 1u);
    EXPECT_EQ(stats.database_hits, 1u);
    EXPECT_EQ(stats.database_misses, 0u);

    EXPECT_EQ(cache->Size(), 1u);

    std::filesystem::remove(path);
}

TEST(CacheRequestSimulator, SecondGetReadsFromCache) {
    auto path = MakeTempFile("cache_simulator_second_get.txt");
    WriteFile(path, "dog=woof\n");

    auto cache = MakeOrDie(Config(PolicyType::Lru));
    TxtDatabase database(path);
    CacheRequestSimulator simulator(*cache, database);

    auto first = simulator.Get("dog");
    auto second = simulator.Get("dog");

    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());

    EXPECT_EQ(*first, "woof");
    EXPECT_EQ(*second, "woof");

    auto stats = simulator.Stats();

    EXPECT_EQ(stats.cache_hits, 1u);
    EXPECT_EQ(stats.cache_misses, 1u);
    EXPECT_EQ(stats.database_hits, 1u);
    EXPECT_EQ(stats.database_misses, 0u);
    EXPECT_DOUBLE_EQ(stats.HitRatio(), 0.5);

    std::filesystem::remove(path);
}

TEST(CacheRequestSimulator, MissingKeyReturnsNullopt) {
    auto path = MakeTempFile("cache_simulator_missing_key.txt");
    WriteFile(path, "apple=red\n");

    auto cache = MakeOrDie(Config(PolicyType::Lru));
    TxtDatabase database(path);
    CacheRequestSimulator simulator(*cache, database);

    auto value = simulator.Get("unknown");

    EXPECT_FALSE(value.has_value());

    auto stats = simulator.Stats();

    EXPECT_EQ(stats.cache_hits, 0u);
    EXPECT_EQ(stats.cache_misses, 1u);
    EXPECT_EQ(stats.database_hits, 0u);
    EXPECT_EQ(stats.database_misses, 1u);

    std::filesystem::remove(path);
}

TEST(CacheRequestSimulator, SetWritesToTxtDatabaseAndCache) {
    auto path = MakeTempFile("cache_simulator_set.txt");

    auto cache = MakeOrDie(Config(PolicyType::Lru));
    TxtDatabase database(path);
    CacheRequestSimulator simulator(*cache, database);

    auto result = simulator.Set("language", "cpp");

    ASSERT_TRUE(result.has_value());

    auto database_value = database.Get("language");
    auto cache_value = cache->Get("language");

    ASSERT_TRUE(database_value.has_value());
    ASSERT_TRUE(cache_value.has_value());

    EXPECT_EQ(*database_value, "cpp");
    EXPECT_EQ(*cache_value, "cpp");
    EXPECT_EQ(simulator.Stats().writes, 1u);

    std::filesystem::remove(path);
}

TEST(CacheRequestSimulator, EraseRemovesFromTxtDatabaseAndCache) {
    auto path = MakeTempFile("cache_simulator_erase.txt");
    WriteFile(path, "apple=red\n");

    auto cache = MakeOrDie(Config(PolicyType::Lru));
    TxtDatabase database(path);
    CacheRequestSimulator simulator(*cache, database);

    ASSERT_TRUE(simulator.Get("apple").has_value());

    EXPECT_TRUE(simulator.Erase("apple"));

    EXPECT_FALSE(database.Get("apple").has_value());
    EXPECT_FALSE(cache->Get("apple").has_value());
    EXPECT_EQ(simulator.Stats().erases, 1u);

    std::filesystem::remove(path);
}
