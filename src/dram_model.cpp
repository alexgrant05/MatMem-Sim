#include "dram_model.h"

#include <algorithm>
#include <limits>
#include <stdexcept>

DRAMModel::DRAMModel(const HardwareParams& params) : params_(params) {}

void DRAMModel::request(std::uint64_t bytes) {
    if (bytes == 0) {
        return;
    }
    pending_.push(bytes);
    if (bytes > std::numeric_limits<std::uint64_t>::max() - bytes_transferred_) {
        throw std::overflow_error("bytes_transferred_ overflow");
    }
    bytes_transferred_ += bytes;
}

void DRAMModel::tick() {
    if (active_remaining_ == 0 && !pending_.empty()) {
        active_remaining_ = request_cycles(pending_.front());
        pending_.pop();
    }

    if (active_remaining_ > 0) {
        --active_remaining_;
    }
}

bool DRAMModel::idle() const {
    return active_remaining_ == 0 && pending_.empty();
}

std::uint64_t DRAMModel::bytes_transferred() const {
    return bytes_transferred_;
}

std::uint64_t DRAMModel::request_cycles(std::uint64_t bytes) const {
    const auto bandwidth = std::max<std::uint64_t>(1, params_.dram_bandwidth_bytes_per_cycle);
    const auto transfer_cycles = bytes / bandwidth + (bytes % bandwidth != 0 ? 1 : 0);
    return params_.dram_latency_cycles + transfer_cycles;
}
