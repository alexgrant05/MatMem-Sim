import csv
import subprocess
import sys


TUNE_FIELDS = (
    "tuned", "tune_objective", "tile_m", "tile_n", "tile_k",
    "tune_candidates_considered", "tune_candidates_evaluated",
    "tune_candidates_rejected",
)


def run(exe: str, *args: str, check: bool = True) -> subprocess.CompletedProcess[str]:
    return subprocess.run([exe, *args], check=check, text=True, capture_output=True)


def main() -> int:
    exe = sys.argv[1]
    result = run(exe, "--strategy", "input_stationary", "--matrix-m", "16",
                 "--matrix-n", "16", "--matrix-k", "16", "--csv")
    row = next(csv.DictReader(result.stdout.splitlines()))
    for field in ("a_reuse_factor", "b_reuse_factor", "c_reuse_factor"):
        if field not in row:
            raise AssertionError(f"missing CSV field: {field}")
        float(row[field])
    for field in ("scratchpad_latency", "scratchpad_stall_cycles"):
        if field not in row:
            raise AssertionError(f"missing CSV field: {field}")
        int(row[field])

    for field in TUNE_FIELDS:
        if field not in row:
            raise AssertionError(f"missing CSV field: {field}")
    if row["tuned"] != "0":
        raise AssertionError("named strategy was incorrectly marked as tuned")

    tuned = run(exe, "--strategy", "auto", "--matrix-m", "16", "--matrix-n", "16",
                "--matrix-k", "16", "--tune-budget", "16", "--csv")
    tuned_row = next(csv.DictReader(tuned.stdout.splitlines()))
    if tuned_row["strategy"] == "auto" or tuned_row["tuned"] != "1":
        raise AssertionError("auto run did not report a concrete winner")
    if tuned_row["tune_objective"] != "cycles":
        raise AssertionError("unexpected default tuning objective")
    for field in ("tile_m", "tile_n", "tile_k", "tune_candidates_evaluated"):
        if int(tuned_row[field]) <= 0:
            raise AssertionError(f"expected positive tuned field: {field}")

    fixed = run(exe, "--strategy", "auto", "--matrix-m", "8", "--matrix-n", "8",
                "--matrix-k", "8", "--tile-m", "4", "--tile-n", "4", "--tile-k", "4",
                "--tune-objective", "energy", "--tune-budget", "4", "--csv", "--trace")
    fixed_row = next(csv.DictReader(fixed.stdout.splitlines()))
    if [fixed_row["tile_m"], fixed_row["tile_n"], fixed_row["tile_k"]] != ["4", "4", "4"]:
        raise AssertionError("fixed tile shape changed during strategy-only tuning")
    trace_lines = [line for line in fixed.stdout.splitlines() if line.startswith("tile ")]
    if len(trace_lines) != 8:
        raise AssertionError(f"expected 8 winner trace records, got {len(trace_lines)}")

    invalid_commands = [
        ("--strategy", "auto", "--tune-budget", "3"),
        ("--strategy", "auto", "--tune-objective", "unknown"),
        ("--strategy", "row_stationary", "--tune-budget", "8"),
        ("--strategy", "auto", "--tile-m", "4"),
    ]
    for args in invalid_commands:
        failed = run(exe, *args, check=False)
        if failed.returncode == 0:
            raise AssertionError(f"command should have failed: {' '.join(args)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
