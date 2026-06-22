#pragma once

#include <cstddef>
#include <expected>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>


class TxtDatabase {
public:
    explicit TxtDatabase(std::filesystem::path path) : path_(std::move(path)) {}

    std::optional<std::string> Get(const std::string& key) const {
        std::lock_guard<std::mutex> lock(mutex_);

        auto data = LoadAll();
        auto it = data.find(key);

        if (it == data.end()) { return std::nullopt; }

        return it->second;
    }

    std::expected<void, std::string> Set(const std::string& key, const std::string& value) {
        std::lock_guard<std::mutex> lock(mutex_);

        if (key.empty()) { return std::unexpected("empty key"); }

        if (key.find('=') != std::string::npos || key.find('\n') != std::string::npos) {
            return std::unexpected("invalid key");
        }

        if (value.find('\n') != std::string::npos) {
            return std::unexpected("invalid value");
        }

        auto data = LoadAll();
        data[key] = value;

        return SaveAll(data);
    }

    bool Erase(const std::string& key) {
        std::lock_guard<std::mutex> lock(mutex_);

        auto data = LoadAll();

        if (data.erase(key) == 0) { return false; }

        auto result = SaveAll(data);

        return result.has_value();
    }

    size_t Size() const {
        std::lock_guard<std::mutex> lock(mutex_);

        return LoadAll().size();
    }

private:
    std::unordered_map<std::string, std::string> LoadAll() const {
        std::unordered_map<std::string, std::string> data;
        std::ifstream input(path_);

        if (!input.is_open()) { return data; }

        std::string line;

        while (std::getline(input, line)) {
            if (line.empty() || line[0] == '#') { continue; }

            size_t delimiter = line.find('=');

            if (delimiter == std::string::npos) { continue; }

            std::string key = line.substr(0, delimiter);
            std::string value = line.substr(delimiter + 1);

            if (!key.empty()) { data[key] = value; }
        }

        return data;
    }

    std::expected<void, std::string> SaveAll(
        const std::unordered_map<std::string, std::string>& data) const {
        auto parent = path_.parent_path();

        if (!parent.empty()) {
            std::error_code error;
            std::filesystem::create_directories(parent, error);

            if (error) { return std::unexpected(error.message()); }
        }

        std::ofstream output(path_, std::ios::trunc);

        if (!output.is_open()) {
            return std::unexpected("cannot open file");
        }

        for (const auto& [key, value] : data) {
            output << key << '=' << value << '\n';
        }

        if (!output.good()) {
            return std::unexpected("cannot write file");
        }

        return {};
    }

    std::filesystem::path path_;
    mutable std::mutex mutex_;
};
