#!/usr/bin/env python3
# ===----------------------------------------------------------------------===#
#
# Part of the OpenCyphal project, under the MIT licence
# SPDX-License-Identifier: MIT
#
# ===----------------------------------------------------------------------===#
"""Run clang-tidy over a list of translation units, several at a time.

CMake's script mode runs ``execute_process`` calls one after another, so the ``check-clang-tidy``
target used to spend about twelve minutes walking the tree on one core. The file selection stays in
``cmake/CheckClangTidy.cmake`` -- this only executes what that script chose, across a pool.

Reads one file path per line, so a long list does not have to survive a command line.

    python3 tools/run_clang_tidy.py --clang-tidy clang-tidy --build-dir build/... --file-list list.txt
"""

from __future__ import annotations

import argparse
import concurrent.futures
import os
import pathlib
import subprocess
import sys


def _job_count(text: str) -> int:
    """Reject a worker count the pool would refuse, so the message names the flag."""
    try:
        value = int(text)
    except ValueError:
        raise argparse.ArgumentTypeError(f"expected an integer, got {text!r}") from None
    if value < 0:
        raise argparse.ArgumentTypeError(f"must be 0 or more, got {value}")
    return value


def _check_one(clang_tidy: str, args: list[str], path: str) -> tuple[str, int, str]:
    finished = subprocess.run([clang_tidy, *args, path], capture_output=True, text=True, check=False)
    output = finished.stdout
    if finished.stderr:
        output = f"{output}\n{finished.stderr}" if output else finished.stderr
    return path, finished.returncode, output


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--clang-tidy", required=True)
    parser.add_argument("--build-dir", required=True)
    parser.add_argument("--file-list", required=True, type=pathlib.Path)
    parser.add_argument("--jobs", type=_job_count, default=0, help="0 selects one per available CPU.")
    parser.add_argument("--extra-arg-before", action="append", default=[])
    parser.add_argument("--header-filter", default=None)
    parser.add_argument("--exclude-header-filter", default=None)
    args = parser.parse_args()

    files = [line.strip() for line in args.file_list.read_text().splitlines() if line.strip()]
    if not files:
        print("No project C/C++ source files found in compile commands for clang-tidy.")
        return 0

    # The compile database is written by whichever compiler configured the build. A GCC build
    # carries flags clang does not know (-Wno-class-memaccess, -Wno-dangling-reference,
    # -Wno-stringop-overread), and clang-tidy's embedded clang treats an unknown -W as an error,
    # which fails every translation unit before a single check runs.
    tidy_args = ["-p", args.build_dir, "--warnings-as-errors=*", "--extra-arg=-Wno-unknown-warning-option"]
    if args.header_filter:
        tidy_args.append(f"--header-filter={args.header_filter}")
    if args.exclude_header_filter:
        tidy_args.append(f"--exclude-header-filter={args.exclude_header_filter}")
    for extra in args.extra_arg_before:
        tidy_args.append(f"--extra-arg-before={extra}")

    jobs = args.jobs or (os.cpu_count() or 1)
    failures: list[tuple[str, str]] = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
        futures = [pool.submit(_check_one, args.clang_tidy, tidy_args, f) for f in files]
        for done in concurrent.futures.as_completed(futures):
            path, code, output = done.result()
            if code != 0:
                failures.append((path, output))

    if failures:
        failures.sort()
        print(f"clang-tidy check failed for {len(failures)} file(s):", file=sys.stderr)
        for path, _ in failures:
            print(f"  {path}", file=sys.stderr)
        first = next((out for _, out in failures if out.strip()), "")
        if first:
            print(f"\nFirst clang-tidy diagnostic:\n{first}", file=sys.stderr)
        return 1

    print(f"clang-tidy check passed for {len(files)} file(s).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
