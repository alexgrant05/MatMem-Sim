from __future__ import annotations

import argparse
import csv
import subprocess
from collections import Counter
from pathlib import Path
from typing import Iterable

import matplotlib

matplotlib.use("Agg")

import matplotlib.pyplot as plt


ROOT = Path(__file__).resolve().parents[1]
RESULTS = ROOT / "results"
BUILD_EXES = [
    ROOT / "build" / "Debug" / "matmem-sim.exe",
    ROOT / "build" / "Release" / "matmem-sim.exe",
    ROOT / "build" / "matmem-sim.exe",
    ROOT / "build" / "matmem-sim",
]
REQUIRED_COLUMNS = ("name", "m", "n", "k")
STRATEGY_COLORS = {
    "row_stationary": "tab:blue",
    "output_stationary": "tab:orange",
    "input_stationary": "tab:green",
    "double_buffer": "tab:red",
}


def find_executable(explicit: Path | None) -> Path:
    if explicit is not None:
        if explicit.exists():
            return explicit
        raise SystemExit(f"matmem-sim executable not found: {explicit}")
    for path in BUILD_EXES:
        if path.exists():
            return path
    raise SystemExit("Could not find built matmem-sim executable. Run CMake build first.")


def parse_uint(value: str, field: str, row_index: int) -> int:
    if value is None or value == "":
        raise SystemExit(f"row {row_index}: missing value for {field}")
    if value.startswith("-"):
        raise SystemExit(f"row {row_index}: {field} must be non-negative")
    try:
        parsed = int(value)
    except ValueError as exc:
        raise SystemExit(f"row {row_index}: {field} must be a non-negative integer") from exc
    if parsed < 0:
        raise SystemExit(f"row {row_index}: {field} must be non-negative")
    return parsed


def load_layers(path: Path) -> list[dict[str, int | str]]:
    with path.open(newline="") as f:
        reader = csv.DictReader(f)
        if reader.fieldnames is None:
            raise SystemExit("workload CSV is empty")
        missing = [column for column in REQUIRED_COLUMNS if column not in reader.fieldnames]
        if missing:
            raise SystemExit("workload CSV missing required column(s): " + ", ".join(missing))

        layers: list[dict[str, int | str]] = []
        for row_index, row in enumerate(reader, start=1):
            name = (row.get("name") or "").strip()
            if not name:
                raise SystemExit(f"row {row_index}: missing value for name")
            layers.append({
                "name": name,
                "m": parse_uint((row.get("m") or "").strip(), "m", row_index),
                "n": parse_uint((row.get("n") or "").strip(), "n", row_index),
                "k": parse_uint((row.get("k") or "").strip(), "k", row_index),
            })

    if not layers:
        raise SystemExit("workload CSV has no layers")
    return layers


def run_layer(exe: Path, args: argparse.Namespace, layer: dict[str, int | str]) -> dict[str, str]:
    cmd = [
        str(exe),
        "--strategy", args.strategy,
        "--scratchpad-kb", str(args.scratchpad_kb),
        "--scratchpad-latency", str(args.scratchpad_latency),
        "--dram-latency", str(args.dram_latency),
        "--bandwidth", str(args.bandwidth),
        "--compute-ops", str(args.compute_ops),
        "--element-bytes", str(args.element_bytes),
        "--matrix-m", str(layer["m"]),
        "--matrix-n", str(layer["n"]),
        "--matrix-k", str(layer["k"]),
        "--csv",
    ]
    if args.strategy == "auto":
        cmd.extend(["--tune-objective", args.tune_objective,
                    "--tune-budget", str(args.tune_budget)])

    try:
        result = subprocess.run(cmd, check=True, text=True, capture_output=True)
    except subprocess.CalledProcessError as exc:
        message = exc.stderr.strip() or exc.stdout.strip() or str(exc)
        raise SystemExit(f"simulation failed for layer {layer['name']}: {message}") from exc

    reader = csv.DictReader(result.stdout.splitlines())
    try:
        return next(reader)
    except StopIteration as exc:
        raise SystemExit(f"simulation produced no CSV row for layer {layer['name']}") from exc


