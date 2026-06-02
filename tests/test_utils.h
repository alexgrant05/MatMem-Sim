#pragma once

#include <cmath>
#include <iostream>

// Each test .cpp must define: int g_failures = 0;
extern int g_failures;

#define CHECK(expr) do { \
    if (!(expr)) { \
        std::cerr << "FAIL [" << __FILE__ << ':' << __LINE__ << "]  " #expr "\n"; \
        ++g_failures; \
    } \
} while(0)

#define CHECK_APPROX(a, b, eps) \
    CHECK(std::abs(static_cast<double>(a) - static_cast<double>(b)) < (eps))

#define CHECK_THROWS(ExcType, expr) do { \
    bool threw_ = false; \
    try { (void)(expr); } catch (const ExcType&) { threw_ = true; } \
    if (!threw_) { \
        std::cerr << "FAIL [" << __FILE__ << ':' << __LINE__ \
                  << "]  expected " #ExcType " from: " #expr "\n"; \
        ++g_failures; \
    } \
} while(0)
