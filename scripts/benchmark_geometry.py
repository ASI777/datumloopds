#!/usr/bin/env python3
"""Run the fixed geometry benchmark under the selected kernel's Python runtime."""

from __future__ import annotations

import argparse
import os
import sys

from datumloopds.geometry.benchmark import all_cases_succeeded, run_benchmark, step_file_specs_from_json


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--backend", choices=("occt", "smlib"), required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    step_files = step_file_specs_from_json(os.environ.get("DATUMLOOPDS_STEP_FILES_JSON"))
    report = run_benchmark(args.backend, args.output, step_files=step_files)
    if not all_cases_succeeded(report):
        failed = [case["name"] for case in report["cases"] if not case.get("valid", False)]
        print(f"geometry benchmark has failed or invalid cases: {', '.join(failed)}", file=sys.stderr)
        return 1
    print(f"geometry benchmark written to {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
