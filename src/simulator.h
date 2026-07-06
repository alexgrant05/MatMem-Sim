#pragma once

#include "hardware_params.h"
#include "metrics.h"
#include "tiling_engine.h"

#include <string>
#include <stdexcept>
#include <vector>

class SimulationLimitError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

Metrics run_simulation(const HardwareParams& params, const std::string& strategy_name,
                       std::vector<TraceRecord>* trace_out = nullptr);
