#include "hardware_params.h"
#include "simulator.h"

#include <cassert>

int main() {
    HardwareParams params;
    params.matrix_m = 64;
    params.matrix_n = 64;
    params.matrix_k = 64;
    params.scratchpad_bytes = 32 * 1024;

    for (const auto* strategy : {"row_stationary", "output_stationary", "double_buffer"}) {
        const auto metrics = run_simulation(params, strategy);
        assert(metrics.total_cycles > 0);
        assert(metrics.compute_cycles > 0);
        assert(metrics.dram_bytes > 0);
        assert(metrics.compute_utilization() > 0.0);
    }

    return 0;
}
