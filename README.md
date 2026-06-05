# MatMem-Sim

Cycle-accurate C++ simulator for studying how scratchpad tiling and dataflow choices affect matrix multiply accelerator performance.

Core question:

> Given a fixed scratchpad size, which tiling strategy best hides memory latency and maximizes compute utilization?

## Scope

This repository is scoped as a summer project (~8 weeks at ~8 hours per week).

It models:

- Fixed-latency, bandwidth-limited DRAM requests
- Finite scratchpad capacity with automatic tile-size selection
- Matrix multiply compute throughput
- Three tiling strategies: row stationary, output stationary, and double buffering
- Parameter sweeps over scratchpad size, DRAM latency, and bandwidth
- CSV output and plotting scripts (roofline, Pareto, stall breakdown, latency sensitivity)

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
.\build\matmem-sim.exe --strategy double_buffer --scratchpad-kb 32 --dram-latency 100
```

On multi-config generators (Visual Studio) the executable is at `.\build\Debug\matmem-sim.exe`.

## CLI Reference

| Flag | Default | Description |
|------|---------|-------------|
| `--strategy` | `row_stationary` | `row_stationary`, `output_stationary`, `double_buffer` |
| `--scratchpad-kb N` | `32` | Scratchpad capacity in KB |
| `--dram-latency N` | `100` | DRAM round-trip latency in cycles |
| `--bandwidth N` | `32` | DRAM bandwidth in bytes/cycle |
| `--compute-ops N` | `256` | Compute throughput in ops/cycle |
| `--matrix-m/n/k N` | `256` | Matrix dimensions |
| `--tile-m/n/k N` | `0` | Tile dimensions; `0` = auto-size to scratchpad |
| `--csv` | off | Emit a single CSV row instead of human-readable output |

### Auto tile sizing

When `--tile-m/n/k` are left at `0` (the default), the simulator picks the largest square tile that fits in the scratchpad given the strategy's buffer requirements:

- Row stationary and output stationary: one tile must fit (A + B + C = 3t²·elem ≤ scratchpad)
- Double buffering: two tiles must fit simultaneously (6t²·elem ≤ scratchpad)

This means larger scratchpads use larger tiles, which is what makes the Pareto plots meaningful.

## Run Sweeps

```powershell
python scripts\run_sweep.py
python plots\plot_results.py results\sweep.csv
```

The default sweep covers 3 strategies × 6 scratchpad sizes × 3 DRAM latencies × 4 bandwidths = **216 simulations**. Pass `--matrix-m/n/k` to the sweep script to change matrix dimensions.

The plot script generates six figures in `results/`:

| File | Description |
|------|-------------|
| `roofline.png` | Arithmetic intensity vs throughput with compute-bound and memory-bound ceilings |
| `scratchpad_pareto.png` | Compute utilization vs scratchpad size |
| `stall_breakdown.png` | DRAM stall cycles by strategy and scratchpad size |
| `latency_sensitivity.png` | Compute utilization vs scratchpad at each DRAM latency, one panel per strategy |
| `bandwidth_sensitivity.png` | Compute utilization vs scratchpad at each DRAM bandwidth, one panel per strategy |
| `energy_efficiency.png` | Energy efficiency (ops/pJ) vs scratchpad size per strategy |

## Metrics

The simulator emits:

| Metric | Description |
|--------|-------------|
| `total_cycles` | Total simulated cycles |
| `compute_cycles` | Cycles spent computing |
| `dram_stall_cycles` | Cycles stalled waiting for DRAM |
| `dram_bytes` | Total bytes transferred to/from DRAM |
| `compute_utilization` | `compute_cycles / total_cycles` |
| `arithmetic_intensity` | `operations / dram_bytes` (ops/byte) |
| `effective_ops_per_cycle` | `operations / total_cycles` |
| `energy_pj` | Total energy in picojoules: DRAM + SRAM + MAC (see energy model below) |
| `ops_per_pj` | Energy efficiency: `operations / energy_pj` |

## Energy Model

Energy is computed post-simulation from three components using constants from Horowitz ISSCC 2014:

```
operations = 2 × MACs  (stored as FLOPs; one MAC = one multiply + one add)

sram_accesses = dram_bytes / element_bytes   (scratchpad I/O in elements)
              + operations                   (2 operand reads per MAC × ops/2 MACs = ops)

energy_pj = dram_bytes        × 200 pJ/byte     (DDR4 off-chip access)
          + sram_accesses     × 5.0 pJ/access   (on-chip SRAM read or write, per element)
          + (operations / 2)  × 3.7 pJ/MAC      (ops/2 = number of MACs)
```

Key finding: **double buffering improves compute utilization but often reduces energy efficiency** relative to output stationary. The smaller tiles required for dual-buffer prefetching increase total DRAM traffic, and DRAM energy dominates the budget.

## Tiling Strategies

**Row stationary** — keeps tile A(mi, ki) resident in the scratchpad and streams B and C across the N dimension. Loop order: `for mi → for ki → for ni`. A is loaded once per (mi, ki) pair.

**Output stationary** — keeps tile C(mi, ni) resident and accumulates partial sums across all K tiles without writing C back to DRAM between k-iterations. Minimises C traffic at the cost of streaming A and B every k-step.

**Double buffering** — uses output stationary tile ordering but prefetches the next tile's data from DRAM while the current tile is computing. Requires the scratchpad to hold two tiles simultaneously. When the scratchpad is too small for two tiles, it falls back to sequential output stationary behaviour.

## Architecture Notes

Double buffering only improves utilization when the scratchpad can hold two working tiles at once. With auto tile sizing, the double buffer strategy automatically sizes its tiles to half the scratchpad so prefetching is always active.

Row stationary minimises A DRAM traffic but reloads C on every k-iteration, making output stationary generally better for square matrices. Row stationary becomes competitive when the N dimension is large relative to K, amortising A loads over many output columns.
