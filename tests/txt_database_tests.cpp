#include "database/txt_database.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>


namespace {

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


TEST(TxtDatabase, GetExistingKeyReturnsValue) {
    auto path = MakeTempFile("txt_database_get_existing.txt");
    WriteFile(path, "apple=red\nbanana=yellow\n");

    TxtDatabase database(path);

    auto value = database.Get("apple");

    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(*value, "red");

    std::filesystem::remove(path);
}

TEST(TxtDatabase, GetMissingKeyReturnsNullopt) {
    auto path = MakeTempFile("txt_database_get_missing.txt");
    WriteFile(path, "apple=red\n");

    TxtDatabase database(path);

    auto value = database.Get("unknown");

    EXPECT_FALSE(value.has_value());

    std::filesystem::remove(path);
}

TEST(TxtDatabase, SetCreatesValue) {
    auto path = MakeTempFile("txt_database_set_create.txt");

    TxtDatabase database(path);

    auto result = database.Set("language", "cpp");

    ASSERT_TRUE(result.has_value());

    auto value = database.Get("language");

    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(*value, "cpp");
    EXPECT_EQ(database.Size(), 1u);

    std::filesystem::remove(path);
}

TEST(TxtDatabase, SetUpdatesValue) {
    auto path = MakeTempFile("txt_database_set_update.txt");

    TxtDatabase database(path);

    ASSERT_TRUE(database.Set("language", "cpp").has_value());
    ASSERT_TRUE(database.Set("language", "cpp23").has_value());

    auto value = database.Get("language");

    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(*value, "cpp23");
    EXPECT_EQ(database.Size(), 1u);

    std::filesystem::remove(path);
}

TEST(TxtDatabase, EraseExistingKeyRemovesValue) {
    auto path = MakeTempFile("txt_database_erase_existing.txt");
    WriteFile(path, "cat=meow\ndog=woof\n");

    TxtDatabase database(path);

    EXPECT_TRUE(database.Erase("cat"));
    EXPECT_FALSE(database.Get("cat").has_value());
    EXPECT_TRUE(database.Get("dog").has_value());
    EXPECT_EQ(database.Size(), 1u);

    std::filesystem::remove(path);
}

TEST(TxtDatabase, EraseMissingKeyReturnsFalse) {
    auto path = MakeTempFile("txt_database_erase_missing.txt");
    WriteFile(path, "cat=meow\n");

    TxtDatabase database(path);

    EXPECT_FALSE(database.Erase("dog"));
    EXPECT_EQ(database.Size(), 1u);

    std::filesystem::remove(path);
}

TEST(TxtDatabase, InvalidKeyIsRejected) {
    auto path = MakeTempFile("txt_database_invalid_key.txt");

    TxtDatabase database(path);

    EXPECT_FALSE(database.Set("", "value").has_value());
    EXPECT_FALSE(database.Set("a=b", "value").has_value());
    EXPECT_FALSE(database.Set("a\nb", "value").has_value());

    EXPECT_EQ(database.Size(), 0u);

    std::filesystem::remove(path);
}

TEST(TxtDatabase, InvalidValueIsRejected) {
    auto path = MakeTempFile("txt_database_invalid_value.txt");

    TxtDatabase database(path);

    EXPECT_FALSE(database.Set("key", "hello\nworld").has_value());
    EXPECT_EQ(database.Size(), 0u);

    std::filesystem::remove(path);
}

TEST(TxtDatabase, CommentsAndInvalidLinesAreIgnored) {
    auto path = MakeTempFile("txt_database_comments.txt");

    WriteFile(path,
              "# comment\n"
              "apple=red\n"
              "invalid_line\n"
              "banana=yellow\n");

    TxtDatabase database(path);

    EXPECT_EQ(database.Size(), 2u);
    EXPECT_TRUE(database.Get("apple").has_value());
    EXPECT_TRUE(database.Get("banana").has_value());
    EXPECT_FALSE(database.Get("invalid_line").has_value());

    std::filesystem::remove(path);
}
