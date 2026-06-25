#pragma once

#include "dram_model.h"
#include "hardware_params.h"
#include "metrics.h"
#include "scratchpad.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct TileWork {
    std::uint64_t load_bytes = 0;
    std::uint64_t store_bytes = 0;
    std::uint64_t operations = 0;
    std::uint64_t scratchpad_bytes = 0;
    std::uint64_t a_load_bytes = 0;
    std::uint64_t b_load_bytes = 0;
    std::uint64_t c_load_bytes = 0;
    std::uint64_t a_demand_bytes = 0;
    std::uint64_t b_demand_bytes = 0;
    std::uint64_t c_demand_bytes = 0;
    // Bytes already resident in the scratchpad from the previous tile (e.g. a
    // shared C tile across K-sub-tiles of the same output tile). The double-
    // buffer prefetch check subtracts these to avoid double-counting shared data.
    std::uint64_t resident_bytes = 0;
};

// Per-tile phase record produced by --trace.
// Ranges are half-open: phase X runs on cycles [X_start, X_end).
// For double buffer, phase intervals may overlap other tiles' intervals.
// end is the scheduling boundary for the next tile and can precede store_end
// when a writeback is hidden under successor compute.
struct TraceRecord {
    std::uint64_t load_start    = 0;
    std::uint64_t load_end      = 0;
    std::uint64_t compute_start = 0;
    std::uint64_t compute_end   = 0;
    std::uint64_t store_start   = 0;
    std::uint64_t store_end     = 0;
    std::uint64_t end           = 0; // first cycle of next tile (exclusive)
};

class TilingEngine {
public:
    virtual ~TilingEngine() = default;
    virtual std::string name() const = 0;
    virtual void tick(DRAMModel& dram, Scratchpad& scratchpad, Metrics& metrics) = 0;
    virtual bool done() const = 0;
    virtual const std::vector<TraceRecord>& trace() const = 0;
};

class SequentialTilingEngine : public TilingEngine {
public:
    SequentialTilingEngine(HardwareParams params, std::vector<TileWork> work);

    void tick(DRAMModel& dram, Scratchpad& scratchpad, Metrics& metrics) override;
    bool done() const override;
    const std::vector<TraceRecord>& trace() const override;

protected:
    HardwareParams params_;
    std::vector<TileWork> work_;
    std::size_t index_ = 0;
    bool load_issued_ = false;
    bool store_issued_ = false;
    bool compute_started_ = false;
    std::uint64_t compute_remaining_ = 0;

private:
    std::vector<TraceRecord> traces_;
    std::uint64_t trace_load_start_    = 0;
    std::uint64_t trace_load_end_      = 0;
    std::uint64_t trace_compute_start_ = 0;
    std::uint64_t trace_compute_end_   = 0;
    std::uint64_t trace_store_start_   = 0;
    std::uint64_t trace_store_end_     = 0;
};

class RowStationaryEngine final : public SequentialTilingEngine {
public:
    explicit RowStationaryEngine(const HardwareParams& params);
    std::string name() const override;
};

class OutputStationaryEngine final : public SequentialTilingEngine {
public:
    explicit OutputStationaryEngine(const HardwareParams& params);
    std::string name() const override;
};

class InputStationaryEngine final : public SequentialTilingEngine {
public:
    explicit InputStationaryEngine(const HardwareParams& params);
    std::string name() const override;
};

class DoubleBufferEngine final : public TilingEngine {
public:
    explicit DoubleBufferEngine(const HardwareParams& params);

    std::string name() const override;
    void tick(DRAMModel& dram, Scratchpad& scratchpad, Metrics& metrics) override;
    bool done() const override;
    const std::vector<TraceRecord>& trace() const override;

private:
    // Ticks DRAM one cycle and ages the outstanding-load countdowns with it.
    void tick_dram(DRAMModel& dram);

    HardwareParams params_;
    std::vector<TileWork> work_;
    std::size_t index_ = 0;
    std::size_t prefetched_index_ = 0;
    bool current_ready_ = false;
    bool current_store_issued_ = false;
    std::uint64_t compute_remaining_ = 0;
    // Cycles until the current tile's load completes (0 = data resident).
    std::uint64_t cur_load_remaining_ = 0;
    // Cycles until the prefetched next tile's load completes.
    std::uint64_t next_load_remaining_ = 0;

    // Trace state
    std::vector<TraceRecord> traces_;
    std::uint64_t cur_load_start_      = 0;
    std::uint64_t cur_load_end_        = 0;
    std::uint64_t next_load_start_     = 0;
    std::uint64_t next_load_end_       = 0;
    std::uint64_t cur_compute_start_   = 0;
    std::uint64_t cur_compute_end_     = 0;
    std::uint64_t cur_store_start_     = 0;
    std::uint64_t cur_store_end_       = 0;
    bool          cur_compute_recorded_ = false;
};

std::unique_ptr<TilingEngine> make_strategy(const std::string& name, const HardwareParams& params);
