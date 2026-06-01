#include "scratchpad.h"

Scratchpad::Scratchpad(std::uint64_t capacity_bytes) : capacity_bytes_(capacity_bytes) {}

bool Scratchpad::can_hold(std::uint64_t bytes) const {
    return bytes <= capacity_bytes_;
}

std::uint64_t Scratchpad::capacity_bytes() const {
    return capacity_bytes_;
}
