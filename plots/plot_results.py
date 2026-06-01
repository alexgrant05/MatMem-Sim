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

    # Hardware ceiling values — read from first row (constant across sweep)
    compute_ops = int(df["compute_ops"].iloc[0])
    bandwidth = int(df["bandwidth"].iloc[0])

    ref_latency = 100
    subset = df[df["dram_latency"] == ref_latency]

    # ── Plot 1: Roofline with theoretical ceilings ────────────────────────────
    plt.figure(figsize=(8, 5))
    ai_range = [df["arithmetic_intensity"].min() * 0.8,
                df["arithmetic_intensity"].max() * 1.2]

    for strategy, group in subset.groupby("strategy"):
        plt.plot(group["arithmetic_intensity"], group["effective_ops_per_cycle"],
                 marker="o", label=strategy)

    # Memory-bound ceiling: ops/cycle = arithmetic_intensity × bandwidth
    ai_vals = pd.Series(ai_range)
    plt.plot(ai_vals, ai_vals * bandwidth, "k--", linewidth=1, label=f"memory ceiling ({bandwidth} B/cyc)")
    # Compute-bound ceiling: flat at compute_ops ops/cycle
    plt.axhline(compute_ops, color="gray", linestyle=":", linewidth=1,
                label=f"compute ceiling ({compute_ops} ops/cyc)")

    plt.xlabel("Arithmetic intensity (ops/byte)")
    plt.ylabel("Effective throughput (ops/cycle)")
    plt.title(f"Roofline comparison, DRAM latency {ref_latency} cycles")
    plt.grid(True, alpha=0.3)
    plt.legend()
    plt.tight_layout()
    plt.savefig(out_dir / "roofline.png", dpi=160)

    # ── Plot 2: Scratchpad Pareto ─────────────────────────────────────────────
    plt.figure(figsize=(8, 5))
    for strategy, group in subset.groupby("strategy"):
        plt.plot(group["scratchpad_kb"], group["compute_utilization"],
                 marker="o", label=strategy)
    plt.xlabel("Scratchpad size (KB)")
    plt.ylabel("Compute utilization")
    plt.title(f"Scratchpad Pareto, DRAM latency {ref_latency} cycles")
    plt.grid(True, alpha=0.3)
    plt.legend()
    plt.tight_layout()
    plt.savefig(out_dir / "scratchpad_pareto.png", dpi=160)

    # ── Plot 3: Stall breakdown ───────────────────────────────────────────────
    plt.figure(figsize=(8, 5))
    stall = subset.pivot(index="scratchpad_kb", columns="strategy", values="dram_stall_cycles")
    stall.plot(kind="bar", ax=plt.gca())
    plt.xlabel("Scratchpad size (KB)")
    plt.ylabel("DRAM stall cycles")
    plt.title(f"Stall breakdown, DRAM latency {ref_latency} cycles")
    plt.tight_layout()
    plt.savefig(out_dir / "stall_breakdown.png", dpi=160)

    # ── Plot 4: Latency sensitivity ───────────────────────────────────────────
    # One subplot per strategy; lines = DRAM latencies; x = scratchpad size
    strategies = sorted(df["strategy"].unique())
    latencies = sorted(df["dram_latency"].unique())
    fig, axes = plt.subplots(1, len(strategies), figsize=(5 * len(strategies), 4),
                             sharey=True)
    if len(strategies) == 1:
        axes = [axes]

    colors = {lat: c for lat, c in zip(latencies, ["tab:blue", "tab:orange", "tab:red"])}

    for ax, strategy in zip(axes, strategies):
        for latency in latencies:
            grp = df[(df["strategy"] == strategy) & (df["dram_latency"] == latency)]
            grp = grp.sort_values("scratchpad_kb")
            ax.plot(grp["scratchpad_kb"], grp["compute_utilization"],
                    marker="o", color=colors[latency], label=f"lat={latency}")
        ax.set_title(strategy.replace("_", " "))
        ax.set_xlabel("Scratchpad (KB)")
        ax.grid(True, alpha=0.3)
        ax.legend(fontsize=8)

    axes[0].set_ylabel("Compute utilization")
    fig.suptitle("Latency sensitivity: compute utilization vs scratchpad size")
    plt.tight_layout()
    plt.savefig(out_dir / "latency_sensitivity.png", dpi=160)

    print(f"wrote plots to {out_dir}")


if __name__ == "__main__":
    main()
