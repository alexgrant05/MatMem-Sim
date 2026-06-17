#include "tiling_engine.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace {

std::uint64_t ceil_div(std::uint64_t a, std::uint64_t b) {
    return a / b + (a % b != 0 ? 1 : 0);
}

std::uint64_t compute_cycles(const HardwareParams& params, std::uint64_t operations) {
    if (operations == 0) return 0;
    return ceil_div(operations, std::max<std::uint64_t>(1, params.compute_ops_per_cycle));
}

// Multiplies two uint64_t values and throws std::overflow_error if the result
// would exceed UINT64_MAX (e.g. tile dimension products with large matrices).
std::uint64_t safe_mul(std::uint64_t a, std::uint64_t b) {
    if (a != 0 && b > std::numeric_limits<std::uint64_t>::max() / a) {
        throw std::overflow_error("tile dimension product overflows uint64_t");
    }
    return a * b;
}

// Adds two uint64_t values and throws std::overflow_error on wrap.
std::uint64_t safe_add(std::uint64_t a, std::uint64_t b) {
    if (a > std::numeric_limits<std::uint64_t>::max() - b) {
        throw std::overflow_error("tile dimension sum overflows uint64_t");
    }
    return a + b;
}

// Maximum work items materialised per strategy to prevent OOM.
// 10M tiles × 32 bytes = 320 MB ceiling.
static constexpr std::uint64_t MAX_TILE_COUNT = 10'000'000;

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

    if (mt == 0 || nt == 0 || kt == 0) return work;

    const auto tile_count = safe_mul(safe_mul(mt, nt), kt);
    if (tile_count > MAX_TILE_COUNT) {
        throw std::overflow_error(
            "row_stationary tile count " + std::to_string(tile_count) +
            " exceeds limit " + std::to_string(MAX_TILE_COUNT));
    }
    work.reserve(static_cast<std::size_t>(tile_count));

    for (std::uint64_t mi = 0; mi < mt; ++mi) {
        const auto m = std::min(p.tile_m, p.matrix_m - mi * p.tile_m);
        for (std::uint64_t ki = 0; ki < kt; ++ki) {
            const auto k = std::min(p.tile_k, p.matrix_k - ki * p.tile_k);
            const auto a_bytes = safe_mul(safe_mul(m, k), p.element_bytes);
            for (std::uint64_t ni = 0; ni < nt; ++ni) {
                const auto n = std::min(p.tile_n, p.matrix_n - ni * p.tile_n);
                const auto b_bytes = safe_mul(safe_mul(k, n), p.element_bytes);
                const auto c_bytes = safe_mul(safe_mul(m, n), p.element_bytes);
                // Scratchpad peak: A stays resident + current B + C
                const auto sp_bytes = safe_add(safe_add(a_bytes, b_bytes), c_bytes);
                // A(mi,ki) stays resident across all ni; only reload it on the first ni
                const auto load = safe_add(safe_add(ni == 0 ? a_bytes : std::uint64_t(0), b_bytes), c_bytes);
                work.push_back({load, c_bytes, safe_mul(safe_mul(safe_mul(2ULL, m), n), k), sp_bytes});
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

    if (mt == 0 || nt == 0 || kt == 0) return work;

    const auto tile_count = safe_mul(mt, nt);
    const auto total_work = safe_mul(tile_count, kt); // bounds inner K-loop iterations too
    if (total_work > MAX_TILE_COUNT) {
        throw std::overflow_error(
            "output_stationary construction work " + std::to_string(total_work) +
            " exceeds limit " + std::to_string(MAX_TILE_COUNT));
    }
    work.reserve(static_cast<std::size_t>(total_work));

    for (std::uint64_t mi = 0; mi < mt; ++mi) {
        const auto m = std::min(p.tile_m, p.matrix_m - mi * p.tile_m);
        for (std::uint64_t ni = 0; ni < nt; ++ni) {
            const auto n = std::min(p.tile_n, p.matrix_n - ni * p.tile_n);
            const auto c_bytes = safe_mul(safe_mul(m, n), p.element_bytes);
            for (std::uint64_t ki = 0; ki < kt; ++ki) {
                const auto kk = std::min(p.tile_k, p.matrix_k - ki * p.tile_k);
                const auto a_bytes = safe_mul(safe_mul(m, kk), p.element_bytes);
                const auto b_bytes = safe_mul(safe_mul(kk, n), p.element_bytes);
                // C stays resident across all K tiles; A+B loaded every K tile.
                // C is loaded on the first K tile and stored on the last.
                const auto load = safe_add(ki == 0 ? c_bytes : std::uint64_t(0),
                                           safe_add(a_bytes, b_bytes));
                const auto store = (ki == kt - 1) ? c_bytes : std::uint64_t(0);
                const auto sp_bytes = safe_add(safe_add(c_bytes, a_bytes), b_bytes);
                const auto ops = safe_mul(safe_mul(safe_mul(2ULL, m), n), kk);
                // C is already resident for ki > 0; the double-buffer prefetch
                // check uses this to avoid double-counting the shared C tile.
                const auto resident = ki == 0 ? std::uint64_t(0) : c_bytes;
                work.push_back({load, store, ops, sp_bytes, resident});
            }
        }
    }
    return work;
}

// Input stationary: B(ki,ni) stays resident across all mi; A and C stream.
// Loop order ni -> ki -> mi is the mirror of row stationary (mi -> ki -> ni)
// under the M<->N / A<->B symmetry.
std::vector<TileWork> build_input_stationary(const HardwareParams& p_in) {
    const auto p = apply_auto_tile(p_in, 1);
    std::vector<TileWork> work;
    const auto mt = ceil_div(p.matrix_m, p.tile_m);
    const auto nt = ceil_div(p.matrix_n, p.tile_n);
    const auto kt = ceil_div(p.matrix_k, p.tile_k);

    if (mt == 0 || nt == 0 || kt == 0) return work;

    const auto tile_count = safe_mul(safe_mul(mt, nt), kt);
    if (tile_count > MAX_TILE_COUNT) {
        throw std::overflow_error(
            "input_stationary tile count " + std::to_string(tile_count) +
            " exceeds limit " + std::to_string(MAX_TILE_COUNT));
    }
    work.reserve(static_cast<std::size_t>(tile_count));

    for (std::uint64_t ni = 0; ni < nt; ++ni) {
        const auto n = std::min(p.tile_n, p.matrix_n - ni * p.tile_n);
        for (std::uint64_t ki = 0; ki < kt; ++ki) {
            const auto k = std::min(p.tile_k, p.matrix_k - ki * p.tile_k);
            const auto b_bytes = safe_mul(safe_mul(k, n), p.element_bytes);
            for (std::uint64_t mi = 0; mi < mt; ++mi) {
                const auto m = std::min(p.tile_m, p.matrix_m - mi * p.tile_m);
                const auto a_bytes = safe_mul(safe_mul(m, k), p.element_bytes);
                const auto c_bytes = safe_mul(safe_mul(m, n), p.element_bytes);
                // Scratchpad peak: B stays resident + current A + C
                const auto sp_bytes = safe_add(safe_add(b_bytes, a_bytes), c_bytes);
                // B(ki,ni) stays resident across all mi; only reload it on the first mi
                const auto load = safe_add(safe_add(mi == 0 ? b_bytes : std::uint64_t(0), a_bytes), c_bytes);
                work.push_back({load, c_bytes, safe_mul(safe_mul(safe_mul(2ULL, m), n), k), sp_bytes});
            }
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
        metrics.operations = safe_add(metrics.operations, tile.operations);
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

InputStationaryEngine::InputStationaryEngine(const HardwareParams& params)
    : SequentialTilingEngine(params, build_input_stationary(params)) {}

std::string InputStationaryEngine::name() const {
    return "input_stationary";
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
        metrics.operations = safe_add(metrics.operations, tile.operations);
    }

    // Compute combined footprint. The next tile's resident_bytes are already
    // present in the current tile's scratchpad (shared C across K-sub-tiles of
    // the same output tile), so only the incremental new bytes need to fit.
    // Saturate to UINT64_MAX on overflow so can_hold() returns false safely.
    const auto next_sp       = prefetched_index_ < work_.size()
                               ? work_[prefetched_index_].scratchpad_bytes : 0;
    const auto next_resident = prefetched_index_ < work_.size()
                               ? work_[prefetched_index_].resident_bytes : 0;
    const auto next_new      = next_sp - next_resident; // safe: resident ≤ scratchpad by construction
    const auto combined_sp   =
        (next_new <= std::numeric_limits<std::uint64_t>::max() - tile.scratchpad_bytes)
        ? tile.scratchpad_bytes + next_new
        : std::numeric_limits<std::uint64_t>::max();

    if (prefetched_index_ == index_ + 1 && prefetched_index_ < work_.size() &&
        scratchpad.can_hold(combined_sp)) {
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

    // For the final tile there is no successor compute to hide the store; wait
    // for DRAM to drain so the engine reaches done() with clean state.
    // For all other tiles, advance immediately: the store drains in the
    // background during the next tile's compute cycles (true ping-pong).
    if (index_ + 1 >= work_.size() && !dram.idle()) {
        metrics.dram_stall_cycles++;
        dram.tick();
        metrics.total_cycles++;
        return;
    }

    ++index_;
    current_store_issued_ = false;
    current_ready_ = (index_ < work_.size()) && (prefetched_index_ > index_);
    if (current_ready_) {
        compute_remaining_ = compute_cycles(params_, work_[index_].operations);
        metrics.operations = safe_add(metrics.operations, work_[index_].operations);
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
    if (name == "input_stationary") {
        return std::make_unique<InputStationaryEngine>(params);
    }
    if (name == "double_buffer") {
        return std::make_unique<DoubleBufferEngine>(params);
    }
    throw std::invalid_argument("unknown strategy: " + name);
}
