#pragma once

#include <cstdint>

class Scratchpad {
public:
    explicit Scratchpad(std::uint64_t capacity_bytes);

    bool can_hold(std::uint64_t bytes) const;
    std::uint64_t capacity_bytes() const;

private:
    std::uint64_t capacity_bytes_;
};
