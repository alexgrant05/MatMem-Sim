# MatMem-Sim

Cycle-accurate C++ simulator for studying how scratchpad tiling and dataflow choices affect matrix multiply accelerator performance.

Core question:

> Given a fixed scratchpad size, which tiling strategy best hides memory latency and maximizes compute utilization?

## Scope

It models:

- Fixed-latency, bandwidth-limited DRAM requests
- Finite scratchpad capacity and configurable pipeline-visible scratchpad latency
- Matrix multiply compute throughput
- Four tiling strategies: row stationary, output stationary, input stationary, and double buffering
- Parameter sweeps over scratchpad size, DRAM latency, and bandwidth
- CSV output, per-tile traces, and plotting scripts (roofline, Pareto, stall breakdown, latency sensitivity, Gantt)

It does not model RTL behavior, FPGA implementation effects, multithreading, cache coherence, detailed DRAM timing standards, or integration with machine learning frameworks.

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

## CLI Reference

| Flag | Default | Description |
|------|---------|-------------|
| `--strategy` | `row_stationary` | `auto`, `row_stationary`, `output_stationary`, `input_stationary`, `double_buffer` |
| `--scratchpad-kb N` | `32` | Scratchpad capacity in KB |
| `--scratchpad-latency N` | `1` | Pipeline-visible scratchpad access latency in cycles; `0` disables this delay |
| `--dram-latency N` | `100` | DRAM round-trip latency in cycles |
| `--bandwidth N` | `32` | DRAM bandwidth in bytes/cycle |
| `--compute-ops N` | `256` | Compute throughput in ops/cycle |
| `--element-bytes N` | `4` | Bytes per matrix element |
| `--matrix-m/n/k N` | `256` | Matrix dimensions |
| `--tile-m/n/k N` | `0` | Tile dimensions; `0` = auto-size to scratchpad |
| `--tune-objective NAME` | `cycles` | Auto-tuner objective: `cycles`, `energy`, or `dram_bytes` |
| `--tune-budget N` | `256` | Maximum exact simulations during tuning; minimum `4` |
| `--csv` | off | Emit a single CSV row instead of human-readable output |
| `--trace` | off | Print per-tile load, compute, and store ranges for Gantt plots |

### Auto tile sizing

For a named strategy, leaving `--tile-m/n/k` at `0` preserves the original fast heuristic. The simulator picks the largest square tile that fits the strategy's buffer requirements:

- Row stationary, output stationary, and input stationary: one tile must fit (A + B + C = 3t²·elem ≤ scratchpad)
- Double buffering: two tiles must fit simultaneously (6t²·elem ≤ scratchpad)

This means larger scratchpads use larger tiles, which is what makes the Pareto plots meaningful.

### Global auto-tuner

Use `--strategy auto` to search tile shapes across all four strategies:

```powershell
.\build\matmem-sim.exe --strategy auto --matrix-m 256 --matrix-n 256 --matrix-k 256
.\build\matmem-sim.exe --strategy auto --tune-objective energy --tune-budget 512
```

The tuner starts with capacity-aware coarse shapes and then refines the strongest candidates. It is deterministic for a fixed configuration and budget. A larger budget can find a better shape but requires more simulator runs. The default objective minimizes total cycles; `energy` minimizes modeled energy and `dram_bytes` minimizes off-chip traffic.

When all three tile dimensions are supplied with `--strategy auto`, the tuner keeps that shape and compares only the strategies. Partial tile specifications are invalid. Named strategies do not accept tuning flags, so tuning options cannot be ignored accidentally.

The reusable `tune_configuration()` C++ API returns the winning strategy, explicit tile dimensions, metrics, energy, and search statistics. It is intended to be the simulator entry point for a future DNN workload driver.

## Run Sweeps

```powershell
python scripts\run_sweep.py
python plots\plot_results.py results\sweep.csv
```

The default sweep covers 4 strategies × 6 scratchpad sizes × 3 DRAM latencies × 4 bandwidths = **288 simulations**. Pass `--matrix-m/n/k` to change matrix dimensions, or `--scratchpad-latency` to use a different fixed scratchpad latency for every run.

To sweep the global tuner instead of the four fixed strategies:

```powershell
python scripts\run_sweep.py --strategy auto --out results\auto_sweep.csv
python plots\plot_results.py results\auto_sweep.csv
```

Auto sweep plots use the CSV stem as a filename prefix, so `auto_sweep.csv` writes files such as `auto_sweep_roofline.png` without replacing the regular sweep figures. You can also pass `--tune-objective` and `--tune-budget` through the sweep script.

The plot script generates six figures in `results/`:

| File | Description |
|------|-------------|
| `roofline.png` | Arithmetic intensity vs throughput with compute-bound and memory-bound ceilings |
| `scratchpad_pareto.png` | Compute utilization vs scratchpad size |
| `stall_breakdown.png` | DRAM stall cycles by strategy and scratchpad size |
| `latency_sensitivity.png` | Compute utilization vs scratchpad at each DRAM latency, one panel per strategy |
| `bandwidth_sensitivity.png` | Compute utilization vs scratchpad at each DRAM bandwidth, one panel per strategy |
| `energy_efficiency.png` | Energy efficiency (ops/pJ) vs scratchpad size per strategy |

For tuned CSVs, the same six plots are generated with the CSV stem prefix. The plot script also adds tuner-specific summaries:

| File | Description |
|------|-------------|
| `auto_sweep_auto_strategy_selection.png` | Winning strategy vs scratchpad size for the reference latency/bandwidth slice |
| `auto_sweep_auto_tile_shape.png` | Selected tile M/N/K dimensions vs scratchpad size |
| `auto_sweep_auto_search_effort.png` | Evaluated and rejected candidate counts vs scratchpad size |

