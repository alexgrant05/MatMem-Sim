#include "tiling_engine.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace {

std::uint64_t ceil_div(std::uint64_t a, std::uint64_t b) {
    return (a + b - 1) / b;
}

std::uint64_t compute_cycles(const HardwareParams& params, std::uint64_t operations) {
    return std::max<std::uint64_t>(1, ceil_div(operations, std::max<std::uint64_t>(1, params.compute_ops_per_cycle)));
}

std::vector<TileWork> build_row_stationary(const HardwareParams& p) {
    std::vector<TileWork> work;
    const auto mt = ceil_div(p.matrix_m, p.tile_m);
    const auto nt = ceil_div(p.matrix_n, p.tile_n);
    const auto kt = ceil_div(p.matrix_k, p.tile_k);

    for (std::uint64_t mi = 0; mi < mt; ++mi) {
        for (std::uint64_t ni = 0; ni < nt; ++ni) {
            for (std::uint64_t ki = 0; ki < kt; ++ki) {
                const auto a_bytes = p.tile_m * p.tile_k * p.element_bytes;
                const auto b_bytes = p.tile_k * p.tile_n * p.element_bytes;
                const auto c_bytes = p.tile_m * p.tile_n * p.element_bytes;
                work.push_back({a_bytes + b_bytes + c_bytes, c_bytes, 2 * p.tile_m * p.tile_n * p.tile_k, a_bytes + b_bytes + c_bytes});
            }
        }
    }
    return work;
}

std::vector<TileWork> build_output_stationary(const HardwareParams& p) {
    std::vector<TileWork> work;
    const auto mt = ceil_div(p.matrix_m, p.tile_m);
    const auto nt = ceil_div(p.matrix_n, p.tile_n);
    const auto kt = ceil_div(p.matrix_k, p.tile_k);

    for (std::uint64_t mi = 0; mi < mt; ++mi) {
        for (std::uint64_t ni = 0; ni < nt; ++ni) {
            const auto c_bytes = p.tile_m * p.tile_n * p.element_bytes;
            std::uint64_t load_bytes = c_bytes;
            std::uint64_t ops = 0;
            std::size_t peak_bytes = c_bytes;
            for (std::uint64_t ki = 0; ki < kt; ++ki) {
                const auto a_bytes = p.tile_m * p.tile_k * p.element_bytes;
                const auto b_bytes = p.tile_k * p.tile_n * p.element_bytes;
                load_bytes += a_bytes + b_bytes;
                ops += 2 * p.tile_m * p.tile_n * p.tile_k;
                peak_bytes = std::max<std::size_t>(peak_bytes, c_bytes + a_bytes + b_bytes);
            }
            work.push_back({load_bytes, c_bytes, ops, peak_bytes});
        }
    }
    return work;
}

} // namespace

SequentialTilingEngine::SequentialTilingEngine(HardwareParams params, std::vector<TileWork> work)
    : params_(params), work_(std::move(work)) {}

void SequentialTilingEngine::tick(DRAMModel& dram, Scratchpad& scratchpad, Metrics& metrics) {
    if (done()) {
        return;
    }

    auto& tile = work_[index_];
    if (!scratchpad.can_hold(tile.scratchpad_bytes)) {
        metrics.dram_stall_cycles++;
        dram.tick();
        metrics.total_cycles++;
        return;
    }

    if (!load_issued_) {
        dram.request(tile.load_bytes);
        load_issued_ = true;
    }

    if (!dram.idle()) {
        metrics.dram_stall_cycles++;
        dram.tick();
        metrics.total_cycles++;
        return;
    }

    if (!compute_started_) {
        compute_remaining_ = compute_cycles(params_, tile.operations);
        metrics.operations += tile.operations;
        compute_started_ = true;
    }

    if (compute_remaining_ > 0) {
        --compute_remaining_;
        metrics.compute_cycles++;
        dram.tick();
        metrics.total_cycles++;
        return;
    }

    if (!store_issued_) {
        dram.request(tile.store_bytes);
        store_issued_ = true;
    }

    if (!dram.idle()) {
        metrics.dram_stall_cycles++;
        dram.tick();
        metrics.total_cycles++;
        return;
    }

    ++index_;
    load_issued_ = false;
    store_issued_ = false;
    compute_started_ = false;
}

bool SequentialTilingEngine::done() const {
    return index_ >= work_.size();
}

RowStationaryEngine::RowStationaryEngine(const HardwareParams& params)
    : SequentialTilingEngine(params, build_row_stationary(params)) {}

std::string RowStationaryEngine::name() const {
    return "row_stationary";
}

OutputStationaryEngine::OutputStationaryEngine(const HardwareParams& params)
    : SequentialTilingEngine(params, build_output_stationary(params)) {}

std::string OutputStationaryEngine::name() const {
    return "output_stationary";
}

DoubleBufferEngine::DoubleBufferEngine(const HardwareParams& params)
    : params_(params), work_(build_output_stationary(params)) {}

std::string DoubleBufferEngine::name() const {
    return "double_buffer";
}

void DoubleBufferEngine::tick(DRAMModel& dram, Scratchpad& scratchpad, Metrics& metrics) {
    if (done()) {
        return;
    }

    auto& tile = work_[index_];
    const bool can_double_buffer = scratchpad.can_hold(tile.scratchpad_bytes * 2);
    if (!scratchpad.can_hold(tile.scratchpad_bytes)) {
        metrics.dram_stall_cycles++;
        dram.tick();
        metrics.total_cycles++;
        return;
    }

    if (!current_ready_) {
        if (prefetched_index_ == index_) {
            dram.request(tile.load_bytes);
            ++prefetched_index_;
        }
        if (!dram.idle()) {
            metrics.dram_stall_cycles++;
            dram.tick();
            metrics.total_cycles++;
            return;
        }
        current_ready_ = true;
        compute_remaining_ = compute_cycles(params_, tile.operations);
        metrics.operations += tile.operations;
    }

    if (can_double_buffer && prefetched_index_ == index_ + 1 && prefetched_index_ < work_.size()) {
        dram.request(work_[prefetched_index_].load_bytes);
        ++prefetched_index_;
    }

    if (compute_remaining_ > 0) {
        --compute_remaining_;
        metrics.compute_cycles++;
        dram.tick();
        metrics.total_cycles++;
        return;
    }

    if (!current_store_issued_) {
        dram.request(tile.store_bytes);
        current_store_issued_ = true;
    }

    if (!dram.idle()) {
        metrics.dram_stall_cycles++;
        dram.tick();
        metrics.total_cycles++;
        return;
    }

    ++index_;
    current_ready_ = can_double_buffer && prefetched_index_ > index_;
    current_store_issued_ = false;
    if (current_ready_ && index_ < work_.size()) {
        compute_remaining_ = compute_cycles(params_, work_[index_].operations);
        metrics.operations += work_[index_].operations;
    }
}

bool DoubleBufferEngine::done() const {
    return index_ >= work_.size();
}

std::unique_ptr<TilingEngine> make_strategy(const std::string& name, const HardwareParams& params) {
    if (name == "row_stationary") {
        return std::make_unique<RowStationaryEngine>(params);
    }
    if (name == "output_stationary") {
        return std::make_unique<OutputStationaryEngine>(params);
    }
    if (name == "double_buffer") {
        return std::make_unique<DoubleBufferEngine>(params);
    }
    throw std::invalid_argument("unknown strategy: " + name);
}
