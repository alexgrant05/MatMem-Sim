from __future__ import annotations

import argparse
from pathlib import Path

import matplotlib

matplotlib.use("Agg")

import matplotlib.pyplot as plt
import pandas as pd


# Reference slices used by most plots
REF_LATENCY = 100
REF_BANDWIDTH = 32

# Per-strategy line styles. On a square problem input_stationary coincides
# exactly with row_stationary (M<->N symmetry), so it is drawn last with a
# dashed line and distinct marker — otherwise the solid row line drawn on top
# would hide it completely in the overlay plots.
STYLES = {
    "row_stationary":    dict(linestyle="-", marker="o"),
    "output_stationary": dict(linestyle="-", marker="s"),
    "double_buffer":     dict(linestyle="-", marker="^"),
    "input_stationary":  dict(linestyle="--", marker="x", linewidth=2),
}
DEFAULT_STYLE = dict(linestyle="-", marker="o")
# Draw order for overlay plots: input_stationary last so it sits on top.
DRAW_ORDER = ["row_stationary", "output_stationary", "double_buffer", "input_stationary"]
STRATEGY_COLORS = {
    "row_stationary": "tab:blue",
    "output_stationary": "tab:orange",
    "input_stationary": "tab:green",
    "double_buffer": "tab:red",
}


def overlay_groups(frame):
    """Yield (strategy, group) in DRAW_ORDER, with any unknown strategies last."""
    known = [s for s in DRAW_ORDER if s in set(frame["strategy"])]
    extra = [s for s in frame["strategy"].unique() if s not in DRAW_ORDER]
    for strategy in known + extra:
        yield strategy, frame[frame["strategy"] == strategy]


def tuned_rows(frame):
    if "tuned" not in frame.columns:
        return frame.iloc[0:0].copy()
    return frame[frame["tuned"].astype(str) == "1"].copy()


