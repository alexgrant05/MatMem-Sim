#include "simulator.h"

#include "dram_model.h"
#include "scratchpad.h"
#include "tiling_engine.h"

#include <stdexcept>

Metrics run_simulation(const HardwareParams& params, const std::string& strategy_name) {
    auto strategy = make_strategy(strategy_name, params);
    DRAMModel dram(params);
    Scratchpad scratchpad(params.scratchpad_bytes);
    Metrics metrics;

    const std::uint64_t max_cycles = 1'000'000'000;
    while (!strategy->done()) {
        strategy->tick(dram, scratchpad, metrics);
        if (metrics.total_cycles > max_cycles) {
            throw std::runtime_error("simulation exceeded max cycle guard");
        }
    }

    metrics.dram_bytes = dram.bytes_transferred();
    return metrics;
}
