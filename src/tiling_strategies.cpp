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

// Returns params with tile_m/n/k set to the largest square tile that allows
// num_buffers tiles to reside in the scratchpad simultaneously.
// Each buffer holds A + B + C = 3 * t^2 * element_bytes bytes.
// If tile_m is already non-zero the caller specified explicit tiles; leave them.
HardwareParams apply_auto_tile(HardwareParams p, std::uint64_t num_buffers) {
    const bool any = (p.tile_m != 0) || (p.tile_n != 0) || (p.tile_k != 0);
    const bool all = (p.tile_m != 0) && (p.tile_n != 0) && (p.tile_k != 0);
    if (any && !all) {
        throw std::invalid_argument(
            "--tile-m/n/k must all be set together or all left at 0 (auto); "
            "got tile_m=" + std::to_string(p.tile_m) +
            " tile_n=" + std::to_string(p.tile_n) +
            " tile_k=" + std::to_string(p.tile_k));
    }
    if (all) {
        return p;
    }
    const double budget = static_cast<double>(p.scratchpad_bytes) /
                          (static_cast<double>(num_buffers) * 3.0 *
                           static_cast<double>(p.element_bytes));
    auto t = static_cast<std::uint64_t>(std::sqrt(budget));
    const auto max_dim = std::min({p.matrix_m, p.matrix_n, p.matrix_k});
    t = std::max<std::uint64_t>(1, std::min(t, max_dim));
    p.tile_m = p.tile_n = p.tile_k = t;
    return p;
}

std::vector<TileWork> build_row_stationary(const HardwareParams& p_in) {
    const auto p = apply_auto_tile(p_in, 1);
    std::vector<TileWork> work;
    const auto mt = ceil_div(p.matrix_m, p.tile_m);
    const auto nt = ceil_div(p.matrix_n, p.tile_n);
    const auto kt = ceil_div(p.matrix_k, p.tile_k);

    // Full-tile bytes used only for the scratchpad capacity field (conservative check).
    const auto sp_bytes = (p.tile_m * p.tile_k + p.tile_k * p.tile_n + p.tile_m * p.tile_n)
                          * p.element_bytes;

    for (std::uint64_t mi = 0; mi < mt; ++mi) {
        const auto m = std::min(p.tile_m, p.matrix_m - mi * p.tile_m);
        for (std::uint64_t ki = 0; ki < kt; ++ki) {
            const auto k = std::min(p.tile_k, p.matrix_k - ki * p.tile_k);
            const auto a_bytes = m * k * p.element_bytes;
            for (std::uint64_t ni = 0; ni < nt; ++ni) {
                const auto n = std::min(p.tile_n, p.matrix_n - ni * p.tile_n);
                const auto b_bytes = k * n * p.element_bytes;
                const auto c_bytes = m * n * p.element_bytes;
                // A(mi,ki) stays resident across all ni; only reload it on the first ni
                const auto load = (ni == 0 ? a_bytes : 0) + b_bytes + c_bytes;
                work.push_back({load, c_bytes, 2 * m * n * k, sp_bytes});
            }
        }
    }
    return work;
}

std::vector<TileWork> build_output_stationary(const HardwareParams& p_in) {
    const auto p = apply_auto_tile(p_in, 1);
    std::vector<TileWork> work;
    const auto mt = ceil_div(p.matrix_m, p.tile_m);
    const auto nt = ceil_div(p.matrix_n, p.tile_n);
    const auto kt = ceil_div(p.matrix_k, p.tile_k);

    for (std::uint64_t mi = 0; mi < mt; ++mi) {
        const auto m = std::min(p.tile_m, p.matrix_m - mi * p.tile_m);
        for (std::uint64_t ni = 0; ni < nt; ++ni) {
            const auto n = std::min(p.tile_n, p.matrix_n - ni * p.tile_n);
            const auto c_bytes = m * n * p.element_bytes;
            std::uint64_t load_bytes = c_bytes;
            std::uint64_t ops = 0;
            std::uint64_t peak_bytes = c_bytes;
            for (std::uint64_t ki = 0; ki < kt; ++ki) {
                const auto kk = std::min(p.tile_k, p.matrix_k - ki * p.tile_k);
                const auto a_bytes = m * kk * p.element_bytes;
                const auto b_bytes = kk * n * p.element_bytes;
                load_bytes += a_bytes + b_bytes;
                ops += 2 * m * n * kk;
                peak_bytes = std::max<std::uint64_t>(peak_bytes, c_bytes + a_bytes + b_bytes);
            }
            work.push_back({load_bytes, c_bytes, ops, peak_bytes});
        }
    }
    return work;
}

} // namespace

SequentialTilingEngine::SequentialTilingEngine(HardwareParams params, std::vector<TileWork> work)
    : params_(params), work_(std::move(work)) {
    for (const auto& tile : work_) {
        if (tile.scratchpad_bytes > params_.scratchpad_bytes) {
            throw std::invalid_argument(
                "tile requires " + std::to_string(tile.scratchpad_bytes) +
                " scratchpad bytes but capacity is " + std::to_string(params_.scratchpad_bytes));
        }
    }
}

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
    : params_(apply_auto_tile(params, 2)), work_(build_output_stationary(params_)) {
    for (const auto& tile : work_) {
        if (tile.scratchpad_bytes > params.scratchpad_bytes) {
            throw std::invalid_argument(
                "tile requires " + std::to_string(tile.scratchpad_bytes) +
                " scratchpad bytes but capacity is " + std::to_string(params.scratchpad_bytes));
        }
    }
}

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
    current_store_issued_ = false;
    const bool next_can_double_buffer = index_ < work_.size() &&
        scratchpad.can_hold(work_[index_].scratchpad_bytes * 2);
    current_ready_ = next_can_double_buffer && prefetched_index_ > index_;
    if (current_ready_) {
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
