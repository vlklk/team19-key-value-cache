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

    explicit TxtDatabase(std::filesystem::path path) : data_(LoadFromFile(path)) {}

    std::optional<std::string> Get(const std::string& key) const {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = data_.find(key);

        if (it == data_.end()) { return std::nullopt; }

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

        data_[key] = value;

        return {};
    }

    bool Erase(const std::string& key) {
        std::lock_guard<std::mutex> lock(mutex_);

        return data_.erase(key) != 0;
    }

    size_t Size() const {
        std::lock_guard<std::mutex> lock(mutex_);

        return data_.size();
    }

private:
    static std::unordered_map<std::string, std::string> LoadFromFile(
        const std::filesystem::path& path) {
        std::unordered_map<std::string, std::string> data;
        std::ifstream input(path);

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

    std::unordered_map<std::string, std::string> data_;
    mutable std::mutex mutex_;
};
