#!/usr/bin/env python3
# ===----------------------------------------------------------------------===#
#
# Part of the OpenCyphal project, under the MIT licence
# SPDX-License-Identifier: MIT
#
# ===----------------------------------------------------------------------===#
"""Hold `dsdlc --help` and the `--target-language` parser to each other.

Both sides are read from the built binary, so adding a lane needs no edit here. What is checked:

* every language the help advertises is accepted by the parser;
* every language the parser accepts is advertised by the help -- probed by offering the parser a
  set of plausible names and asserting that anything it takes appears in the help;
* an unknown value is refused, and the refusal names the value.

A test that pinned the help text instead would fail on every new lane and be repaired by pasting
the new string in, which proves nothing about whether the parser agrees with it.

    python3 tools/dsdlc/check_language_contract.py --dsdlc path/to/dsdlc
"""

from __future__ import annotations

import argparse
import pathlib
import re
import subprocess
import sys

# Names the parser might plausibly grow, used to catch a lane wired into the parser but never
# advertised. Not a second copy of the table: nothing here has to be true, and a name the parser
# rejects is simply skipped.
CANDIDATE_NAMES = [
    "ast", "mlir", "c", "cpp", "rust", "go", "ts", "python", "obj",
    "zig", "java", "csharp", "swift", "kotlin", "nim", "d", "ada", "js", "wasm", "llvm", "asm",
]


def _run(dsdlc: str, args: list[str]) -> tuple[int, str]:
    finished = subprocess.run([dsdlc, *args], capture_output=True, text=True, check=False)
    return finished.returncode, finished.stdout + finished.stderr


def _advertised(dsdlc: str) -> list[str]:
    code, out = _run(dsdlc, ["--help"])
    if code != 0 and not out:
        raise SystemExit(f"error: `{dsdlc} --help` produced nothing (exit {code})")
    match = re.search(r"^LANGUAGES\n\s*(.+)$", out, re.M)
    if not match:
        raise SystemExit("error: --help has no LANGUAGES section")
    return [name.strip() for name in match.group(1).split("|") if name.strip()]


def _accepted(dsdlc: str, name: str) -> bool:
    """True when the parser recognises `name`, whatever it then complains about."""
    _, out = _run(dsdlc, ["--target-language", name])
    return "unknown --target-language value" not in out


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--dsdlc", required=True)
    args = parser.parse_args()

    problems: list[str] = []

    advertised = _advertised(args.dsdlc)
    if not advertised:
        problems.append("the LANGUAGES section is empty")
    if len(advertised) != len(set(advertised)):
        problems.append(f"the LANGUAGES section repeats itself: {advertised}")

    for name in advertised:
        if not _accepted(args.dsdlc, name):
            problems.append(f"--help advertises {name!r}, but --target-language {name} is refused")

    for name in CANDIDATE_NAMES:
        if _accepted(args.dsdlc, name) and name not in advertised:
            problems.append(f"--target-language {name} is accepted, but --help does not advertise it")

    code, out = _run(args.dsdlc, ["--target-language", "definitely-not-a-language"])
    if code == 0:
        problems.append("an unknown --target-language value was accepted")
    if "definitely-not-a-language" not in out:
        problems.append("the unknown-language error does not name the value it refused")

    if problems:
        for problem in problems:
            print(f"error: {problem}", file=sys.stderr)
        return 1

    print(f"--help and --target-language agree on {len(advertised)} languages: {' '.join(advertised)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
