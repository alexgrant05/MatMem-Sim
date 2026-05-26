from __future__ import annotations

import sys
from pathlib import Path

import matplotlib

matplotlib.use("Agg")

import matplotlib.pyplot as plt
import pandas as pd


def main() -> None:
    csv_path = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("results/sweep.csv")
    df = pd.read_csv(csv_path)
    out_dir = csv_path.parent

    df["scratchpad_kb"] = df["scratchpad_kb"].astype(int)
    df["dram_latency"] = df["dram_latency"].astype(int)

    latency = 100
    subset = df[df["dram_latency"] == latency]

    plt.figure(figsize=(8, 5))
    for strategy, group in subset.groupby("strategy"):
        plt.plot(group["arithmetic_intensity"], group["effective_gops"], marker="o", label=strategy)
    plt.xlabel("Arithmetic intensity (ops/byte)")
    plt.ylabel("Effective throughput (GOPS/cycle-scaled)")
    plt.title(f"Roofline-style comparison, DRAM latency {latency} cycles")
    plt.grid(True, alpha=0.3)
    plt.legend()
    plt.tight_layout()
    plt.savefig(out_dir / "roofline.png", dpi=160)

    plt.figure(figsize=(8, 5))
    for strategy, group in subset.groupby("strategy"):
        plt.plot(group["scratchpad_kb"], group["compute_utilization"], marker="o", label=strategy)
    plt.xlabel("Scratchpad size (KB)")
    plt.ylabel("Compute utilization")
    plt.title(f"Scratchpad Pareto, DRAM latency {latency} cycles")
    plt.grid(True, alpha=0.3)
    plt.legend()
    plt.tight_layout()
    plt.savefig(out_dir / "scratchpad_pareto.png", dpi=160)

    plt.figure(figsize=(8, 5))
    stall = subset.pivot(index="scratchpad_kb", columns="strategy", values="dram_stall_cycles")
    stall.plot(kind="bar", ax=plt.gca())
    plt.xlabel("Scratchpad size (KB)")
    plt.ylabel("DRAM stall cycles")
    plt.title(f"Stall breakdown, DRAM latency {latency} cycles")
    plt.tight_layout()
    plt.savefig(out_dir / "stall_breakdown.png", dpi=160)

    print(f"wrote plots to {out_dir}")


if __name__ == "__main__":
    main()
