#include "metrics.h"
#include "test_utils.h"

#include <iostream>

int g_failures = 0;

static void test_zero_guard() {
    Metrics m;
    CHECK_APPROX(m.compute_utilization(), 0.0, 1e-12);
    CHECK_APPROX(m.arithmetic_intensity(), 0.0, 1e-12);
    CHECK_APPROX(m.effective_ops_per_cycle(), 0.0, 1e-12);
}

static void test_compute_utilization() {
    Metrics m;
    m.total_cycles = 1000;
    m.compute_cycles = 600;
    CHECK_APPROX(m.compute_utilization(), 0.6, 1e-9);

    m.compute_cycles = 1000;
    CHECK_APPROX(m.compute_utilization(), 1.0, 1e-9);

    m.compute_cycles = 0;
    CHECK_APPROX(m.compute_utilization(), 0.0, 1e-9);
}

static void test_arithmetic_intensity() {
    Metrics m;
    m.operations = 8192;
    m.dram_bytes = 4096;
    CHECK_APPROX(m.arithmetic_intensity(), 2.0, 1e-9);

    m.dram_bytes = 0;
    CHECK_APPROX(m.arithmetic_intensity(), 0.0, 1e-9); // zero guard
}

static void test_effective_ops_per_cycle() {
    Metrics m;
    m.total_cycles = 1000;
    m.operations = 256000;
    CHECK_APPROX(m.effective_ops_per_cycle(), 256.0, 1e-9);

    m.total_cycles = 0;
    CHECK_APPROX(m.effective_ops_per_cycle(), 0.0, 1e-9); // zero guard
}

static void test_utilization_bounded() {
    // compute_utilization must stay in [0, 1]
    Metrics m;
    m.total_cycles = 500;
    m.compute_cycles = 500;
    CHECK(m.compute_utilization() <= 1.0);
    CHECK(m.compute_utilization() >= 0.0);
}

int main() {
    test_zero_guard();
    test_compute_utilization();
    test_arithmetic_intensity();
    test_effective_ops_per_cycle();
    test_utilization_bounded();

    if (g_failures == 0) {
        std::cout << "test_metrics: all tests passed\n";
    } else {
        std::cout << "test_metrics: " << g_failures << " test(s) FAILED\n";
    }
    return g_failures > 0 ? 1 : 0;
}
