#pragma once

#include "hardware_params.h"
#include "metrics.h"

#include <string>

Metrics run_simulation(const HardwareParams& params, const std::string& strategy_name);
