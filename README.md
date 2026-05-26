# MatMem-Sim

Cycle-accurate C++ simulator for studying how scratchpad tiling and dataflow choices affect matrix multiply accelerator performance.

Core question:

> Given a fixed scratchpad size, which tiling strategy best hides memory latency and maximizes compute utilization?

## Scope

This repository is intentionally scoped for an 8 to 10 week summer project at about 8 hours per week.

It models:

- Fixed-latency, bandwidth-limited DRAM requests
- Finite scratchpad capacity and simple tile residency checks
- Matrix multiply compute throughput
- Three tiling strategies: row stationary, output stationary, and simplified double buffering
- Parameter sweeps over scratchpad size and DRAM latency
- CSV output and plotting scripts

It does not model RTL, FPGA implementation, multithreading, cache coherence, full DRAM timing standards, or neural network framework integration.

## Repository Layout

```text
matmem-sim/
  src/        C++ simulator implementation
  tests/      smoke tests
  scripts/    parameter sweep entry points
  plots/      plotting utilities
  results/    generated CSVs and figures
```

## Build

```powershell
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## Run One Simulation

```powershell
.\build\Debug\matmem-sim.exe --strategy double_buffer --scratchpad-kb 32 --dram-latency 100
```

On single-config generators, the executable may be at:

```powershell
.\build\matmem-sim.exe
```

## Run Sweeps

```powershell
python scripts\run_sweep.py
python plots\plot_results.py results\sweep.csv
```

The sweep covers scratchpad sizes `4, 8, 16, 32, 64, 128 KB` and DRAM latencies `50, 100, 200 cycles` for all three strategies.

## Metrics

The simulator emits:

- `total_cycles`
- `compute_cycles`
- `dram_stall_cycles`
- `dram_bytes`
- `compute_utilization`
- `arithmetic_intensity`
- `effective_gops`

## Architecture Notes

Double buffering only improves utilization when the scratchpad can hold two working tiles at once. Below that threshold, overlap collapses and the run becomes memory-bound.

The starter model is deliberately simple and inspectable. The next useful improvements are configurable tile sizes, richer scratchpad banking, and timeline trace output.
