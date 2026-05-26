#pragma once

#include <cstdint>

struct Metrics {
    std::uint64_t total_cycles = 0;
    std::uint64_t compute_cycles = 0;
    std::uint64_t dram_stall_cycles = 0;
    std::uint64_t dram_bytes = 0;
    std::uint64_t operations = 0;

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

    double effective_gops() const {
        if (total_cycles == 0) {
            return 0.0;
        }
        return static_cast<double>(operations) / static_cast<double>(total_cycles) / 1.0e9;
    }
};
