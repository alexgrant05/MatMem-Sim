#pragma once

#include "hardware_params.h"
#include "metrics.h"
#include "tiling_engine.h"

#include <string>
#include <vector>

Metrics run_simulation(const HardwareParams& params, const std::string& strategy_name,
                       std::vector<TraceRecord>* trace_out = nullptr);
