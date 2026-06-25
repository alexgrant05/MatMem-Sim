#pragma once

#include <cstdint>

struct Metrics {
    std::uint64_t total_cycles = 0;
    std::uint64_t compute_cycles = 0;
    std::uint64_t dram_stall_cycles = 0;
    std::uint64_t dram_bytes = 0;
    std::uint64_t operations = 0;
    std::uint64_t a_load_bytes = 0;
    std::uint64_t b_load_bytes = 0;
    std::uint64_t c_load_bytes = 0;
    std::uint64_t a_demand_bytes = 0;
    std::uint64_t b_demand_bytes = 0;
    std::uint64_t c_demand_bytes = 0;

    double compute_utilization() const {
        if (total_cycles == 0) {
            return 0.0;
        }
        return static_cast<double>(compute_cycles) / static_cast<double>(total_cycles);
    }

    double arithmetic_intensity() const {
        if (dram_bytes == 0) {
            return 0.0;
        }
        return static_cast<double>(operations) / static_cast<double>(dram_bytes);
    }

    double effective_ops_per_cycle() const {
        if (total_cycles == 0) {
            return 0.0;
        }
        return static_cast<double>(operations) / static_cast<double>(total_cycles);
    }

    double a_reuse_factor() const {
        if (a_load_bytes == 0) {
            return 0.0;
        }
        return static_cast<double>(a_demand_bytes) / static_cast<double>(a_load_bytes);
    }

    double b_reuse_factor() const {
        if (b_load_bytes == 0) {
            return 0.0;
        }
        return static_cast<double>(b_demand_bytes) / static_cast<double>(b_load_bytes);
    }

    double c_reuse_factor() const {
        if (c_load_bytes == 0) {
            return 0.0;
        }
        return static_cast<double>(c_demand_bytes) / static_cast<double>(c_load_bytes);
    }
};
