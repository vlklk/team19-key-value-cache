#pragma once

#include <optional>
#include <string>

#include "eviction_policy.h"

class NoEvictionPolicy : public EvictionPolicy {
public:
    void OnInsert(const std::string&) override {}

    void OnAccess(const std::string&) override {}

    void OnErase(const std::string&) override {}

    std::optional<std::string> Victim() override { return std::nullopt; }

    void Clear() override {}
};
