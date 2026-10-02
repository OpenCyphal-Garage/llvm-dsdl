#!/usr/bin/env python3
# ===----------------------------------------------------------------------===#
#
# Part of the OpenCyphal project, under the MIT licence
# SPDX-License-Identifier: MIT
#
# ===----------------------------------------------------------------------===#
"""Fail a CI run in which a test did not run.

A test that skips reports success to everything that reads only failures: ctest counts it as passed,
and the job stays green. A gate that skips where it was meant to gate has stopped gating, and nothing
says so. This reads the JUnit reports ctest writes and fails on every test that did not run, unless
the step allows that test by name, with the reason.

An allowance is held to the run it is given for: one whose test ran is reported, so the list stays a
true statement of what skips.
"""

from __future__ import annotations

import argparse
import os
import sys
import tempfile
import xml.etree.ElementTree as ElementTree
from pathlib import Path

# The lines of a skipped test's output shown with it, which is where a test states why it skipped.
OUTPUT_LINES = 6


def skipped_tests(report: Path) -> tuple[int, dict[str, str]]:
    """The number of tests @p report holds, and each that did not run with what it said."""
    root = ElementTree.parse(report).getroot()
    testcases = list(root.iter("testcase"))
    skipped = {}
    for testcase in testcases:
        marker = testcase.find("skipped")
        if testcase.get("status") != "notrun" and marker is None:
            continue
        message = marker.get("message", "") if marker is not None else ""
        output = (testcase.findtext("system-out") or "").strip().splitlines()
        skipped[testcase.get("name", "")] = "\n".join([message, *output[-OUTPUT_LINES:]]).strip()
    return len(testcases), skipped


def allowance(text: str) -> tuple[str, str]:
    """`NAME=REASON` as its parts; an allowance without a reason is refused."""
    name, _, reason = text.partition("=")
    if not name.strip() or not reason.strip():
        raise argparse.ArgumentTypeError(f"an allowance is NAME=REASON, with both: {text!r}")
    return name.strip(), reason.strip()


def check(reports: list[Path], allowed: dict[str, str]) -> list[str]:
    """Each test that did not run without an allowance, each report with no test, and each allowance
    whose test ran."""
    problems = []
    skipped: dict[str, str] = {}
    for report in reports:
        if not report.is_file():
            problems.append(f"{report}: no such report")
            continue
        count, found = skipped_tests(report)
        if count == 0:
            problems.append(f"{report}: holds no test")
        skipped.update(found)
    for name, output in sorted(skipped.items()):
        if name not in allowed:
            detail = "\n".join(f"    {line}" for line in output.splitlines())
            problems.append(f"{name} did not run:\n{detail}" if detail else f"{name} did not run")
    for name in sorted(set(allowed) - set(skipped)):
        problems.append(f"{name} is allowed to skip ({allowed[name]}) but ran or is absent; drop the allowance")
    return problems


def self_test() -> int:
    """Checks the check against reports in ctest's shape."""

    def report(*testcases: str) -> str:
        return f'<?xml version="1.0"?><testsuite tests="{len(testcases)}">{"".join(testcases)}</testsuite>'

    ran = '<testcase name="ran" status="run"><system-out>ok</system-out></testcase>'
    skip = (
        '<testcase name="gate" status="notrun"><skipped message="SKIP_RETURN_CODE=77"/>'
        "<system-out>skipped: valgrind is not installed</system-out></testcase>"
    )
    disabled = '<testcase name="off" status="notrun"><skipped message="Disabled"/></testcase>'
    cases = [
        ("a run with no skip passes", report(ran), {}, 0),
        ("a skip fails, with what the test said", report(ran, skip), {}, 1),
        ("a disabled test fails", report(ran, disabled), {}, 1),
        ("an allowed skip passes", report(ran, skip), {"gate": "no valgrind"}, 0),
        ("an allowance whose test ran fails", report(ran), {"ran": "stale"}, 1),
        ("a report with no test fails", report(), {}, 1),
    ]
    failures = []
    with tempfile.TemporaryDirectory() as scratch:
        path = Path(scratch) / "junit.xml"
        for what, text, allowed, expected in cases:
            path.write_text(text, encoding="utf-8")
            problems = check([path], allowed)
            if (1 if problems else 0) != expected:
                failures.append(f"{what}: reported {problems}")
            if what.endswith("with what the test said") and not any("valgrind" in p for p in problems):
                failures.append(f"{what}: the test's output is missing from {problems}")
        missing = Path(scratch) / "absent.xml"
        if not any("no such report" in p for p in check([missing], {})):
            failures.append("a missing report was not reported")
    try:
        allowance("gate=")
        failures.append("an allowance without a reason was accepted")
    except argparse.ArgumentTypeError:
        pass
    for failure in failures:
        print(f"self-test: {failure}", file=sys.stderr)
    if failures:
        return 1
    print(f"self-test: {len(cases) + 2} cases as expected")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Fail when a test in a ctest JUnit report did not run, unless it is allowed to skip by "
        "name, with the reason."
    )
    parser.add_argument("reports", nargs="*", type=Path, help="JUnit reports ctest wrote with --output-junit")
    parser.add_argument(
        "--allow",
        type=allowance,
        action="append",
        default=[],
        metavar="NAME=REASON",
        help="a test allowed to skip in this run, and why; repeatable",
    )
    parser.add_argument("--self-test", action="store_true", help="check the check rather than a report")
    arguments = parser.parse_args()
    if arguments.self_test:
        return self_test()
    if not arguments.reports:
        parser.error("name at least one report")

    problems = check(arguments.reports, dict(arguments.allow))
    annotate = os.environ.get("GITHUB_ACTIONS") == "true"
    for problem in problems:
        if annotate:
            print(f"::error::{problem.splitlines()[0]}")
        print(problem, file=sys.stderr)
    if problems:
        print("A test that does not run gates nothing. Make it run here, or allow it by name with the reason.")
        return 1
    # check() has held every allowance to a test that skipped.
    for name, reason in sorted(arguments.allow):
        print(f"{name} skipped, as allowed: {reason}")
    others = "every other test" if arguments.allow else "every test"
    print(f"{others} in {', '.join(str(r) for r in arguments.reports)} ran")
    return 0


if __name__ == "__main__":
    sys.exit(main())
