#include "hardware_params.h"
#include "simulator.h"
#include "test_utils.h"

#include <iostream>
#include <stdexcept>

int g_failures = 0;

static HardwareParams small_matrix() {
    HardwareParams p;
    p.matrix_m = 64;
    p.matrix_n = 64;
    p.matrix_k = 64;
    p.scratchpad_bytes = 32 * 1024;
    return p;
}

// ── 1. Basic sanity ────────────────────────────────────────────────────────
static void test_all_strategies_produce_nonzero_metrics() {
    const auto p = small_matrix();
    for (const auto* s : {"row_stationary", "output_stationary", "double_buffer"}) {
        const auto m = run_simulation(p, s);
        CHECK(m.total_cycles > 0);
        CHECK(m.compute_cycles > 0);
        CHECK(m.dram_bytes > 0);
        CHECK(m.operations > 0);
        CHECK(m.compute_utilization() > 0.0);
        CHECK(m.arithmetic_intensity() > 0.0);
        CHECK(m.effective_ops_per_cycle() > 0.0);
    }
}

// ── 2. Cycle accounting invariant ─────────────────────────────────────────
// Every tick increments exactly one of compute_cycles or dram_stall_cycles,
// and always increments total_cycles. So their sum must equal total_cycles.
static void test_cycle_accounting_invariant() {
    const auto p = small_matrix();
    for (const auto* s : {"row_stationary", "output_stationary", "double_buffer"}) {
        const auto m = run_simulation(p, s);
        CHECK(m.compute_cycles + m.dram_stall_cycles == m.total_cycles);
    }
}

// ── 3. Compute utilization is in (0, 1] ───────────────────────────────────
static void test_utilization_range() {
    const auto p = small_matrix();
    for (const auto* s : {"row_stationary", "output_stationary", "double_buffer"}) {
        const auto m = run_simulation(p, s);
        CHECK(m.compute_utilization() > 0.0);
        CHECK(m.compute_utilization() <= 1.0);
    }
}

// ── 4. Row and output stationary same operation count ─────────────────────
// Both use the same auto-tile size (1-buffer), so same tile decomposition,
// so same total operation count.
static void test_row_output_same_operations() {
    const auto p = small_matrix();
    const auto row = run_simulation(p, "row_stationary");
    const auto out = run_simulation(p, "output_stationary");
    CHECK(row.operations == out.operations);
}

// ── 5. Double buffer prefetch isolates to fewer total cycles ──────────────
// With an *explicit* tile size (same for both strategies), dram_bytes and
// operations are identical. Double buffer's prefetch then reduces total_cycles.
//
// tile = 36: two tiles need 2 * 3 * 36^2 * 4 = 31104 bytes <= 32768 (fits).
// On a 256x256x256 matrix compute_time (2916 cyc) > load_time (2854 cyc),
// so prefetching completes during compute → meaningful overlap.
static void test_double_buffer_prefetch_reduces_cycles() {
    HardwareParams p;
    p.matrix_m = p.matrix_n = p.matrix_k = 256;
    p.scratchpad_bytes = 32 * 1024;
    p.tile_m = p.tile_n = p.tile_k = 36;

    const auto db = run_simulation(p, "double_buffer");
    const auto os = run_simulation(p, "output_stationary");

    CHECK(db.operations == os.operations);   // same work
    CHECK(db.dram_bytes == os.dram_bytes);   // same traffic
    CHECK(db.total_cycles < os.total_cycles); // prefetch hides latency
    CHECK(db.compute_utilization() > os.compute_utilization());
}

// ── 6. Double buffer stall cycles strictly less than output stationary ────
// Prefetching converts load-stall cycles into overlapped compute cycles.
static void test_double_buffer_fewer_stalls() {
    HardwareParams p;
    p.matrix_m = p.matrix_n = p.matrix_k = 256;
    p.scratchpad_bytes = 32 * 1024;
    p.tile_m = p.tile_n = p.tile_k = 36;

    const auto db = run_simulation(p, "double_buffer");
    const auto os = run_simulation(p, "output_stationary");
    CHECK(db.dram_stall_cycles < os.dram_stall_cycles);
}

// ── 7. Unknown strategy name throws ───────────────────────────────────────
static void test_unknown_strategy_throws() {
    const auto p = small_matrix();
    CHECK_THROWS(std::invalid_argument, run_simulation(p, "nonexistent_strategy"));
}

// ── 8. Partial tile specification throws ──────────────────────────────────
// Setting only tile-m but leaving tile-n/k at 0 used to cause division by zero.
// It must now throw a clear error.
static void test_partial_tile_args_throw() {
    auto p = small_matrix();
    p.tile_m = 16; p.tile_n = 0; p.tile_k = 0;
    CHECK_THROWS(std::invalid_argument, run_simulation(p, "output_stationary"));

    p.tile_m = 0; p.tile_n = 16; p.tile_k = 0;
    CHECK_THROWS(std::invalid_argument, run_simulation(p, "output_stationary"));

    p.tile_m = 16; p.tile_n = 16; p.tile_k = 0;
    CHECK_THROWS(std::invalid_argument, run_simulation(p, "output_stationary"));
}

// ── 9. Explicitly oversized tile throws ───────────────────────────────────
// tile_m = 256, matrix = 64×64×64, scratchpad = 1 KB → tile needs 3×256×256×4 bytes.
static void test_oversized_tile_throws() {
    HardwareParams p = small_matrix();
    p.scratchpad_bytes = 1024;
    p.tile_m = 256;
    p.tile_n = 256;
    p.tile_k = 256;
    CHECK_THROWS(std::invalid_argument, run_simulation(p, "output_stationary"));
}

int main() {
    test_all_strategies_produce_nonzero_metrics();
    test_cycle_accounting_invariant();
    test_utilization_range();
    test_row_output_same_operations();
    test_double_buffer_prefetch_reduces_cycles();
    test_double_buffer_fewer_stalls();
    test_partial_tile_args_throw();
    test_unknown_strategy_throws();
    test_oversized_tile_throws();

    if (g_failures == 0) {
        std::cout << "test_simulation: all tests passed\n";
    } else {
        std::cout << "test_simulation: " << g_failures << " test(s) FAILED\n";
    }
    return g_failures > 0 ? 1 : 0;
}
