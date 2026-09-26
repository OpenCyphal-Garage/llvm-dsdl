#!/usr/bin/env python3
# ===----------------------------------------------------------------------===#
#
# Part of the OpenCyphal project, under the MIT licence
# SPDX-License-Identifier: MIT
#
# ===----------------------------------------------------------------------===#
"""Count where each emitter writes generated text, and hold the counts to a baseline.

An emission site is a call that writes text into the output -- a `SourceWriter`'s `line`, `open`,
`close`, `midway`, `raw` or `blank`, or a string streamed with `<<`. The count is split by where the call
sits: inside the emitter's `BodySpelling`, which spells a body the IR states, or outside it, in the
declaration half that `CLEAN_CODE.md` means to bring under a renderer. It is a proxy, and a crude
one -- a site that writes one token and a site that assembles a signature count alike -- but it moves
when string-built code is added or removed, which nothing else here measured.

A count may fall and not rise. A rise that is intended is retaken with --update, and the commit says
what was added and why the renderer could not state it.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
EMITTERS = REPO_ROOT / "lib" / "CodeGen" / "emitter"
LANGUAGES = ("C", "Cpp", "Rust", "Go", "Ts", "Python")

SITE = re.compile(r'\b\w+\.(?:line|open|close|midway|raw|blank)\(|<<\s*"')
SPELLING = re.compile(r"\b(?:class|struct)\s+\w+\s+final\s*:\s*public\s+(?:llvmdsdl::)?BodySpelling\b")


def count(path: Path) -> dict[str, int]:
    """Count @p path's emission sites inside and outside its `BodySpelling`."""
    lines = path.read_text().splitlines()
    start = next((i for i, line in enumerate(lines) if SPELLING.search(line)), None)
    if start is None:
        raise SystemExit(f"{path}: no class deriving BodySpelling")
    indent = len(lines[start]) - len(lines[start].lstrip())
    end = next(i for i in range(start + 1, len(lines)) if lines[i].startswith(" " * indent + "};"))
    body = sum(1 for line in lines[start : end + 1] if SITE.search(line) and not line.strip().startswith("//"))
    total = sum(1 for line in lines if SITE.search(line) and not line.strip().startswith("//"))
    return {"body": body, "declaration": total - body}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--baseline", type=Path, help="compare against this file, or rewrite it with --update")
    parser.add_argument("--update", action="store_true", help="rewrite the baseline from this count")
    arguments = parser.parse_args()

    counts = {language: count(EMITTERS / f"{language}.cpp") for language in LANGUAGES}
    for language, split in counts.items():
        print(f"{language:<8} body {split['body']:>5}   declaration {split['declaration']:>5}")
    body = sum(split["body"] for split in counts.values())
    declaration = sum(split["declaration"] for split in counts.values())
    print(f"{'total':<8} body {body:>5}   declaration {declaration:>5}")

    if arguments.baseline is None:
        return 0
    if arguments.update:
        arguments.baseline.write_text(json.dumps(counts, indent=2) + "\n")
        print(f"baseline rewritten: {arguments.baseline}")
        return 0

    recorded = json.loads(arguments.baseline.read_text())
    rises = [
        f"{language} {half}: {counts[language][half]}, baseline {recorded.get(language, {}).get(half)}"
        for language in LANGUAGES
        for half in ("body", "declaration")
        if counts[language][half] > recorded.get(language, {}).get(half, -1)
    ]
    if not rises:
        print("no emitter writes text in more places than its baseline")
        return 0
    print("an emitter writes text in more places than its baseline allows:", file=sys.stderr)
    for rise in rises:
        print(f"  {rise}", file=sys.stderr)
    print(
        "A count may fall and not rise. If the rise is intended, retake the baseline with --update and"
        " say in the commit what was added and why the renderer could not state it.",
        file=sys.stderr,
    )
    return 1


if __name__ == "__main__":
    sys.exit(main())
