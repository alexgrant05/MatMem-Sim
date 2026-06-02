#include "scratchpad.h"
#include "test_utils.h"

#include <iostream>

int g_failures = 0;

static void test_capacity_boundary() {
    Scratchpad sp(1024);
    CHECK(sp.capacity_bytes() == 1024);
    CHECK(sp.can_hold(0));
    CHECK(sp.can_hold(1));
    CHECK(sp.can_hold(1023));
    CHECK(sp.can_hold(1024));
    CHECK(!sp.can_hold(1025));
    CHECK(!sp.can_hold(1024 * 1024));
}

static void test_zero_capacity() {
    Scratchpad sp(0);
    CHECK(sp.capacity_bytes() == 0);
    CHECK(sp.can_hold(0));
    CHECK(!sp.can_hold(1));
}

static void test_large_capacity() {
    Scratchpad sp(128 * 1024);
    CHECK(sp.can_hold(128 * 1024));
    CHECK(!sp.can_hold(128 * 1024 + 1));
}

int main() {
    test_capacity_boundary();
    test_zero_capacity();
    test_large_capacity();

    if (g_failures == 0) {
        std::cout << "test_scratchpad: all tests passed\n";
    } else {
        std::cout << "test_scratchpad: " << g_failures << " test(s) FAILED\n";
    }
    return g_failures > 0 ? 1 : 0;
}
