#!/usr/bin/env python3
# ===----------------------------------------------------------------------===#
#
# Part of the OpenCyphal project, under the MIT licence
# SPDX-License-Identifier: MIT
#
# ===----------------------------------------------------------------------===#
"""Generate the machine-readable views of the manual: ``llms.txt`` and ``llms-full.txt``.

Both follow the llms.txt convention (https://llmstxt.org/): a link index small enough for an agent
to load before deciding what to fetch, and the whole manual concatenated for agents that would
rather make one request than crawl.

Both are derived from the navigation in ``mkdocs.yml``. That is the point -- a hand-written index is
a second copy of the navigation, and the second copy is the one that goes stale. Deriving it also
lets this script enforce the coverage rule: every page under ``docs/`` is either in the navigation or
in the navigation's declared exclusion list, and anything else fails the build.

The output carries no timestamp, so regenerating from an unchanged tree is byte-identical. The
compiler is held to that; its documentation may as well be.
"""

from __future__ import annotations

import argparse
import fnmatch
import re
import sys
from dataclasses import dataclass
from pathlib import Path

# One line of a mkdocs nav entry: "  - Title: path/page.md" or "  - Group Title:".
_NAV_ENTRY = re.compile(r"^(?P<indent>\s*)-\s+(?P<body>\S.*)$")

# Inline markdown that would only be noise in a one-line summary. An underscore only counts as
# emphasis at a word boundary: inside a word it is part of a file name the page is citing, and
# `parity_matrix_report.py` must not come out as `paritymatrixreport.py`.
_MD_LINK = re.compile(r"\[([^\]]+)\]\([^)]*\)")
_MD_EMPHASIS = re.compile(r"\*\*|\*|(?<![A-Za-z0-9])_|_(?![A-Za-z0-9])")
_HTML_COMMENT = re.compile(r"<!--.*?-->", re.DOTALL)

SUMMARY_MAX_CHARS = 220


class DocsIndexError(Exception):
    """A condition the caller must fix: a malformed nav, a missing page, an unfiled page."""


@dataclass(frozen=True)
class Page:
    """One published page: where it lives, what it is called, and where it lands on the site."""

    section: str  # top-level nav section, e.g. "Reference"
    group: str  # nested group within the section, "" when the entry sits directly under it
    title: str
    path: Path  # absolute path on disk
    rel: str  # path relative to docs_dir, e.g. "reference/commands/dsdlc.md"
    url: str  # absolute published URL

    @property
    def label(self) -> str:
        return f"{self.group}: {self.title}" if self.group else self.title


# --------------------------------------------------------------------------------------------------
# mkdocs.yml
#
# Parsed by hand rather than with PyYAML. This script runs under whatever python3 the build found,
# which is not the pinned documentation virtualenv and cannot be assumed to have PyYAML -- and
# mkdocs.yml carries a `!!python/name:` tag that PyYAML's safe loader rejects outright, so using it
# would mean supplying a custom loader anyway. The nav grammar this accepts is small, and every line
# that does not fit it is an error rather than a silent skip.
# --------------------------------------------------------------------------------------------------


def _scalar(text: str, key: str) -> str:
    """Read a top-level ``key: value`` scalar from mkdocs.yml."""
    match = re.search(rf"^{re.escape(key)}:\s*(\S.*?)\s*$", text, re.MULTILINE)
    if not match:
        raise DocsIndexError(f"mkdocs.yml has no top-level `{key}:`")
    return match.group(1).strip().strip("'\"")


def _block(text: str, key: str) -> list[str]:
    """Read a top-level ``key: |`` literal block from mkdocs.yml, one entry per line."""
    match = re.search(rf"^{re.escape(key)}:\s*\|\s*$", text, re.MULTILINE)
    if not match:
        return []
    lines: list[str] = []
    for line in text[match.end() :].splitlines():
        if not line.strip():
            continue
        if not line.startswith((" ", "\t")):
            break
        lines.append(line.strip())
    return lines


