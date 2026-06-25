import csv
import subprocess
import sys


def main() -> int:
    exe = sys.argv[1]
    result = subprocess.run(
        [exe, "--strategy", "input_stationary", "--matrix-m", "16", "--matrix-n", "16",
         "--matrix-k", "16", "--csv"],
        check=True,
        text=True,
        capture_output=True,
    )
    row = next(csv.DictReader(result.stdout.splitlines()))
    for field in ("a_reuse_factor", "b_reuse_factor", "c_reuse_factor"):
        if field not in row:
            raise AssertionError(f"missing CSV field: {field}")
        float(row[field])
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
