#pragma once

#include "hardware_params.h"

#include <cstdint>
#include <queue>

class DRAMModel {
public:
    explicit DRAMModel(const HardwareParams& params);

    void request(std::uint64_t bytes);
    void tick();
    bool idle() const;
    std::uint64_t bytes_transferred() const;
    // Cycles until all currently queued work (active + pending) completes. A
    // request read immediately after issuing it gives the cycle at which that
    // request finishes, since the queue is FIFO with a single server.
    std::uint64_t backlog_cycles() const;

private:
    std::uint64_t request_cycles(std::uint64_t bytes) const;

    HardwareParams params_;
    std::queue<std::uint64_t> pending_;
    std::uint64_t active_remaining_ = 0;
    std::uint64_t bytes_transferred_ = 0;
    std::uint64_t backlog_remaining_ = 0;
};
