#pragma once

#include "hardware_params.h"
#include "metrics.h"

struct EnergyResult {
    double energy_pj = 0.0;
    double ops_per_pj = 0.0;
};

EnergyResult compute_energy(const HardwareParams& params, const Metrics& metrics);
