from __future__ import annotations

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


def main() -> None:
    exe = find_executable()
    RESULTS.mkdir(exist_ok=True)
    out_path = RESULTS / "sweep.csv"
    strategies = ["row_stationary", "output_stationary", "double_buffer"]
    scratchpad_kb = [4, 8, 16, 32, 64, 128]
    dram_latencies = [50, 100, 200]

    rows = []
    for strategy in strategies:
        for sp_kb in scratchpad_kb:
            for latency in dram_latencies:
                result = subprocess.run(
                    [
                        str(exe),
                        "--strategy",
                        strategy,
                        "--scratchpad-kb",
                        str(sp_kb),
                        "--dram-latency",
                        str(latency),
                        "--csv",
                    ],
                    check=True,
                    text=True,
                    capture_output=True,
                )
                reader = csv.DictReader(result.stdout.splitlines())
                rows.extend(reader)

    with out_path.open("w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        writer.writeheader()
        writer.writerows(rows)

    print(f"wrote {out_path}")


if __name__ == "__main__":
    main()
