#include "hardware_params.h"
#include "simulator.h"

#include <cstdlib>
#include <stdexcept>
#include <iomanip>
#include <iostream>
#include <string>

namespace {

std::string value_after(int& i, int argc, char** argv) {
    if (i + 1 >= argc) {
        throw std::invalid_argument(std::string("missing value for ") + argv[i]);
    }
    return argv[++i];
}

void print_usage() {
    std::cout << "usage: matmem-sim [--strategy row_stationary|output_stationary|double_buffer]"
              << " [--scratchpad-kb N] [--dram-latency cycles] [--csv]\n";
}

} // namespace

int main(int argc, char** argv) {
    HardwareParams params;
    std::string strategy = "row_stationary";
    bool csv = false;

    try {
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--strategy") {
                strategy = value_after(i, argc, argv);
            } else if (arg == "--scratchpad-kb") {
                params.scratchpad_bytes = std::stoull(value_after(i, argc, argv)) * 1024;
            } else if (arg == "--dram-latency") {
                params.dram_latency_cycles = std::stoull(value_after(i, argc, argv));
            } else if (arg == "--csv") {
                csv = true;
            } else if (arg == "--help" || arg == "-h") {
                print_usage();
                return 0;
            } else {
                throw std::invalid_argument("unknown argument: " + arg);
            }
        }

        const auto metrics = run_simulation(params, strategy);

        if (csv) {
            std::cout << "strategy,scratchpad_kb,dram_latency,total_cycles,compute_cycles,dram_stall_cycles,dram_bytes,compute_utilization,arithmetic_intensity,effective_gops\n";
            std::cout << strategy << ','
                      << params.scratchpad_bytes / 1024 << ','
                      << params.dram_latency_cycles << ','
                      << metrics.total_cycles << ','
                      << metrics.compute_cycles << ','
                      << metrics.dram_stall_cycles << ','
                      << metrics.dram_bytes << ','
                      << metrics.compute_utilization() << ','
                      << metrics.arithmetic_intensity() << ','
                      << metrics.effective_gops() << '\n';
        } else {
            std::cout << std::fixed << std::setprecision(4);
            std::cout << "strategy: " << strategy << '\n';
            std::cout << "total_cycles: " << metrics.total_cycles << '\n';
            std::cout << "compute_cycles: " << metrics.compute_cycles << '\n';
            std::cout << "dram_stall_cycles: " << metrics.dram_stall_cycles << '\n';
            std::cout << "dram_bytes: " << metrics.dram_bytes << '\n';
            std::cout << "compute_utilization: " << metrics.compute_utilization() << '\n';
            std::cout << "arithmetic_intensity: " << metrics.arithmetic_intensity() << '\n';
            std::cout << "effective_gops: " << metrics.effective_gops() << '\n';
        }
    } catch (const std::exception& ex) {
        std::cerr << "error: " << ex.what() << '\n';
        print_usage();
        return 1;
    }

    return 0;
}
