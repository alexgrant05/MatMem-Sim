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
// tile = 36: two adjacent K-sub-tiles need 2 * 15552 = 31104 bytes <= 32768
// (fits). Each K-tile has compute=365 cycles, load=424 cycles; prefetch is
// partial but still reduces total stall cycles vs sequential output_stationary.
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

// ── 8. Zero-K matrix → all-zero metrics for every strategy ───────────────
// K=0 means no MACs and no data to accumulate, so the simulation has nothing
// to do and must return all-zero metrics rather than spurious DRAM traffic.
static void test_zero_k_all_zero_metrics() {
    HardwareParams p = small_matrix();
    p.matrix_k = 0;
    for (const auto* s : {"row_stationary", "output_stationary", "double_buffer"}) {
        const auto m = run_simulation(p, s);
        CHECK(m.total_cycles == 0);
        CHECK(m.compute_cycles == 0);
        CHECK(m.dram_stall_cycles == 0);
        CHECK(m.dram_bytes == 0);
        CHECK(m.operations == 0);
    }
}

// ── 9. Explicit tile larger than matrix dimension is accepted ─────────────
// --tile-m/n/k 256 on a 64×64×64 matrix should work: the actual clipped tile
// is 64×64×64 = 49 152 bytes, which fits in 64 KB.  The old (broken) code
// used the full 256×256×256 dimensions for the scratchpad check and threw.
static void test_explicit_tile_larger_than_matrix() {
    HardwareParams p = small_matrix();          // 64×64×64
    p.scratchpad_bytes = 64 * 1024;             // 64 KB
    p.tile_m = p.tile_n = p.tile_k = 256;       // larger than matrix

    const auto m = run_simulation(p, "row_stationary");
    CHECK(m.total_cycles > 0);
    CHECK(m.operations == 2 * 64 * 64 * 64);    // exact: no overcounting

    const auto os = run_simulation(p, "output_stationary");
    CHECK(os.operations == 2 * 64 * 64 * 64);
}

// ── 10. Double buffer mixed tile sizes: falls back to sequential ───────────
// Repro: matrix 128×100×64, tile 64, scratchpad 70 KB.
// Full tile (n=64) sp = 49 152 bytes; edge tile (n=36) sp = 34 816 bytes.
// Every adjacent pair exceeds 70 KB (49152+34816=83968 in both orders), so
// the per-pair prefetch check must reject all prefetches.  With no prefetch
// active, double buffer degrades to sequential output_stationary: identical
// total_cycles and dram_bytes.  The old broken code set can_double_buffer=
// true for the edge tile (69632 ≤ 71680) and would have prefetched the next
// full tile illegally, producing fewer stall cycles than output_stationary.
static void test_double_buffer_mixed_tiles_falls_back_to_sequential() {
    HardwareParams p;
    p.matrix_m = 128; p.matrix_n = 100; p.matrix_k = 64;
    p.tile_m = p.tile_n = p.tile_k = 64;
    p.scratchpad_bytes = 70 * 1024;

    const auto db = run_simulation(p, "double_buffer");
    const auto os = run_simulation(p, "output_stationary");

    CHECK(db.compute_cycles + db.dram_stall_cycles == db.total_cycles);
    CHECK(db.dram_bytes == os.dram_bytes);
    // No prefetch fires → same cycle count as sequential output stationary.
    CHECK(db.total_cycles == os.total_cycles);
}

// ── 11. Partial tile specification throws ─────────────────────────────────
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

// ── 13. Huge dimensions whose byte product overflows uint64_t throw ─────────
// m = n = k = 2^32: m * k * element_bytes wraps to 0 before the fix, so the
// simulation silently returns all-zero metrics. The tiling builder must detect
// the overflow and throw std::overflow_error instead.
// ── 14. Huge compute_ops_per_cycle: ceil_div must round up, not wrap to 0 ────
// operations = 2, compute_ops = UINT64_MAX.
// Buggy ceil_div: (2 + UINT64_MAX - 1) / UINT64_MAX = (UINT64_MAX+1)/UINT64_MAX = 0.
// Fixed:          2 / UINT64_MAX + (2 % UINT64_MAX != 0) = 0 + 1 = 1.
// ── 14. sp_bytes / load_bytes addition overflows uint64_t and must throw ─────
// m = n = 2^31-1, k = 2: each byte product fits in uint64_t but
// sp_bytes = a_bytes + b_bytes + c_bytes = ~UINT64_MAX + 17 GB.
// safe_mul passes for every term; the addition itself must be guarded.
static void test_tile_byte_addition_overflow_throws() {
    HardwareParams p;
    p.matrix_m = p.matrix_n = 2147483647ULL; // 2^31 - 1
    p.matrix_k = 2;
    p.tile_m = 2147483647ULL; p.tile_n = 2147483647ULL; p.tile_k = 2;
    p.scratchpad_bytes = 18446744073709551615ULL; // UINT64_MAX
    // Instant DRAM and compute so the buggy run completes in ~3 cycles (no hang)
    p.dram_latency_cycles = 0;
    p.dram_bandwidth_bytes_per_cycle = 18446744073709551615ULL; // UINT64_MAX
    p.compute_ops_per_cycle = 18446744073709551615ULL;          // UINT64_MAX
    CHECK_THROWS(std::overflow_error, run_simulation(p, "row_stationary"));
    CHECK_THROWS(std::overflow_error, run_simulation(p, "output_stationary"));
}

