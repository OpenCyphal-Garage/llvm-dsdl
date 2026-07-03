# ===----------------------------------------------------------------------===//
#
# Part of the OpenCyphal project, under the MIT licence
# SPDX-License-Identifier: MIT
#
# ===----------------------------------------------------------------------===//

"""Parse ctest/JUnit XML result files into pass/fail/skip test-name sets.

This turns the convergence matrix gates from *name-presence* checks (a cell is
"covered" if a test with a matching name is merely registered) into *behavioral*
checks (a cell is "covered" only if a matching test actually executed and passed).

The parser is deliberately tolerant of the two shapes the project produces:

* `ctest --output-junit <file>` — a single ``<testsuite>`` whose ``<testcase>``
  elements carry a ``<failure>``/``<error>`` child on failure, a ``<skipped>``
  child (or ``status="notrun"``) on skip, and nothing on success.
* Generic JUnit — nested ``<testsuites>``/``<testsuite>`` with the same
  ``<testcase>`` conventions.

Precedence when a test name appears more than once across files: ``fail`` wins
over ``pass`` wins over ``skip`` (a flaky test that failed anywhere is treated as
failed; a test that passed anywhere and never failed is treated as passed).
"""

from __future__ import annotations

import xml.etree.ElementTree as ET
from dataclasses import dataclass, field
from pathlib import Path
from typing import Iterable, List, Set


@dataclass
class TestResults:
    """Executed-test outcomes keyed by ctest test name."""

    passed: Set[str] = field(default_factory=set)
    failed: Set[str] = field(default_factory=set)
    skipped: Set[str] = field(default_factory=set)

    @property
    def total(self) -> int:
        return len(self.passed) + len(self.failed) + len(self.skipped)


def _classify_testcase(testcase: ET.Element) -> str:
    """Return one of ``fail`` / ``skip`` / ``pass`` for a ``<testcase>`` element."""
    status = (testcase.get("status") or "").strip().lower()
    has_failure = any(
        child.tag.rsplit("}", 1)[-1] in ("failure", "error") for child in testcase
    )
    has_skip = any(child.tag.rsplit("}", 1)[-1] == "skipped" for child in testcase)
    if has_failure or status in ("fail", "failed"):
        return "fail"
    if has_skip or status in ("notrun", "skipped", "disabled", "not_run"):
        return "skip"
    return "pass"


def load_junit_results(paths: Iterable[Path]) -> TestResults:
    """Parse one or more JUnit XML files into a :class:`TestResults`.

    Raises ``FileNotFoundError`` if any path is missing and ``ValueError`` if a
    file cannot be parsed as XML — a behavioral gate must fail loudly rather than
    silently treat "no results" as "everything passed".
    """
    results = TestResults()
    for raw_path in paths:
        path = Path(raw_path)
        if not path.exists():
            raise FileNotFoundError(f"ctest results file not found: {path}")
        try:
            root = ET.fromstring(path.read_text(encoding="utf-8"))
        except ET.ParseError as err:
            raise ValueError(f"failed to parse JUnit XML {path}: {err}") from err
        for testcase in root.iter("testcase"):
            name = (testcase.get("name") or "").strip()
            if not name:
                continue
            outcome = _classify_testcase(testcase)
            if outcome == "fail":
                results.failed.add(name)
            elif outcome == "skip":
                results.skipped.add(name)
            else:
                results.passed.add(name)
    # Apply precedence: fail > pass > skip.
    results.passed -= results.failed
    results.skipped -= results.failed
    results.skipped -= results.passed
    return results


def matches(test_names: Iterable[str], patterns: Iterable[str]) -> List[str]:
    """Return the sorted subset of ``test_names`` matching any regex in ``patterns``."""
    import re

    compiled = [re.compile(pattern) for pattern in patterns]
    return [name for name in sorted(set(test_names)) if any(p.search(name) for p in compiled)]
