#include "energy_model.h"

EnergyResult compute_energy(const HardwareParams& p, const Metrics& m) {
    const double ops = static_cast<double>(m.operations);
    const double element_bytes = static_cast<double>(p.element_bytes);
    const double sram_accesses = element_bytes > 0.0
        ? static_cast<double>(m.dram_bytes) / element_bytes + ops
        : ops;
    const double energy = static_cast<double>(m.dram_bytes) * p.energy_dram_pj_per_byte
                        + sram_accesses * p.energy_sram_pj_per_access
                        + (ops / 2.0) * p.energy_mac_pj;
    return {energy, energy > 0.0 ? ops / energy : 0.0};
}
