#pragma once

#include <optional>
#include <string>


class EvictionPolicy {
public:
    virtual ~EvictionPolicy() = default;

    virtual void OnInsert(const std::string& key) = 0;
    virtual void OnAccess(const std::string& key) = 0;
    virtual void OnErase(const std::string& key) = 0;

    virtual std::optional<std::string> Victim() = 0;
    virtual void Clear() = 0;
};