def parse_nav(text: str) -> list[tuple[str, str, str, str]]:
    """Return ``(section, group, title, relative path)`` for every page in the nav, in nav order."""
    start = re.search(r"^nav:\s*$", text, re.MULTILINE)
    if not start:
        raise DocsIndexError("mkdocs.yml has no top-level `nav:`")

    entries: list[tuple[str, str, str, str]] = []
    stack: list[tuple[int, str]] = []  # (indent, title) for the groups currently open

    for lineno, line in enumerate(text[start.end() :].splitlines()[1:], start=1):
        if not line.strip():
            continue
        if not line.startswith((" ", "\t")):
            break  # dedented back to column 0: the nav block is over
        if line.lstrip().startswith("#"):
            continue

        match = _NAV_ENTRY.match(line)
        if not match:
            raise DocsIndexError(f"nav line {lineno} is not a `- Title: page.md` entry: {line!r}")

        indent = len(match.group("indent"))
        body = match.group("body").strip()

        while stack and stack[-1][0] >= indent:
            stack.pop()

        if body.endswith(":"):  # a group: "- Reference:"
            stack.append((indent, body[:-1].strip()))
            continue

        title, _, target = body.rpartition(":")
        title, target = title.strip(), target.strip()
        if not title or not target.endswith(".md"):
            raise DocsIndexError(f"nav line {lineno} does not name a .md page: {line!r}")

        section = stack[0][1] if stack else title
        group = stack[1][1] if len(stack) > 1 else ""
        entries.append((section, group, title, target))

    if not entries:
        raise DocsIndexError("mkdocs.yml `nav:` lists no pages")
    return entries


# --------------------------------------------------------------------------------------------------
# Pages
# --------------------------------------------------------------------------------------------------


def page_url(site_url: str, rel: str) -> str:
    """Map a docs-relative path to its published URL under mkdocs' default directory URLs."""
    base = site_url if site_url.endswith("/") else site_url + "/"
    if rel == "index.md":
        return base
    if rel.endswith("/index.md"):
        return base + rel[: -len("index.md")]
    return base + rel[: -len(".md")] + "/"


def summarize(path: Path) -> str:
    """One line describing the page, taken from the page's own first paragraph.

    Taken from the page rather than written here so that a summary cannot describe a page that has
    since been rewritten. Leading HTML comments (the generated pages carry a do-not-edit banner) and
    the H1 are skipped; a leading blockquote counts, because several pages open with one. A paragraph
    that only states where the page came from is skipped too -- provenance is not what a page is
    about, and one generated page still leads with it.
    """
    text = _HTML_COMMENT.sub("", path.read_text(encoding="utf-8"))

    paragraph: list[str] = []
    blocks: list[str] = []
    for line in text.splitlines() + [""]:  # sentinel: close a paragraph that runs to end of file
        stripped = line.strip()
        if not stripped or stripped.startswith(("|", "```", "- ", "* ", "1. ")):
            if paragraph:
                blocks.append(" ".join(paragraph))
                paragraph = []
            continue  # a page whose body opens with a table or list has no lead paragraph
        if stripped.startswith("#"):
            continue
        paragraph.append(stripped.lstrip("> ").strip())

    summary = next((block for block in blocks if not block.startswith("Generated by")), "")
    summary = _MD_LINK.sub(r"\1", summary)
    summary = _MD_EMPHASIS.sub("", summary).replace("`", "")
    summary = re.sub(r"\s+", " ", summary).strip()

    if len(summary) > SUMMARY_MAX_CHARS:
        cut = summary[:SUMMARY_MAX_CHARS].rsplit(" ", 1)[0]
        summary = cut.rstrip(",.;:") + "…"
    return summary


def collect(docs_dir: Path, site_url: str, nav: list[tuple[str, str, str, str]]) -> list[Page]:
    pages: list[Page] = []
    for section, group, title, rel in nav:
        path = docs_dir / rel
        if not path.is_file():
            raise DocsIndexError(
                f"nav lists `{rel}`, which does not exist. Generated pages (the showroom, the "
                f"guarantee matrices) are produced by `docs-generate` before this script runs."
            )
        pages.append(Page(section, group, title, path, rel, page_url(site_url, rel)))
    return pages