Sweep CSVs are committed as lightweight reference data. Plot PNGs are generated from those CSVs and ignored by git to keep the repository small.

## Run GEMM Workloads

For a model-like sequence of independent GEMMs, use the workload driver:

```powershell
python scripts\run_workload.py examples\gemm_workload.csv --out results\gemm_workload.csv
```

The input CSV has four required columns:

```csv
name,m,n,k
attention_qkv,256,768,768
mlp_up,256,3072,768
```

The driver defaults to `--strategy auto`, so each layer is tuned independently and the output CSV records the concrete winning strategy and tile shape. Hardware flags such as `--scratchpad-kb`, `--scratchpad-latency`, `--dram-latency`, `--bandwidth`, `--compute-ops`, and `--element-bytes` apply to every layer. `--tune-objective` and `--tune-budget` pass through to the auto-tuner.

The workload CSV adds `layer_index` and `layer_name` in front of the normal simulator metrics. The script also prints total cycles, compute cycles, DRAM and scratchpad stall cycles, energy, DRAM bytes, overall throughput, and strategy counts.

It writes three plots next to the output CSV:

| File | Description |
|------|-------------|
| `gemm_workload_cycles_by_layer.png` | Total cycles for each GEMM layer |
| `gemm_workload_energy_by_layer.png` | Energy for each GEMM layer |
| `gemm_workload_strategy_distribution.png` | Count of layers won by each strategy |

This is the intended bridge toward future Conv2D support: a Conv2D layer can later be lowered into one or more GEMM rows and sent through the same driver.

## Gantt Traces

Gantt charts are generated separately from the sweep plots using the simulator's `--trace` output:

```powershell
python plots\plot_gantt.py
```

By default this writes one Gantt chart per strategy to `results/`. Use `--strategy`, `--matrix-m/n/k`, `--tile-m/n/k`, and `--max-tiles` to focus on a smaller case when comparing schedules.

The trace bars show DRAM load, compute, and DRAM store intervals. Gaps between those bars can represent the configured scratchpad pipeline delay; that delay is reported numerically as `scratchpad_stall_cycles`.

## Metrics

The simulator emits:

| Metric | Description |
|--------|-------------|
| `total_cycles` | Total simulated cycles |
| `compute_cycles` | Cycles spent computing |
| `dram_stall_cycles` | Cycles stalled waiting for DRAM |
| `scratchpad_stall_cycles` | Pipeline-visible scratchpad delay before compute and before a nonzero C writeback |
| `dram_bytes` | Total bytes transferred to/from DRAM |
| `compute_utilization` | `compute_cycles / total_cycles` |
| `arithmetic_intensity` | `operations / dram_bytes` (ops/byte) |
| `effective_ops_per_cycle` | `operations / total_cycles` |
| `energy_pj` | Total energy in picojoules: DRAM + SRAM + MAC (see energy model below) |
| `ops_per_pj` | Energy efficiency: `operations / energy_pj` |
| `a_reuse_factor` | Logical A tile demand divided by actual A DRAM load bytes |
| `b_reuse_factor` | Logical B tile demand divided by actual B DRAM load bytes |
| `c_reuse_factor` | Logical C tile demand divided by actual C DRAM load bytes |

Tuned CSV rows also include `tuned`, `tune_objective`, the selected `tile_m/n/k`, and considered, evaluated, and rejected candidate counts. The `strategy` field contains the concrete winning strategy rather than `auto`. Normal rows retain zero tile values when a named strategy uses the square heuristic.

Cycle accounting is exact for a completed simulation:

```
total_cycles = compute_cycles + dram_stall_cycles + scratchpad_stall_cycles
```

Scratchpad latency is modeled as a pipelined per-tile visibility delay, not as a serialized delay for every MAC. In double-buffer mode, the successor tile's DRAM prefetch starts once the current tile is resident and can overlap that current tile's scratchpad delay and compute.

Reuse factors count loads only. C writebacks still contribute to `dram_bytes` and energy, but they are not included in `c_reuse_factor`.

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

Scratchpad latency changes timing but does not add a separate energy term; SRAM energy remains an access-count model.

Key finding: **double buffering improves compute utilization but often reduces energy efficiency** relative to output stationary. The smaller tiles required for dual-buffer prefetching increase total DRAM traffic, and DRAM energy dominates the budget.

## Tiling Strategies

**Row stationary**: keeps a row stripe of output C tiles resident across K while reusing A across the N tiles in that stripe. This reduces intermediate C traffic and makes the row schedule distinct from pure input reuse.

**Output stationary**: keeps tile C(mi, ni) resident and accumulates partial sums across all K tiles without writing C back to DRAM between k-iterations. Minimises C traffic at the cost of streaming A and B every k-step.

**Input stationary**: keeps tile A(mi, ki), the input/activation tile, resident while streaming B and C across the N dimension. C is loaded and stored for each tile because this strategy does not retain output partial sums across K.

**Double buffering**: uses output stationary tile ordering but prefetches the next tile's data from DRAM as soon as the current tile is resident, overlapping the current tile's scratchpad delay and compute when capacity permits. It requires the scratchpad to hold two working tile footprints simultaneously. When the next pair does not fit, it falls back to sequential output-stationary behavior for that transition.

## Architecture Notes

Double buffering only improves utilization when the scratchpad can hold the current and successor working tiles at once. With auto tile sizing, the double-buffer strategy chooses a square tile sized for two full tile footprints, so full-tile transitions are capacity-safe for prefetching.

Row stationary reduces intermediate C traffic by holding a row stripe of outputs across K. Input stationary instead focuses on activation reuse, so its Gantt charts should show A reuse but more frequent C traffic than row stationary.