def write_results(path: Path, rows: list[dict[str, str]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fieldnames = ["layer_index", "layer_name", *[f for f in rows[0].keys()
                                                  if f not in {"layer_index", "layer_name"}]]
    with path.open("w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)


def plot_bar(labels: list[str], values: Iterable[float], ylabel: str, title: str, path: Path) -> None:
    plt.figure(figsize=(max(7, len(labels) * 1.2), 4.8))
    plt.bar(labels, list(values), color="tab:blue")
    plt.xlabel("Layer")
    plt.ylabel(ylabel)
    plt.title(title)
    plt.xticks(rotation=30, ha="right")
    plt.grid(True, axis="y", alpha=0.3)
    plt.tight_layout()
    plt.savefig(path, dpi=160)
    plt.close()


def plot_strategy_distribution(counts: Counter[str], path: Path) -> None:
    strategies = list(counts.keys())
    values = [counts[s] for s in strategies]
    colors = [STRATEGY_COLORS.get(s, "tab:gray") for s in strategies]
    plt.figure(figsize=(7, 4.8))
    plt.bar([s.replace("_", " ") for s in strategies], values, color=colors)
    plt.xlabel("Winning strategy")
    plt.ylabel("Layer count")
    plt.title("Workload strategy distribution")
    plt.xticks(rotation=20, ha="right")
    plt.grid(True, axis="y", alpha=0.3)
    plt.tight_layout()
    plt.savefig(path, dpi=160)
    plt.close()


def write_plots(out_csv: Path, rows: list[dict[str, str]]) -> None:
    labels = [row["layer_name"] for row in rows]
    stem = out_csv.with_suffix("")
    plot_bar(labels,
             [float(row["total_cycles"]) for row in rows],
             "Cycles",
             "Workload cycles by layer",
             stem.parent / f"{stem.name}_cycles_by_layer.png")
    plot_bar(labels,
             [float(row["energy_pj"]) for row in rows],
             "Energy (pJ)",
             "Workload energy by layer",
             stem.parent / f"{stem.name}_energy_by_layer.png")
    plot_strategy_distribution(Counter(row["strategy"] for row in rows),
                               stem.parent / f"{stem.name}_strategy_distribution.png")


def print_summary(rows: list[dict[str, str]]) -> None:
    total_cycles = sum(int(row["total_cycles"]) for row in rows)
    total_compute_cycles = sum(int(row["compute_cycles"]) for row in rows)
    total_dram_stall_cycles = sum(int(row["dram_stall_cycles"]) for row in rows)
    total_scratchpad_stall_cycles = sum(int(row["scratchpad_stall_cycles"]) for row in rows)
    total_dram_bytes = sum(int(row["dram_bytes"]) for row in rows)
    total_energy_pj = sum(float(row["energy_pj"]) for row in rows)
    total_ops = sum(2 * int(row["matrix_m"]) * int(row["matrix_n"]) * int(row["matrix_k"])
                    for row in rows)
    effective_ops_per_cycle = total_ops / total_cycles if total_cycles else 0.0
    ops_per_pj = total_ops / total_energy_pj if total_energy_pj else 0.0

    print(f"layers: {len(rows)}")
    print(f"total_cycles: {total_cycles}")
    print(f"total_compute_cycles: {total_compute_cycles}")
    print(f"total_dram_stall_cycles: {total_dram_stall_cycles}")
    print(f"total_scratchpad_stall_cycles: {total_scratchpad_stall_cycles}")
    print(f"total_dram_bytes: {total_dram_bytes}")
    print(f"total_energy_pj: {total_energy_pj:.4f}")
    print(f"effective_ops_per_cycle: {effective_ops_per_cycle:.4f}")
    print(f"ops_per_pj: {ops_per_pj:.6f}")
    strategy_counts = Counter(row["strategy"] for row in rows)
    print("strategy_counts: " + ", ".join(f"{name}={count}"
                                          for name, count in strategy_counts.items()))


def main() -> None:
    parser = argparse.ArgumentParser(description="Run a GEMM workload through matmem-sim")
    parser.add_argument("workload", type=Path, help="CSV with required columns: name,m,n,k")
    parser.add_argument("--out", type=Path, default=RESULTS / "gemm_workload.csv")
    parser.add_argument("--exe", type=Path, default=None, help="path to matmem-sim executable")
    parser.add_argument(
        "--strategy",
        choices=["auto", "row_stationary", "output_stationary", "input_stationary", "double_buffer"],
        default="auto",
    )
    parser.add_argument("--scratchpad-kb", type=int, default=32)
    parser.add_argument("--scratchpad-latency", type=int, default=1)
    parser.add_argument("--dram-latency", type=int, default=100)
    parser.add_argument("--bandwidth", type=int, default=32)
    parser.add_argument("--compute-ops", type=int, default=256)
    parser.add_argument("--element-bytes", type=int, default=4)
    parser.add_argument(
        "--tune-objective",
        choices=["cycles", "energy", "dram_bytes"],
        default=None,
        help="objective for --strategy auto",
    )
    parser.add_argument("--tune-budget", type=int, default=None)
    args = parser.parse_args()

    tune_objective_set = args.tune_objective is not None
    tune_budget_set = args.tune_budget is not None
    if args.strategy != "auto" and (tune_objective_set or tune_budget_set):
        parser.error("--tune-objective and --tune-budget require --strategy auto")
    if args.tune_objective is None:
        args.tune_objective = "cycles"
    if args.tune_budget is None:
        args.tune_budget = 256

    exe = find_executable(args.exe)
    layers = load_layers(args.workload)

    rows: list[dict[str, str]] = []
    for index, layer in enumerate(layers):
        sim_row = run_layer(exe, args, layer)
        rows.append({
            "layer_index": str(index),
            "layer_name": str(layer["name"]),
            **sim_row,
        })
        print(f"  [{index + 1}/{len(layers)}] {layer['name']} "
              f"{layer['m']}x{layer['n']}x{layer['k']} -> {sim_row['strategy']}",
              flush=True)

    write_results(args.out, rows)
    write_plots(args.out, rows)
    print_summary(rows)
    print(f"wrote {args.out}")


if __name__ == "__main__":
    main()
