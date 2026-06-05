#include "dram_model.h"
#include "hardware_params.h"
#include "test_utils.h"

#include <iostream>

int g_failures = 0;

// Default params: latency=100 cycles, bandwidth=32 bytes/cycle.
// request_cycles(bytes) = 100 + ceil(bytes / 32).

static HardwareParams make_params() {
    HardwareParams p;
    p.dram_latency_cycles = 100;
    p.dram_bandwidth_bytes_per_cycle = 32;
    return p;
}

static void test_starts_idle() {
    DRAMModel dram(make_params());
    CHECK(dram.idle());
    CHECK(dram.bytes_transferred() == 0);
}

static void test_zero_byte_request_ignored() {
    DRAMModel dram(make_params());
    dram.request(0);
    CHECK(dram.idle());
    CHECK(dram.bytes_transferred() == 0);
}

static void test_bytes_counted_at_request_time() {
    DRAMModel dram(make_params());
    dram.request(64);
    CHECK(dram.bytes_transferred() == 64);
    // bytes are counted even before any tick
}

static void test_single_request_latency() {
    // 64 bytes at 32 B/cyc + 100 latency = 102 ticks to idle.
    DRAMModel dram(make_params());
    dram.request(64);
    CHECK(!dram.idle());

    for (int i = 0; i < 101; ++i) dram.tick();
    CHECK(!dram.idle()); // one cycle remains

    dram.tick(); // tick 102
    CHECK(dram.idle());
}

static void test_min_latency_one_byte() {
    // 1 byte: 100 + ceil(1/32) = 101 ticks.
    DRAMModel dram(make_params());
    dram.request(1);
    for (int i = 0; i < 100; ++i) dram.tick();
    CHECK(!dram.idle());
    dram.tick(); // tick 101
    CHECK(dram.idle());
}

static void test_bandwidth_ceiling() {
    // 33 bytes: ceil(33/32) = 2 transfer cycles → 102 ticks total.
    DRAMModel dram(make_params());
    dram.request(33);
    for (int i = 0; i < 101; ++i) dram.tick();
    CHECK(!dram.idle());
    dram.tick();
    CHECK(dram.idle());
}

static void test_pipelined_requests() {
    // Two requests queued back-to-back:
    //   first  = 64 bytes → 102 cycles
    //   second = 32 bytes → 101 cycles
    // Total: 203 ticks to idle.
    DRAMModel dram(make_params());
    dram.request(64);
    dram.request(32);
    CHECK(dram.bytes_transferred() == 96);

    int ticks = 0;
    while (!dram.idle()) { dram.tick(); ++ticks; }
    CHECK(ticks == 203);
}

// ── 9. request_cycles ceiling must not wrap for huge bytes ───────────────────
// bytes = UINT64_MAX, bandwidth = 2.
// Buggy: (UINT64_MAX + 2 - 1) / 2 wraps to 0 transfer cycles (wrong).
// Fixed: UINT64_MAX / 2 + 1 = 9223372036854775808 transfer cycles.
// After 1 tick the model must still be busy, not idle.
static void test_request_cycles_no_wrap() {
    HardwareParams p;
    p.dram_latency_cycles = 0;
    p.dram_bandwidth_bytes_per_cycle = 2;
    DRAMModel dram(p);
    dram.request(18446744073709551615ULL); // UINT64_MAX bytes
    dram.tick(); // dequeues; with bug active_remaining_ becomes 0 immediately
    CHECK(!dram.idle()); // must still be busy — UINT64_MAX/2 cycles remain
}

static void test_not_idle_while_pending() {
    DRAMModel dram(make_params());
    dram.request(32);
    dram.request(32);
    // Even before any tick, both in flight or pending → not idle.
    CHECK(!dram.idle());
    // Complete the first, second should keep it busy.
    for (int i = 0; i < 101; ++i) dram.tick();
    CHECK(!dram.idle()); // second request now active
}

int main() {
    test_starts_idle();
    test_zero_byte_request_ignored();
    test_bytes_counted_at_request_time();
    test_single_request_latency();
    test_min_latency_one_byte();
    test_bandwidth_ceiling();
    test_pipelined_requests();
    test_not_idle_while_pending();
    test_request_cycles_no_wrap();

    if (g_failures == 0) {
        std::cout << "test_dram: all tests passed\n";
    } else {
        std::cout << "test_dram: " << g_failures << " test(s) FAILED\n";
    }
    return g_failures > 0 ? 1 : 0;
}
