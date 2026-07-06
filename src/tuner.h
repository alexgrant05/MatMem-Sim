#pragma once

#include "energy_model.h"
#include "hardware_params.h"
#include "metrics.h"

#include <cstdint>
#include <string>

enum class TuneObjective {
    Cycles,
    Energy,
    DramBytes,
};

struct TuneOptions {
    TuneObjective objective = TuneObjective::Cycles;
    std::uint64_t max_evaluations = 256;
};

struct TuneStats {
    std::uint64_t candidates_considered = 0;
    std::uint64_t candidates_evaluated = 0;
    std::uint64_t candidates_rejected = 0;
};

struct TuneResult {
    std::string strategy;
    HardwareParams params;
    Metrics metrics;
    EnergyResult energy;
    TuneObjective objective = TuneObjective::Cycles;
    TuneStats stats;
};

const char* tune_objective_name(TuneObjective objective);
TuneObjective parse_tune_objective(const std::string& name);
bool tune_result_better(const TuneResult& lhs, const TuneResult& rhs);
TuneResult tune_configuration(const HardwareParams& params,
                              const TuneOptions& options = {});
