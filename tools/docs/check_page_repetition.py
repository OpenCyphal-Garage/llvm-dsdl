"""Fails when a documentation page has eaten itself.

A scripted edit that replaces without bounding how many times it matches can grow a page into
thousands of copies of one passage. Nothing else notices: prose is not compiled, not linted and
not read by any test, so a page whose content is gone builds, publishes and passes every gate.
That happened here -- one 271-line page became 245,231 lines of 95 distinct ones, and was
committed.

Substantial lines are the signal. Short ones repeat for good reasons -- a fence, a table rule, a
bare comment marker in a generated sample -- while a whole sentence appearing dozens of times
means the page is a copy of itself. Across this corpus the most any sentence legitimately
repeats is ten, in a generated type page that shows one doc comment rendered into six languages.
"""

from __future__ import annotations

import argparse
import sys
from collections import Counter
from pathlib import Path

# Below this a line is structure rather than prose.
SUBSTANTIAL_CHARS = 40


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--docs-dir", required=True, type=Path)
    parser.add_argument("--max-repeats", type=int, default=25,
                        help="How often one sentence may appear in a page (default: 25)")
    args = parser.parse_args()

    failures: list[str] = []
    pages = 0
    for page in sorted(args.docs_dir.rglob("*.md")):
        pages += 1
        lines = [line.strip()
                 for line in page.read_text(encoding="utf-8", errors="replace").splitlines()]
        substantial = [line for line in lines if len(line) >= SUBSTANTIAL_CHARS]
        if not substantial:
            continue
        line, count = Counter(substantial).most_common(1)[0]
        if count > args.max_repeats:
            failures.append(f"{page}: one line appears {count} times in {len(lines)} line(s)\n"
                            f"        |{line[:78]}|")

    if failures:
        print("documentation repetition check failed: a page is largely a copy of itself")
        for failure in failures:
            print(f"    {failure}")
        return 1

    print(f"documentation repetition check: {pages} page(s), none repeating a line more than "
          f"{args.max_repeats} times")
    return 0


if __name__ == "__main__":
    sys.exit(main())