def collect_excluded(docs_dir: Path, site_url: str, patterns: list[str], navigated: set[str]) -> list[Page]:
    """Pages deliberately kept out of the human nav -- the showroom's per-type gallery.

    They stay out of the dropdown because two dozen entries would drown the section they belong to,
    but they are the densest description of what the compiler emits, so a machine reader gets them.
    """
    found: list[Page] = []
    for path in sorted(docs_dir.rglob("*.md")):
        rel = path.relative_to(docs_dir).as_posix()
        if rel in navigated:
            continue
        if not any(fnmatch.fnmatch(rel, pattern) for pattern in patterns):
            raise DocsIndexError(
                f"`{rel}` is in neither the mkdocs nav nor `not_in_nav`. Add it to the nav, or to "
                f"`not_in_nav:` in mkdocs.yml if it is deliberately unlisted."
            )
        found.append(Page("Showroom Types", "", path.stem, path, rel, page_url(site_url, rel)))
    return found


# --------------------------------------------------------------------------------------------------
# Rendering
# --------------------------------------------------------------------------------------------------


def render_index(site_name: str, site_description: str, site_url: str, pages: list[Page]) -> str:
    base = site_url if site_url.endswith("/") else site_url + "/"
    out = [
        f"# {site_name}",
        "",
        f"> {site_description}",
        "",
        "Generated from the navigation in mkdocs.yml by tools/docs/build_llms_index.py. Every page of",
        "the manual is listed below with a summary taken from the page itself. The full text of every",
        f"page, concatenated into one document, is at {base}llms-full.txt.",
        "",
    ]

    for section in dict.fromkeys(page.section for page in pages):
        out.append(f"## {section}")
        out.append("")
        for page in (p for p in pages if p.section == section):
            summary = summarize(page.path)
            out.append(f"- [{page.label}]({page.url})" + (f": {summary}" if summary else ""))
        out.append("")

    return "\n".join(out).rstrip() + "\n"


def render_full(site_name: str, site_url: str, pages: list[Page], docs_dir: Path) -> str:
    base = site_url if site_url.endswith("/") else site_url + "/"
    rule = "=" * 98
    out = [
        f"# {site_name} -- complete text",
        "",
        f"Every page of the manual at {base}, concatenated in navigation order.",
        "Generated by tools/docs/build_llms_index.py; the link index alone is at llms.txt.",
        "",
        "The showroom's generated type gallery is not included here. Those two dozen pages carry the",
        "full generated code for every type in eight language/profile variants and are four times the",
        "size of the manual itself, which would make this file expensive to fetch for the many readers",
        "who want the manual. Every one of them is listed with its URL in llms.txt.",
        "",
    ]
    for page in pages:
        out += [
            rule,
            f"Source: {docs_dir.name}/{page.rel}",
            f"URL: {page.url}",
            rule,
            "",
            _HTML_COMMENT.sub("", page.path.read_text(encoding="utf-8")).strip(),
            "",
        ]
    return "\n".join(out).rstrip() + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mkdocs-yml", type=Path, required=True)
    parser.add_argument("--docs-dir", type=Path, required=True)
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=None,
        help="where llms.txt and llms-full.txt are written (default: the docs directory)",
    )
    args = parser.parse_args()

    output_dir = args.output_dir or args.docs_dir

    try:
        config = args.mkdocs_yml.read_text(encoding="utf-8")
        site_name = _scalar(config, "site_name")
        site_description = _scalar(config, "site_description")
        site_url = _scalar(config, "site_url")
        nav = parse_nav(config)

        pages = collect(args.docs_dir, site_url, nav)
        excluded = collect_excluded(
            args.docs_dir, site_url, _block(config, "not_in_nav"), {page.rel for page in pages}
        )
    except DocsIndexError as error:
        print(f"error: {error}", file=sys.stderr)
        return 1

    # Rendered before anything is written so a failure cannot leave one file new and the other stale.
    index_text = render_index(site_name, site_description, site_url, pages + excluded)
    full_text = render_full(site_name, site_url, pages, args.docs_dir)

    output_dir.mkdir(parents=True, exist_ok=True)
    (output_dir / "llms.txt").write_text(index_text, encoding="utf-8")
    (output_dir / "llms-full.txt").write_text(full_text, encoding="utf-8")

    print(
        f"docs index: {len(pages)} navigated page(s) and {len(excluded)} unlisted page(s) "
        f"written to {output_dir}/llms.txt and {output_dir}/llms-full.txt"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
