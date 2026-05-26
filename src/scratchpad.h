#pragma once

#include <cstddef>

class Scratchpad {
public:
    explicit Scratchpad(std::size_t capacity_bytes);

    bool can_hold(std::size_t bytes) const;
    std::size_t capacity_bytes() const;

private:
    std::size_t capacity_bytes_;
};
