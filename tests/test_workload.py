import csv
import subprocess
import sys
import tempfile
from pathlib import Path


def run(*args: str, check: bool = True) -> subprocess.CompletedProcess[str]:
    return subprocess.run(args, check=check, text=True, capture_output=True)


def write(path: Path, text: str) -> None:
    path.write_text(text, encoding="utf-8")


def main() -> int:
    repo = Path(sys.argv[1])
    exe = Path(sys.argv[2])
    script = repo / "scripts" / "run_workload.py"

    with tempfile.TemporaryDirectory() as tmp_name:
        tmp = Path(tmp_name)
        workload = tmp / "workload.csv"
        out_csv = tmp / "workload_results.csv"
        write(workload, "name,m,n,k\nproj,8,8,8\nmlp,16,8,8\n")

        result = run(sys.executable, str(script), str(workload),
                     "--exe", str(exe),
                     "--strategy", "auto",
                     "--tune-budget", "4",
                     "--out", str(out_csv))
        if "layers: 2" not in result.stdout:
            raise AssertionError("workload summary did not report two layers")

        rows = list(csv.DictReader(out_csv.read_text(encoding="utf-8").splitlines()))
        if len(rows) != 2:
            raise AssertionError(f"expected two workload rows, got {len(rows)}")
        required = {
            "layer_index", "layer_name", "matrix_m", "matrix_n", "matrix_k",
            "strategy", "total_cycles", "energy_pj", "tuned",
            "tune_candidates_evaluated",
        }
        for row in rows:
            missing = required - set(row)
            if missing:
                raise AssertionError(f"missing workload output fields: {sorted(missing)}")
            if row["strategy"] == "auto":
                raise AssertionError("workload output kept auto instead of concrete strategy")
            if row["tuned"] != "1":
                raise AssertionError("auto workload row was not marked tuned")
            if int(row["tune_candidates_evaluated"]) <= 0:
                raise AssertionError("auto workload row did not evaluate candidates")

        for suffix in ("cycles_by_layer", "energy_by_layer", "strategy_distribution"):
            plot = tmp / f"workload_results_{suffix}.png"
            if not plot.exists() or plot.stat().st_size == 0:
                raise AssertionError(f"missing workload plot: {plot}")

        invalid_cases = {
            "missing_column.csv": "name,m,n\nbad,1,2\n",
            "invalid_number.csv": "name,m,n,k\nbad,nope,2,3\n",
            "empty.csv": "name,m,n,k\n",
        }
        for filename, contents in invalid_cases.items():
            bad_workload = tmp / filename
            write(bad_workload, contents)
            failed = run(sys.executable, str(script), str(bad_workload),
                         "--exe", str(exe),
                         "--out", str(tmp / f"{filename}.out.csv"),
                         check=False)
            if failed.returncode == 0:
                raise AssertionError(f"invalid workload should have failed: {filename}")

        failed = run(sys.executable, str(script), str(workload),
                     "--exe", str(exe),
                     "--strategy", "row_stationary",
                     "--tune-budget", "4",
                     "--out", str(tmp / "named_with_tune.csv"),
                     check=False)
        if failed.returncode == 0:
            raise AssertionError("named strategy accepted a tuning flag")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
