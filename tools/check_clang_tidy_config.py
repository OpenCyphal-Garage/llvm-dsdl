#!/usr/bin/env python3
# ===----------------------------------------------------------------------===#
#
# Part of the OpenCyphal project, under the MIT licence
# SPDX-License-Identifier: MIT
#
# ===----------------------------------------------------------------------===#
"""Reject a clang-tidy configuration that clang-tidy itself would accept and quietly misread.

clang-tidy splits the ``Checks:`` scalar on commas and treats every fragment as a glob. Nothing
that fails to match is reported, so two edits that look harmless change the ruleset silently:

* A ``#`` comment inside the scalar. A comment with no comma is inert, but one comma turns its
  tail into a live pattern -- ``# disabled, modernize-*`` enables all 47 modernize checks.
* A mistyped check name. ``-readabilty-magic-numbers`` subtracts nothing, and the check it was
  meant to disable stays on.

Unknown ``CheckOptions`` keys are dropped the same way, so a renamed or misspelled option reads as
a setting that is in force when it is not.

This script fails on all three. The Lint job runs it over the project's ``.clang-tidy`` and over the
rulesets the C and C++ style judges hold generated code to.
"""

from __future__ import annotations

import argparse
import pathlib
import re
import shutil
import subprocess
import sys

_REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent


def _scalar_lines(text: str, key: str) -> tuple[list[str], int] | None:
    """Return the folded block scalar under ``key`` with the line number it starts on."""
    lines = text.splitlines()
    for index, line in enumerate(lines):
        if not re.match(rf"^{re.escape(key)}\s*:\s*[>|]", line):
            continue
        body: list[str] = []
        for offset, following in enumerate(lines[index + 1 :], start=index + 2):
            if following.strip() and not following.startswith((" ", "\t")):
                break
            body.append(following)
        return body, index + 1
    return None


def _known_checks(clang_tidy: str) -> set[str]:
    out = subprocess.run(
        [clang_tidy, "--checks=*", "--list-checks"],
        capture_output=True,
        text=True,
        check=True,
    ).stdout
    return {line.strip() for line in out.splitlines()[1:] if line.strip()}


def _unknown_options(clang_tidy: str, config: pathlib.Path) -> tuple[set[str], str]:
    """Return the ``CheckOptions`` keys clang-tidy does not recognise, and what it said.

    ``--dump-config`` lists only the options a check stores a default for, which leaves out every
    per-kind option of ``readability-identifier-naming``, so clang-tidy's own verification is asked.
    """
    result = subprocess.run(
        [clang_tidy, "--verify-config", f"--config-file={config}"], capture_output=True, text=True
    )
    said = result.stdout + result.stderr
    return set(re.findall(r"unknown check option '([^']+)'", said)), said if result.returncode else ""


def _glob_matches(pattern: str, names: set[str]) -> bool:
    expanded = ".*".join(re.escape(part) for part in pattern.split("*"))
    return any(re.fullmatch(expanded, name) for name in names)


def _check(config: pathlib.Path, clang_tidy: str, checks: set[str]) -> int:
    """Report what is wrong with @p config, returning the exit status for it alone."""
    if not config.is_file():
        print(f"error: no such file: {config}", file=sys.stderr)
        return 2

    text = config.read_text()
    problems: list[str] = []

    found = _scalar_lines(text, "Checks")
    if found is None:
        print(f"error: {config} has no `Checks:` block scalar", file=sys.stderr)
        return 2
    body, first_line = found

    for offset, line in enumerate(body, start=first_line + 1):
        if "#" in line:
            problems.append(
                f"{config}:{offset}: `#` inside the Checks scalar. clang-tidy globs every "
                f"comma-separated fragment, so a comment can enable checks: {line.strip()!r}"
            )

    unknown, said = _unknown_options(clang_tidy, config)

    for offset, line in enumerate(body, start=first_line + 1):
        if "#" in line:
            continue
        for token in (fragment.strip() for fragment in line.split(",")):
            if not token:
                continue
            name = token[1:] if token.startswith("-") else token
            if not name:
                problems.append(f"{config}:{offset}: bare `-` in the Checks scalar")
            elif "*" in name:
                if not _glob_matches(name, checks):
                    problems.append(f"{config}:{offset}: glob matches no check: {token!r}")
            elif name not in checks:
                problems.append(f"{config}:{offset}: not a check known to {clang_tidy}: {token!r}")

    for match in re.finditer(r"^\s{2}([A-Za-z0-9_.-]+\.[A-Za-z0-9_]+)\s*:", text, re.M):
        key = match.group(1)
        if key in unknown:
            line_number = text[: match.start()].count("\n") + 1
            problems.append(f"{config}:{line_number}: not an option known to {clang_tidy}: {key!r}")
    # A verification that failed on something other than an option is still a failure.
    if said and not problems:
        problems.append(f"{config}: clang-tidy --verify-config failed:\n{said.strip()}")

    if problems:
        for problem in problems:
            print(problem, file=sys.stderr)
        print(f"\n{len(problems)} problem(s) in {config}", file=sys.stderr)
        return 1

    enabled = subprocess.run(
        [clang_tidy, "--list-checks", f"--config-file={config}"], capture_output=True, text=True, check=True
    ).stdout
    print(f"{config} is consistent with {clang_tidy}: {len(enabled.splitlines()) - 1} checks enabled.")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--config", type=pathlib.Path, nargs="+", default=[_REPO_ROOT / ".clang-tidy"])
    parser.add_argument("--clang-tidy", default=None)
    args = parser.parse_args()

    clang_tidy = args.clang_tidy or shutil.which("clang-tidy")
    if not clang_tidy:
        print("error: clang-tidy not found; pass --clang-tidy PATH", file=sys.stderr)
        return 2

    checks = _known_checks(clang_tidy)
    return max(_check(config, clang_tidy, checks) for config in args.config)


if __name__ == "__main__":
    sys.exit(main())
