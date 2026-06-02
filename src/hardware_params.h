#pragma once

#include <cstdint>

struct HardwareParams {
    std::uint64_t dram_latency_cycles = 100;
    std::uint64_t dram_bandwidth_bytes_per_cycle = 32;
    std::uint64_t scratchpad_bytes = 32 * 1024;
    std::uint64_t scratchpad_latency_cycles = 1;
    std::uint64_t compute_ops_per_cycle = 256;
    std::uint64_t matrix_m = 256;
    std::uint64_t matrix_n = 256;
    std::uint64_t matrix_k = 256;
    std::uint64_t element_bytes = 4;
    std::uint64_t tile_m = 0;  // 0 = auto-size to scratchpad capacity
    std::uint64_t tile_n = 0;
    std::uint64_t tile_k = 0;

    // Energy constants (pJ). Defaults from Horowitz ISSCC 2014 / typical accelerator values.
    double energy_dram_pj_per_byte   = 200.0;  // DDR4 off-chip access
    double energy_sram_pj_per_access = 5.0;    // on-chip SRAM read or write, per element
    double energy_mac_pj             = 3.7;    // FP32 multiply-accumulate
};
