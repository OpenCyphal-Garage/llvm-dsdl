#!/usr/bin/env python3
# ===----------------------------------------------------------------------===#
#
# Part of the OpenCyphal project, under the MIT licence
# SPDX-License-Identifier: MIT
#
# ===----------------------------------------------------------------------===#
"""Render examples/build_integration into browsable Markdown under docs/build-integration.

Every recipe on the generated pages is read out of the cell's own `cell.json` and its own build
files -- the same `cell.json` that `run_cell.py` executes, and the same build files it stages. A
snippet in the manual therefore cannot drift from the snippet CI runs, because there is only one of
them and this script copies rather than restates it.

Unlike the showroom generator, this one needs no compiler and no generated tree: cells are described
by their manifests and their build files, both of which are committed. It runs anywhere, which is
why the docs target for this section has no dependency on building dsdlc.

The index page is the example's README rendered for the site, so that the file a contributor would
naturally edit is the file the site shows.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "examples" / "build_integration"))

from run_cell import Cell, CellError, discover_cells  # noqa: E402

MATRIX_MARKER = "<!-- build-integration: matrix-table -->"
SKIP_MARKER = "<!-- build-integration: skip -->"

# The runner substitutes these with the binary under test, which is an absolute path into a build
# tree. A reader has neither, and pasting `{dsdlc}` into a shell does nothing -- so the page shows
# the names a reader would actually have on PATH.
DISPLAY_SUBSTITUTIONS = {
    "dsdlc": "dsdlc",
    "python": "python3",
    # A shell variable rather than a literal path: where llvm-dsdl is installed is the reader's
    # decision, and a made-up prefix in a command they are meant to paste is worse than a name that
    # says what to set.
    "prefix": "$LLVM_DSDL_PREFIX",
}

IDIOM_LABEL = {
    "A": "A -- the generated tree is the package",
    "B": "B -- your project owns the manifest",
}

DEP_STRATEGY_LABEL = {
    "list-outputs": "Configure-time query (`--list-outputs`)",
    "depfile": "Build-time depfile (`-MD`)",
    "stamp": "Stamp file",
    "none": "None -- regeneration is an explicit step",
}

# Build files are shown in full on the cell page; these are the ones worth leading with, in the order
# a reader meets them. Anything else in the cell directory is appended afterwards, alphabetically, so
# a new build system does not need this list edited to have its files appear.
FILE_ORDER = (
    "CMakeLists.txt", "Makefile", "GNUmakefile", "BUILD.bazel", "MODULE.bazel",
    "Cargo.toml", "build.rs", "go.mod", "package.json", "tsconfig.json",
    "pyproject.toml", "setup.py",
)

FENCE_BY_SUFFIX = {
    ".toml": "toml", ".json": "json", ".rs": "rust", ".py": "python",
    ".bazel": "python", ".bzl": "python", ".mod": "go", ".txt": "cmake",
    ".cmake": "cmake", ".yml": "yaml", ".yaml": "yaml",
}
FENCE_BY_NAME = {
    "Makefile": "makefile", "GNUmakefile": "makefile", "CMakeLists.txt": "cmake",
    "BUILD.bazel": "python", "MODULE.bazel": "python",
}


def fence_for(path: Path) -> str:
    return FENCE_BY_NAME.get(path.name) or FENCE_BY_SUFFIX.get(path.suffix, "text")


def build_files(cell: Cell) -> list[Path]:
    """Every committed file in a cell directory except its manifest, in reading order."""
    found = [
        p for p in sorted(cell.directory.rglob("*"))
        if p.is_file() and p.name != "cell.json"
    ]
    ranked = {name: i for i, name in enumerate(FILE_ORDER)}
    return sorted(found, key=lambda p: (ranked.get(p.name, len(FILE_ORDER)), str(p)))


# --------------------------------------------------------------------------------------------------
# Cell pages.

def render_cell_page(cell: Cell) -> str:
    lines = [
        f"# {cell.build_system} ({cell.row})",
        "",
        cell.summary,
        "",
        "| | |",
        "|---|---|",
        f"| Language | {cell.language} |",
        f"| Build system | {cell.build_system} |",
        f"| Idiom | {IDIOM_LABEL[cell.idiom]} |",
        f"| Regeneration | {DEP_STRATEGY_LABEL[cell.dep_strategy]} |",
        "",
    ]

    if cell.notes:
        lines += ["## Notes", "", cell.notes, ""]

    lines += [
        "## What you need",
        "",
    ]
    if cell.requires:
        lines += ["| Tool | Why |", "|---|---|"]
        lines += [f"| `{r.command}` | {r.reason} |" for r in cell.requires]
    else:
        lines.append("Nothing beyond `dsdlc` itself.")
    lines.append("")

    lines += [
        "## Commands",
        "",
        "Run these from the cell directory, with `dsdl/` and `src/` copied alongside it. This is the "
        "exact sequence CI runs.",
        "",
        "```bash",
    ]
    for step in cell.steps:
        lines.append(f"# {step.name}")
        lines.append(" ".join(_shell_quote(a.format_map(DISPLAY_SUBSTITUTIONS)) for a in step.run))
    lines += ["```", ""]

    files = build_files(cell)
    if files:
        lines += ["## The build files", ""]
        for path in files:
            rel = path.relative_to(cell.directory)
            lines += [f"### `{rel}`", "", f"```{fence_for(path)}",
                      path.read_text(encoding="utf-8").rstrip("\n"), "```", ""]

    return "\n".join(lines).rstrip("\n") + "\n"


def _shell_quote(arg: str) -> str:
    """Quote for display only. The runner never goes through a shell."""
    return arg if re.fullmatch(r"[A-Za-z0-9_@%+=:,./{}-]+", arg) else f'"{arg}"'


# --------------------------------------------------------------------------------------------------
# Index page.

def render_matrix_table(cells: list[Cell]) -> list[str]:
    if not cells:
        return ["*No cells are registered yet.*"]
    lines = ["| Language | Build system | Idiom | Regeneration |", "|---|---|---|---|"]
    for cell in cells:
        link = f"[{cell.build_system}](cells/{cell.name}.md)"
        lines.append(
            f"| {cell.row} | {link} | {cell.idiom} | "
            f"{DEP_STRATEGY_LABEL[cell.dep_strategy]} |"
        )
    return lines


def _rewrite_link(target: str) -> str:
    """Repository-relative links in the README do not resolve from the site; drop to plain text."""
    if target.startswith(("http://", "https://", "#")):
        return target
    if target == "../showroom/README.md":
        return "../showroom/index.md"
    return target


def render_index(readme: Path, cells: list[Cell]) -> str:
    lines = readme.read_text(encoding="utf-8").splitlines()
    out: list[str] = []
    index = 0
    while index < len(lines):
        line = lines[index]

        # A section whose body opens with the skip marker is addressed to someone reading the
        # repository rather than the site, and is dropped along with its heading.
        if line.startswith("#"):
            body = index + 1
            while body < len(lines) and not lines[body].strip():
                body += 1
            if body < len(lines) and lines[body].strip() == SKIP_MARKER:
                level = len(line) - len(line.lstrip("#"))
                index += 1
                while index < len(lines):
                    nxt = lines[index]
                    if nxt.startswith("#") and (len(nxt) - len(nxt.lstrip("#"))) <= level:
                        break
                    index += 1
                continue

        if line.strip() == MATRIX_MARKER:
            out += render_matrix_table(cells)
            index += 1
            continue

        out.append(re.sub(r"\]\(([^)]+)\)", lambda m: f"]({_rewrite_link(m.group(1))})", line))
        index += 1

    return "\n".join(out).rstrip("\n") + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--example-root", type=Path,
                        default=REPO_ROOT / "examples" / "build_integration")
    parser.add_argument("--output-dir", type=Path,
                        default=REPO_ROOT / "docs" / "build-integration")
    args = parser.parse_args()

    readme = args.example_root / "README.md"
    if not readme.is_file():
        print(f"error: no README at {readme}", file=sys.stderr)
        return 2

    try:
        cells = discover_cells(args.example_root / "cells")
    except CellError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2

    cells_out = args.output_dir / "cells"
    cells_out.mkdir(parents=True, exist_ok=True)

    # Stale pages from a removed cell would keep 404-ing from the matrix; clear and rewrite.
    for existing in cells_out.glob("*.md"):
        existing.unlink()

    (args.output_dir / "index.md").write_text(render_index(readme, cells), encoding="utf-8")
    for cell in cells:
        (cells_out / f"{cell.name}.md").write_text(render_cell_page(cell), encoding="utf-8")

    print(f"docs/build-integration: index + {len(cells)} cell page(s)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