// ── 15. Huge tile counts must throw before the work vector is materialised ───
// row_stationary: 216^3 = 10.08M tiles → exceeds the 10M explicit limit.
// output_stationary: 3163^2 = 10.0M outer tiles → same limit.
// Fast DRAM settings keep the "before fix" run under 5 s.
static void test_huge_tile_count_throws() {
    HardwareParams p;
    p.matrix_m = p.matrix_n = p.matrix_k = 216; // 216^3 = 10,077,696 > 10M
    p.tile_m = p.tile_n = p.tile_k = 1;
    p.scratchpad_bytes = 64 * 1024 * 1024;
    p.dram_latency_cycles = 0;
    p.dram_bandwidth_bytes_per_cycle = 18446744073709551615ULL; // UINT64_MAX — instant
    p.compute_ops_per_cycle = 18446744073709551615ULL;          // UINT64_MAX — instant
    CHECK_THROWS(std::overflow_error, run_simulation(p, "row_stationary"));

    HardwareParams p2;
    p2.matrix_m = 3163; p2.matrix_n = 3163; p2.matrix_k = 1; // 3163^2 = 10,004,569 > 10M
    p2.tile_m = 1; p2.tile_n = 1; p2.tile_k = 1;
    p2.scratchpad_bytes = 64 * 1024 * 1024;
    p2.dram_latency_cycles = 0;
    p2.dram_bandwidth_bytes_per_cycle = 18446744073709551615ULL;
    p2.compute_ops_per_cycle = 18446744073709551615ULL;
    CHECK_THROWS(std::overflow_error, run_simulation(p2, "output_stationary"));
}

static void test_compute_cycles_huge_ops_rounds_up() {
    HardwareParams p;
    p.matrix_m = p.matrix_n = p.matrix_k = 1;
    p.tile_m = p.tile_n = p.tile_k = 1;
    p.scratchpad_bytes = 64;
    p.compute_ops_per_cycle = 18446744073709551615ULL; // UINT64_MAX
    const auto m = run_simulation(p, "row_stationary");
    CHECK(m.compute_cycles == 1); // ceil(2 / UINT64_MAX) = 1, not 0
}

// ── 16. Huge K-tile count stalls output_stationary construction ──────────────
// mt*nt = 1 but kt = 11M: before the fix the inner K loop runs 11M times
// (visible as a multi-second hang) with no exception. After the fix the
// mt*nt*kt total-work check throws immediately.
// ── 17. tile-k affects output_stationary cycle count ─────────────────────────
// --tile-k 1 pays DRAM latency for each of 64 K-sub-tiles; --tile-k 64 pays
// it once. Before the fix both cases produce identical total_cycles because the
// inner K loop was batched into one DRAM request. After the fix they diverge.
// ── 17. Double buffer must not double-count C when prefetching adjacent K tiles ─
// 64×64×2, tile 64×64×1, scratchpad=20KB.
// True combined footprint when prefetching K-chunk 1 into K-chunk 0's buffer:
//   C(shared) + A0 + B0 + A1 + B1 = 16384+256+256+256+256 = 17408 B ≤ 20KB → fits.
// Buggy check: (C+A0+B0) + (C+A1+B1) = 33792 B > 20KB → prefetch blocked.
// Result before fix: db.total_cycles == os.total_cycles (falls back to sequential).
// ── 18. Double buffer must overlap store writeback with next tile's compute ───
// 16 output tiles (4×4 grid), bw=1 B/cycle, lat=0, compute_ops=1.
// Per tile: load=8192 cyc, compute=32768 cyc, store=4096 cyc.
// Compute >> store >> load, so every store can be fully hidden in successor compute.
// Before fix: engine waits for dram.idle() after each store → 15 × 4096 = 61440
//             extra stall cycles. After fix: only the initial load stall (8192) and
//             the final tile's store stall (4096) remain unavoidable.
static void test_double_buffer_store_overlaps_with_compute() {
    HardwareParams p;
    p.matrix_m = p.matrix_n = 128; p.matrix_k = 16;
    p.tile_m = p.tile_n = 32; p.tile_k = 16;   // 16 independent output tiles (kt=1 each)
    p.scratchpad_bytes = 20 * 1024;             // 20KB: two 8KB tiles fit
    p.dram_latency_cycles = 0;
    p.dram_bandwidth_bytes_per_cycle = 1;       // 1 B/cycle: transfer time == bytes
    p.compute_ops_per_cycle = 1;

    const auto db = run_simulation(p, "double_buffer");
    const auto os = run_simulation(p, "output_stationary");

    CHECK(db.total_cycles < os.total_cycles);
    CHECK(db.operations == os.operations);
    CHECK(db.dram_bytes == os.dram_bytes);
    // Before fix: db.dram_stall_cycles == 8192 + 16*4096 == 73728
    // After fix:  db.dram_stall_cycles == 8192 + 4096     == 12288 (only first+last)
    CHECK(db.dram_stall_cycles <= 13000);
}

