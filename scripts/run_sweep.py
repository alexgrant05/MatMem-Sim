from __future__ import annotations

import argparse
import csv
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
RESULTS = ROOT / "results"
BUILD_EXES = [
    ROOT / "build" / "Debug" / "matmem-sim.exe",
    ROOT / "build" / "Release" / "matmem-sim.exe",
    ROOT / "build" / "matmem-sim.exe",
    ROOT / "build" / "matmem-sim",
]


def find_executable() -> Path:
    for path in BUILD_EXES:
        if path.exists():
            return path
    raise SystemExit("Could not find built matmem-sim executable. Run CMake build first.")


def run_one(exe: Path, strategy: str, sp_kb: int, latency: int, bandwidth: int,
            matrix_m: int, matrix_n: int, matrix_k: int) -> dict:
    cmd = [
        str(exe),
        "--strategy", strategy,
        "--scratchpad-kb", str(sp_kb),
        "--dram-latency", str(latency),
        "--bandwidth", str(bandwidth),
        "--matrix-m", str(matrix_m),
        "--matrix-n", str(matrix_n),
        "--matrix-k", str(matrix_k),
        "--csv",
    ]
    result = subprocess.run(cmd, check=True, text=True, capture_output=True)
    reader = csv.DictReader(result.stdout.splitlines())
    return next(iter(reader))


def main() -> None:
    parser = argparse.ArgumentParser(description="Parameter sweep for matmem-sim")
    parser.add_argument("--matrix-m", type=int, default=256)
    parser.add_argument("--matrix-n", type=int, default=256)
    parser.add_argument("--matrix-k", type=int, default=256)
    parser.add_argument("--out", type=Path, default=RESULTS / "sweep.csv")
    args = parser.parse_args()

    exe = find_executable()
    args.out.parent.mkdir(exist_ok=True)

    strategies = ["row_stationary", "output_stationary", "input_stationary", "double_buffer"]
    scratchpad_kb = [4, 8, 16, 32, 64, 128]
    dram_latencies = [50, 100, 200]
    bandwidths = [8, 16, 32, 64]

    rows = []
    total = len(strategies) * len(scratchpad_kb) * len(dram_latencies) * len(bandwidths)
    done = 0
    for strategy in strategies:
        for sp_kb in scratchpad_kb:
            for latency in dram_latencies:
                for bw in bandwidths:
                    rows.append(run_one(exe, strategy, sp_kb, latency, bw,
                                        args.matrix_m, args.matrix_n, args.matrix_k))
                    done += 1
                    print(f"  [{done}/{total}] {strategy} sp={sp_kb}KB lat={latency} bw={bw}", flush=True)

    with args.out.open("w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        writer.writeheader()
        writer.writerows(rows)

    print(f"wrote {args.out}")


if __name__ == "__main__":
    main()
