#pragma once
#include <vector>
#include <string>
#include "benchmark_config.h"

enum class OperationType {
    Get,
    Put,
};

struct Operation {
    OperationType type = OperationType::Get;
    std::string key;
};

std::vector<Operation> GenerateWorkload(const BenchmarkScenario& scenario);