def main() -> None:
    parser = argparse.ArgumentParser(description="Plot matmem-sim sweep CSV results")
    parser.add_argument("csv_path", nargs="?", type=Path, default=Path("results/sweep.csv"))
    parser.add_argument(
        "--prefix",
        default=None,
        help="prefix for output filenames; defaults to none for sweep.csv and CSV stem otherwise",
    )
    args = parser.parse_args()

    csv_path = args.csv_path
    df = pd.read_csv(csv_path)
    out_dir = csv_path.parent
    prefix = args.prefix
    if prefix is None:
        prefix = "" if csv_path.name == "sweep.csv" else f"{csv_path.stem}_"

    def out(name: str) -> Path:
        return out_dir / f"{prefix}{name}"

    df["scratchpad_kb"] = df["scratchpad_kb"].astype(int)
    df["dram_latency"] = df["dram_latency"].astype(int)
    df["bandwidth"] = df["bandwidth"].astype(int)

    # Hardware ceiling values — read from the reference slice
    ref = df[(df["dram_latency"] == REF_LATENCY) & (df["bandwidth"] == REF_BANDWIDTH)]
    compute_ops = int(ref["compute_ops"].iloc[0])

    # ── Plot 1: Roofline with theoretical ceilings ────────────────────────────
    plt.figure(figsize=(8, 5))
    ai_range = [ref["arithmetic_intensity"].min() * 0.8,
                ref["arithmetic_intensity"].max() * 1.2]

    for strategy, group in overlay_groups(ref):
        plt.plot(group["arithmetic_intensity"], group["effective_ops_per_cycle"],
                 label=strategy, **STYLES.get(strategy, DEFAULT_STYLE))

    ai_vals = pd.Series(ai_range)
    plt.plot(ai_vals, ai_vals * REF_BANDWIDTH, "k--", linewidth=1,
             label=f"memory ceiling ({REF_BANDWIDTH} B/cyc)")
    plt.axhline(compute_ops, color="gray", linestyle=":", linewidth=1,
                label=f"compute ceiling ({compute_ops} ops/cyc)")

    plt.xlabel("Arithmetic intensity (ops/byte)")
    plt.ylabel("Effective throughput (ops/cycle)")
    plt.title(f"Roofline comparison — latency {REF_LATENCY} cyc, bandwidth {REF_BANDWIDTH} B/cyc")
    plt.grid(True, alpha=0.3)
    plt.legend()
    plt.tight_layout()
    plt.savefig(out("roofline.png"), dpi=160)

    # ── Plot 2: Scratchpad Pareto ─────────────────────────────────────────────
    plt.figure(figsize=(8, 5))
    for strategy, group in overlay_groups(ref):
        plt.plot(group["scratchpad_kb"], group["compute_utilization"],
                 label=strategy, **STYLES.get(strategy, DEFAULT_STYLE))
    plt.xlabel("Scratchpad size (KB)")
    plt.ylabel("Compute utilization")
    plt.title(f"Scratchpad Pareto — latency {REF_LATENCY} cyc, bandwidth {REF_BANDWIDTH} B/cyc")
    plt.grid(True, alpha=0.3)
    plt.legend()
    plt.tight_layout()
    plt.savefig(out("scratchpad_pareto.png"), dpi=160)

    # ── Plot 3: Stall breakdown ───────────────────────────────────────────────
    plt.figure(figsize=(8, 5))
    stall = ref.pivot(index="scratchpad_kb", columns="strategy", values="dram_stall_cycles")
    stall.plot(kind="bar", ax=plt.gca())
    plt.xlabel("Scratchpad size (KB)")
    plt.ylabel("DRAM stall cycles")
    plt.title(f"Stall breakdown — latency {REF_LATENCY} cyc, bandwidth {REF_BANDWIDTH} B/cyc")
    plt.tight_layout()
    plt.savefig(out("stall_breakdown.png"), dpi=160)

    # ── Plot 4: Latency sensitivity ───────────────────────────────────────────
    lat_df = df[df["bandwidth"] == REF_BANDWIDTH]
    strategies = sorted(lat_df["strategy"].unique())
    latencies = sorted(lat_df["dram_latency"].unique())
    fig, axes = plt.subplots(1, len(strategies), figsize=(5 * len(strategies), 4), sharey=True)
    if len(strategies) == 1:
        axes = [axes]

    lat_colors = {lat: c for lat, c in zip(latencies, ["tab:blue", "tab:orange", "tab:red"])}
    for ax, strategy in zip(axes, strategies):
        for latency in latencies:
            grp = lat_df[(lat_df["strategy"] == strategy) & (lat_df["dram_latency"] == latency)]
            grp = grp.sort_values("scratchpad_kb")
            ax.plot(grp["scratchpad_kb"], grp["compute_utilization"],
                    marker="o", color=lat_colors[latency], label=f"lat={latency}")
        ax.set_title(strategy.replace("_", " "))
        ax.set_xlabel("Scratchpad (KB)")
        ax.grid(True, alpha=0.3)
        ax.legend(fontsize=8)

    axes[0].set_ylabel("Compute utilization")
    fig.suptitle(f"Latency sensitivity — bandwidth {REF_BANDWIDTH} B/cyc")
    plt.tight_layout()
    plt.savefig(out("latency_sensitivity.png"), dpi=160)

    # ── Plot 5: Bandwidth sensitivity ─────────────────────────────────────────
    bw_df = df[df["dram_latency"] == REF_LATENCY]
    bandwidths = sorted(bw_df["bandwidth"].unique())
    fig, axes = plt.subplots(1, len(strategies), figsize=(5 * len(strategies), 4), sharey=True)
    if len(strategies) == 1:
        axes = [axes]

    bw_colors = {bw: c for bw, c in zip(bandwidths, ["tab:blue", "tab:green", "tab:orange", "tab:red"])}
    for ax, strategy in zip(axes, strategies):
        for bw in bandwidths:
            grp = bw_df[(bw_df["strategy"] == strategy) & (bw_df["bandwidth"] == bw)]
            grp = grp.sort_values("scratchpad_kb")
            ax.plot(grp["scratchpad_kb"], grp["compute_utilization"],
                    marker="o", color=bw_colors[bw], label=f"{bw} B/cyc")
        ax.set_title(strategy.replace("_", " "))
        ax.set_xlabel("Scratchpad (KB)")
        ax.grid(True, alpha=0.3)
        ax.legend(fontsize=8)

    axes[0].set_ylabel("Compute utilization")
    fig.suptitle(f"Bandwidth sensitivity — DRAM latency {REF_LATENCY} cycles")
    plt.tight_layout()
    plt.savefig(out("bandwidth_sensitivity.png"), dpi=160)

    # ── Plot 6: Energy efficiency ─────────────────────────────────────────────
    plt.figure(figsize=(8, 5))
    for strategy, group in overlay_groups(ref):
        plt.plot(group["scratchpad_kb"], group["ops_per_pj"],
                 label=strategy, **STYLES.get(strategy, DEFAULT_STYLE))
    plt.xlabel("Scratchpad size (KB)")
    plt.ylabel("Energy efficiency (ops/pJ)")
    plt.title(f"Energy efficiency — latency {REF_LATENCY} cyc, bandwidth {REF_BANDWIDTH} B/cyc")
    plt.grid(True, alpha=0.3)
    plt.legend()
    plt.tight_layout()
    plt.savefig(out("energy_efficiency.png"), dpi=160)

    auto_ref = tuned_rows(ref)
    if not auto_ref.empty:
        auto_ref = auto_ref.sort_values("scratchpad_kb")
        strategy_order = [s for s in DRAW_ORDER if s in set(auto_ref["strategy"])]
        strategy_order += [s for s in auto_ref["strategy"].unique() if s not in strategy_order]
        strategy_rank = {s: i for i, s in enumerate(strategy_order)}

        plt.figure(figsize=(8, 4.5))
        y = auto_ref["strategy"].map(strategy_rank)
        colors = auto_ref["strategy"].map(lambda s: STRATEGY_COLORS.get(s, "tab:gray"))
        plt.scatter(auto_ref["scratchpad_kb"], y, c=colors, s=80)
        plt.yticks(range(len(strategy_order)), [s.replace("_", " ") for s in strategy_order])
        plt.xlabel("Scratchpad size (KB)")
        plt.ylabel("Winning strategy")
        plt.title(f"Auto-tuner strategy choice - latency {REF_LATENCY} cyc, bandwidth {REF_BANDWIDTH} B/cyc")
        plt.grid(True, axis="x", alpha=0.3)
        plt.tight_layout()
        plt.savefig(out("auto_strategy_selection.png"), dpi=160)

        plt.figure(figsize=(8, 5))
        for column, label, marker in [
            ("tile_m", "tile M", "o"),
            ("tile_n", "tile N", "s"),
            ("tile_k", "tile K", "^"),
        ]:
            plt.plot(auto_ref["scratchpad_kb"], auto_ref[column],
                     marker=marker, label=label)
        plt.xlabel("Scratchpad size (KB)")
        plt.ylabel("Selected tile dimension")
        plt.title(f"Auto-tuner tile shape - latency {REF_LATENCY} cyc, bandwidth {REF_BANDWIDTH} B/cyc")
        plt.grid(True, alpha=0.3)
        plt.legend()
        plt.tight_layout()
        plt.savefig(out("auto_tile_shape.png"), dpi=160)

        plt.figure(figsize=(8, 5))
        for column, label, marker in [
            ("tune_candidates_evaluated", "evaluated", "o"),
            ("tune_candidates_rejected", "rejected", "s"),
        ]:
            if column in auto_ref:
                plt.plot(auto_ref["scratchpad_kb"], auto_ref[column],
                         marker=marker, label=label)
        plt.xlabel("Scratchpad size (KB)")
        plt.ylabel("Candidate count")
        plt.title(f"Auto-tuner search effort - latency {REF_LATENCY} cyc, bandwidth {REF_BANDWIDTH} B/cyc")
        plt.grid(True, alpha=0.3)
        plt.legend()
        plt.tight_layout()
        plt.savefig(out("auto_search_effort.png"), dpi=160)

    print(f"wrote plots to {out_dir}")


if __name__ == "__main__":
    main()
