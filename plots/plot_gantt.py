from __future__ import annotations

import argparse
import re
import subprocess
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.patches as mpatches
import matplotlib.pyplot as plt


ROOT = Path(__file__).resolve().parents[1]
BUILD_EXES = [
    ROOT / "build" / "Debug" / "matmem-sim.exe",
    ROOT / "build" / "Release" / "matmem-sim.exe",
    ROOT / "build" / "matmem-sim.exe",
    ROOT / "build" / "matmem-sim",
]

PHASE_COLORS = {"load": "steelblue", "compute": "mediumseagreen", "store": "tomato"}

_TILE_RE = re.compile(
    r"tile\s+(\d+):\s+load=\[(\d+),(\d+)\)\s+compute=\[(\d+),(\d+)\)\s+store=\[(\d+),(\d+)\)"
)


def find_executable() -> Path:
    for path in BUILD_EXES:
        if path.exists():
            return path
    raise SystemExit("Could not find matmem-sim executable — run cmake --build build first.")


def run_trace(exe: Path, args: argparse.Namespace) -> list[dict]:
    cmd = [str(exe), "--trace", "--strategy", args.strategy_current]
    for flag, attr in [
        ("--scratchpad-kb", "scratchpad_kb"),
        ("--dram-latency",  "dram_latency"),
        ("--bandwidth",     "bandwidth"),
        ("--compute-ops",   "compute_ops"),
        ("--matrix-m",      "matrix_m"),
        ("--matrix-n",      "matrix_n"),
        ("--matrix-k",      "matrix_k"),
    ]:
        val = getattr(args, attr)
        if val is not None:
            cmd += [flag, str(val)]
    if args.tile_m > 0:
        cmd += ["--tile-m", str(args.tile_m),
                "--tile-n", str(args.tile_n),
                "--tile-k", str(args.tile_k)]

    result = subprocess.run(cmd, check=True, text=True, capture_output=True)
    tiles = []
    for line in result.stdout.splitlines():
        m = _TILE_RE.match(line.strip())
        if m:
            idx, ls, le, cs, ce, ss, se = (int(x) for x in m.groups())
            tiles.append({"idx": idx,
                          "load":    (ls, le),
                          "compute": (cs, ce),
                          "store":   (ss, se)})
    return tiles


def plot(strategy: str, tiles: list[dict], out_path: Path, max_tiles: int) -> None:
    total_tiles = len(tiles)
    tiles = tiles[:max_tiles]
    n = len(tiles)
    if n == 0:
        print("no tiles in trace output")
        return

    fig_h = max(3.0, min(n * 0.35 + 1.5, 20.0))
    fig, ax = plt.subplots(figsize=(12, fig_h))

    bar_h = 0.6
    for tile in tiles:
        y = n - 1 - tile["idx"]          # tile 0 at top
        for phase, color in PHASE_COLORS.items():
            start, end = tile[phase]
            width = end - start
            if width > 0:
                ax.barh(y, width, left=start, height=bar_h, color=color, alpha=0.85)

    ax.set_yticks(range(n))
    ax.set_yticklabels([f"tile {n - 1 - i}" for i in range(n)], fontsize=7)
    ax.set_xlabel("Cycle")
    title = f"Gantt — {strategy.replace('_', ' ')}  ({total_tiles} tile{'s' if total_tiles != 1 else ''})"
    if n < total_tiles:
        title += f"  [first {n} shown]"
    ax.set_title(title)
    ax.grid(axis="x", alpha=0.3)

    legend_handles = [mpatches.Patch(color=c, label=p) for p, c in PHASE_COLORS.items()]
    ax.legend(handles=legend_handles, loc="lower right")

    plt.tight_layout()
    plt.savefig(out_path, dpi=160)
    print(f"wrote {out_path}")


ALL_STRATEGIES = ["row_stationary", "output_stationary", "input_stationary", "double_buffer"]


def main() -> None:
    parser = argparse.ArgumentParser(description="Gantt chart from matmem-sim --trace")
    parser.add_argument("--strategy",      nargs="+", default=ALL_STRATEGIES,
                        metavar="STRATEGY",
                        help="one or more strategies (default: all four)")
    parser.add_argument("--scratchpad-kb", type=int, dest="scratchpad_kb", default=None)
    parser.add_argument("--dram-latency",  type=int, dest="dram_latency",  default=None)
    parser.add_argument("--bandwidth",     type=int, dest="bandwidth",     default=None)
    parser.add_argument("--compute-ops",   type=int, dest="compute_ops",   default=None)
    parser.add_argument("--matrix-m",      type=int, dest="matrix_m",     default=None)
    parser.add_argument("--matrix-n",      type=int, dest="matrix_n",     default=None)
    parser.add_argument("--matrix-k",      type=int, dest="matrix_k",     default=None)
    parser.add_argument("--tile-m",        type=int, dest="tile_m",       default=0)
    parser.add_argument("--tile-n",        type=int, dest="tile_n",       default=0)
    parser.add_argument("--tile-k",        type=int, dest="tile_k",       default=0)
    parser.add_argument("--max-tiles",     type=int, dest="max_tiles",    default=50,
                        help="cap rows shown (default 50; full runs can have thousands)")
    parser.add_argument("--out-dir",       type=Path, dest="out_dir",
                        default=ROOT / "results",
                        help="directory for output PNGs (default: results/)")
    args = parser.parse_args()

    tile_dims = (args.tile_m, args.tile_n, args.tile_k)
    if any(v < 0 for v in tile_dims):
        parser.error("--tile-m, --tile-n, and --tile-k must be non-negative")
    if any(v != 0 for v in tile_dims) and not all(v > 0 for v in tile_dims):
        parser.error("--tile-m, --tile-n, and --tile-k must be set together or all left at 0")

    args.out_dir.mkdir(exist_ok=True)
    exe = find_executable()
    for strategy in args.strategy:
        args.strategy_current = strategy
        tiles = run_trace(exe, args)
        out_path = args.out_dir / f"gantt_{strategy}.png"
        plot(strategy, tiles, out_path, args.max_tiles)


if __name__ == "__main__":
    main()
