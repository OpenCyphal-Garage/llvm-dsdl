#!/usr/bin/env python3
# ===----------------------------------------------------------------------===#
#
# Part of the OpenCyphal project, under the MIT licence
# SPDX-License-Identifier: MIT
#
# ===----------------------------------------------------------------------===#
"""Check that every ```mermaid fence reaches the built site in a form mermaid can render.

A tripwire for a failure that is invisible to `mkdocs build --strict`: the markdown is valid, the
page builds, and the diagram becomes a red "Syntax error in text" card in the reader's browser. The
build has no opinion, because the damage is done by JavaScript that never runs at build time.

The specific breakage this exists to catch is a one-word edit that looks like a correction. Almost
every mermaid-with-MkDocs recipe specifies `fence_code_format`, which emits

    <pre class="mermaid"><code>flowchart LR ...</code></pre>

Those recipes are written for mkdocs-material, whose own JavaScript pulls the diagram out of that
`<code>` by textContent. Vanilla mermaid -- what this site runs -- reads the element's innerHTML
instead, so the `<code>` tag becomes the first token of the diagram and every diagram on the page
fails with "No diagram type detected ... for text: <code>flowchart LR". This site therefore uses
`fence_div_format`, and the comment in mkdocs.yml says why. This script is what notices if that
changes back.

Not a mermaid syntax check: rendering the diagrams would mean running a browser in CI. What is
checked is that the diagram text arrives at the browser in the shape mermaid expects.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

# A mermaid fence, opening at column zero. A page could in principle show this markup *as an example*
# inside a wider fence, which would count here without producing a diagram -- so a page having more
# fences than rendered elements is reported, while the reverse is left alone.
MERMAID_FENCE = re.compile(r"^```mermaid\s*$", re.MULTILINE)

# Any element mermaid will pick up, and the same element with the wrapper that breaks it.
MERMAID_ELEMENT = re.compile(r"<(?:pre|div)[^>]*\bclass=\"[^\"]*\bmermaid\b[^\"]*\"[^>]*>")
MERMAID_WRAPPED = re.compile(
    r"<(?:pre|div)[^>]*\bclass=\"[^\"]*\bmermaid\b[^\"]*\"[^>]*>\s*<code\b"
)


def built_page(site_dir: Path, rel: str) -> Path:
    """The HTML mkdocs writes for a docs-relative markdown path, under directory URLs."""
    if rel == "index.md":
        return site_dir / "index.html"
    if rel.endswith("/index.md"):
        return site_dir / rel[: -len("index.md")] / "index.html"
    return site_dir / rel[: -len(".md")] / "index.html"


def check(docs_dir: Path, site_dir: Path) -> list[str]:
    problems: list[str] = []
    checked = 0

    for source in sorted(docs_dir.rglob("*.md")):
        rel = source.relative_to(docs_dir).as_posix()
        fences = len(MERMAID_FENCE.findall(source.read_text(encoding="utf-8")))
        if not fences:
            continue

        page = built_page(site_dir, rel)
        if not page.is_file():
            problems.append(f"{rel}: has {fences} mermaid fence(s) but built no page at {page}")
            continue

        html = page.read_text(encoding="utf-8")
        wrapped = len(MERMAID_WRAPPED.findall(html))
        rendered = len(MERMAID_ELEMENT.findall(html))
        checked += 1

        if wrapped:
            problems.append(
                f"{rel}: {wrapped} mermaid element(s) in {page} wrap the diagram in <code>. "
                f"Vanilla mermaid reads innerHTML, so the tag becomes part of the diagram and the "
                f"page renders a syntax error. Check the mermaid custom_fence in mkdocs.yml: it "
                f"must be `fence_div_format`, not `fence_code_format`."
            )
        elif rendered < fences:
            problems.append(
                f"{rel}: {fences} mermaid fence(s) produced only {rendered} mermaid element(s) in "
                f"{page}. The custom_fence in mkdocs.yml may no longer be matching this fence."
            )

    if not problems:
        print(
            f"mermaid: {checked} page(s) carry diagrams, all in a renderable shape"
            if checked
            else "mermaid: no pages carry diagrams"
        )
    return problems


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--docs-dir", type=Path, required=True)
    parser.add_argument("--site-dir", type=Path, required=True)
    args = parser.parse_args()

    for label, path in (("docs", args.docs_dir), ("site", args.site_dir)):
        if not path.is_dir():
            suffix = "\n       build the site first: mkdocs build --strict" if label == "site" else ""
            print(f"error: {label} directory does not exist: {path}{suffix}", file=sys.stderr)
            return 1

    problems = check(args.docs_dir, args.site_dir)
    if problems:
        print(
            f"error: {len(problems)} page(s) render mermaid in a shape mermaid cannot read:",
            file=sys.stderr,
        )
        for problem in problems:
            print(f"  {problem}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
