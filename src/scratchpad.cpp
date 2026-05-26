#include "scratchpad.h"

Scratchpad::Scratchpad(std::size_t capacity_bytes) : capacity_bytes_(capacity_bytes) {}

bool Scratchpad::can_hold(std::size_t bytes) const {
    return bytes <= capacity_bytes_;
}

std::size_t Scratchpad::capacity_bytes() const {
    return capacity_bytes_;
}