static void test_double_buffer_k_tile_prefetch_no_double_count() {
    HardwareParams p;
    p.matrix_m = 64; p.matrix_n = 64; p.matrix_k = 2;
    p.tile_m = 64;   p.tile_n = 64;  p.tile_k = 1;
    p.scratchpad_bytes = 20 * 1024; // 20KB: true footprint fits, buggy check blocks

    const auto db = run_simulation(p, "double_buffer");
    const auto os = run_simulation(p, "output_stationary");

    CHECK(db.total_cycles < os.total_cycles);
    CHECK(db.dram_stall_cycles < os.dram_stall_cycles);
    CHECK(db.operations == os.operations);
    CHECK(db.dram_bytes == os.dram_bytes);
}

static void test_output_stationary_tile_k_affects_cycles() {
    HardwareParams p;
    p.matrix_m = 64; p.matrix_n = 64; p.matrix_k = 64;
    p.tile_m = 64;   p.tile_n = 64;
    p.scratchpad_bytes = 64 * 1024;

    p.tile_k = 1;  // 64 K-sub-tiles, 64 latencies
    const auto m1 = run_simulation(p, "output_stationary");

    p.tile_k = 64; // 1 K-sub-tile, 1 latency
    const auto m64 = run_simulation(p, "output_stationary");

    CHECK(m1.total_cycles > m64.total_cycles);       // more round trips
    CHECK(m1.dram_stall_cycles > m64.dram_stall_cycles);
    CHECK(m1.operations == m64.operations);           // same total work
    CHECK(m1.dram_bytes == m64.dram_bytes);           // same data volume
}

static void test_huge_k_count_throws() {
    HardwareParams p;
    p.matrix_m = 1; p.matrix_n = 1; p.matrix_k = 11000000; // kt = 11M > 10M limit
    p.tile_m = 1;  p.tile_n = 1;  p.tile_k = 1;
    p.scratchpad_bytes = 64 * 1024 * 1024;
    p.dram_latency_cycles = 0;
    p.dram_bandwidth_bytes_per_cycle = 18446744073709551615ULL; // UINT64_MAX
    p.compute_ops_per_cycle = 18446744073709551615ULL;
    CHECK_THROWS(std::overflow_error, run_simulation(p, "output_stationary"));
    CHECK_THROWS(std::overflow_error, run_simulation(p, "double_buffer"));
}

static void test_large_dim_overflow_throws() {
    HardwareParams p;
    p.matrix_m = p.matrix_n = p.matrix_k = 4294967296ULL; // 2^32
    p.tile_m = p.tile_n = p.tile_k = 4294967296ULL;
    p.scratchpad_bytes = 64ULL * 1024 * 1024 * 1024; // 64 GB, not the constraint
    CHECK_THROWS(std::overflow_error, run_simulation(p, "row_stationary"));
    CHECK_THROWS(std::overflow_error, run_simulation(p, "output_stationary"));
    CHECK_THROWS(std::overflow_error, run_simulation(p, "double_buffer"));
}

int main() {
    test_all_strategies_produce_nonzero_metrics();
    test_cycle_accounting_invariant();
    test_utilization_range();
    test_row_output_same_operations();
    test_double_buffer_prefetch_reduces_cycles();
    test_double_buffer_fewer_stalls();
    test_zero_k_all_zero_metrics();
    test_explicit_tile_larger_than_matrix();
    test_double_buffer_mixed_tiles_falls_back_to_sequential();
    test_partial_tile_args_throw();
    test_unknown_strategy_throws();
    test_oversized_tile_throws();
    test_large_dim_overflow_throws();
    test_tile_byte_addition_overflow_throws();
    test_double_buffer_store_overlaps_with_compute();
    test_double_buffer_k_tile_prefetch_no_double_count();
    test_output_stationary_tile_k_affects_cycles();
    test_huge_tile_count_throws();
    test_huge_k_count_throws();
    test_compute_cycles_huge_ops_rounds_up();

    if (g_failures == 0) {
        std::cout << "test_simulation: all tests passed\n";
    } else {
        std::cout << "test_simulation: " << g_failures << " test(s) FAILED\n";
    }
    return g_failures > 0 ? 1 : 0;
}
