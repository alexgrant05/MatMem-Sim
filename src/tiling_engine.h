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
    std::size_t scratchpad_bytes = 0;
};

class TilingEngine {
public:
    virtual ~TilingEngine() = default;
    virtual std::string name() const = 0;
    virtual void tick(DRAMModel& dram, Scratchpad& scratchpad, Metrics& metrics) = 0;
    virtual bool done() const = 0;
};

class SequentialTilingEngine : public TilingEngine {
public:
    SequentialTilingEngine(HardwareParams params, std::vector<TileWork> work);

    void tick(DRAMModel& dram, Scratchpad& scratchpad, Metrics& metrics) override;
    bool done() const override;

protected:
    HardwareParams params_;
    std::vector<TileWork> work_;
    std::size_t index_ = 0;
    bool load_issued_ = false;
    bool store_issued_ = false;
    bool compute_started_ = false;
    std::uint64_t compute_remaining_ = 0;
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

class DoubleBufferEngine final : public TilingEngine {
public:
    explicit DoubleBufferEngine(const HardwareParams& params);

    std::string name() const override;
    void tick(DRAMModel& dram, Scratchpad& scratchpad, Metrics& metrics) override;
    bool done() const override;

private:
    HardwareParams params_;
    std::vector<TileWork> work_;
    std::size_t index_ = 0;
    std::size_t prefetched_index_ = 0;
    bool current_ready_ = false;
    bool current_store_issued_ = false;
    std::uint64_t compute_remaining_ = 0;
};

std::unique_ptr<TilingEngine> make_strategy(const std::string& name, const HardwareParams& params);
