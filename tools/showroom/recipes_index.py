#!/usr/bin/env python3
# ===----------------------------------------------------------------------===#
#
# Part of the OpenCyphal project, under the MIT licence
# SPDX-License-Identifier: MIT
#
# ===----------------------------------------------------------------------===#
"""Render the showroom's build recipes into browsable Markdown under docs/showroom/recipes.

Every recipe on the generated pages is read out of the recipe's own `recipe.json` and its own build
files -- the same `recipe.json` that `run_recipe.py` executes, and the same build files it stages. A
snippet in the manual therefore cannot drift from the snippet CI runs, because there is only one of
them and this script copies rather than restates it.

Unlike the showroom generator, this one needs no compiler and no generated tree: recipes are described
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
sys.path.insert(0, str(REPO_ROOT / "examples" / "showroom"))

from run_recipe import Recipe, RecipeError, discover_recipes  # noqa: E402

MATRIX_MARKER = "<!-- showroom-recipes: matrix-table -->"
SKIP_MARKER = "<!-- showroom-recipes: skip -->"

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

# Build files are shown in full on the recipe page; these are the ones worth leading with, in the order
# a reader meets them. Anything else in the recipe directory is appended afterwards, alphabetically, so
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


def build_files(recipe: Recipe) -> list[Path]:
    """Every committed file in a recipe directory except its manifest, in reading order."""
    found = [
        p for p in sorted(recipe.directory.rglob("*"))
        if p.is_file() and p.name != "recipe.json"
    ]
    ranked = {name: i for i, name in enumerate(FILE_ORDER)}
    return sorted(found, key=lambda p: (ranked.get(p.name, len(FILE_ORDER)), str(p)))


# --------------------------------------------------------------------------------------------------
# Recipe pages.

def render_recipe_page(recipe: Recipe) -> str:
    lines = [
        f"# {recipe.build_system} ({recipe.row})",
        "",
        recipe.summary,
        "",
        "| | |",
        "|---|---|",
        f"| Language | {recipe.language} |",
        f"| Build system | {recipe.build_system} |",
        f"| Idiom | {IDIOM_LABEL[recipe.idiom]} |",
        f"| Regeneration | {DEP_STRATEGY_LABEL[recipe.dep_strategy]} |",
        "",
    ]

    if recipe.notes:
        lines += ["## Notes", "", recipe.notes, ""]

    lines += [
        "## What you need",
        "",
    ]
    if recipe.requires:
        lines += ["| Tool | Why |", "|---|---|"]
        lines += [f"| `{r.command}` | {r.reason} |" for r in recipe.requires]
    else:
        lines.append("Nothing beyond `dsdlc` itself.")
    lines.append("")

    lines += [
        "## Commands",
        "",
        "Run these from the recipe directory, with `dsdl/` and `src/` copied alongside it. This is the "
        "exact sequence CI runs.",
        "",
        "```bash",
    ]
    for step in recipe.steps:
        lines.append(f"# {step.name}")
        lines.append(" ".join(_shell_quote(a.format_map(DISPLAY_SUBSTITUTIONS)) for a in step.run))
    lines += ["```", ""]

    lines += [
        "## The types this builds",
        "",
        "The whole `lanyard` namespace -- twenty-four definitions, browsable from the "
        "[showroom overview](../index.md), where each one is paired with its wire-layout facts and "
        "a declaration excerpt in every language.",
        "",
    ]

    files = build_files(recipe)
    if files:
        lines += ["## The build files", ""]
        for path in files:
            rel = path.relative_to(recipe.directory)
            lines += [f"### `{rel}`", "", f"```{fence_for(path)}",
                      path.read_text(encoding="utf-8").rstrip("\n"), "```", ""]

    return "\n".join(lines).rstrip("\n") + "\n"


def _shell_quote(arg: str) -> str:
    """Quote for display only. The runner never goes through a shell."""
    return arg if re.fullmatch(r"[A-Za-z0-9_@%+=:,./{}-]+", arg) else f'"{arg}"'


# --------------------------------------------------------------------------------------------------
# Index page.

def render_matrix_table(recipes: list[Recipe]) -> list[str]:
    if not recipes:
        return ["*No recipes are registered yet.*"]
    lines = ["| Language | Build system | Idiom | Regeneration |", "|---|---|---|---|"]
    for recipe in recipes:
        link = f"[{recipe.build_system}]({recipe.name}.md)"
        lines.append(
            f"| {recipe.row} | {link} | {recipe.idiom} | "
            f"{DEP_STRATEGY_LABEL[recipe.dep_strategy]} |"
        )
    return lines


def _rewrite_link(target: str) -> str:
    """Repository-relative links in the README do not resolve from the site; drop to plain text."""
    if target.startswith(("http://", "https://", "#")):
        return target
    # The repository layout and the site layout differ by one level: RECIPES.md sits beside
    # README.md with the recipes in a subdirectory, while the rendered index sits *inside* that
    # subdirectory. Both link forms are rewritten so the source file stays correct on disk.
    if target == "README.md":
        return "../index.md"
    if target.startswith("recipes/"):
        return target[len("recipes/"):]
    return target


def render_recipes_index(readme: Path, recipes: list[Recipe]) -> str:
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
            out += render_matrix_table(recipes)
            index += 1
            continue

        out.append(re.sub(r"\]\(([^)]+)\)", lambda m: f"]({_rewrite_link(m.group(1))})", line))
        index += 1

    return "\n".join(out).rstrip("\n") + "\n"


def generate(recipes_root: Path, readme: Path, output_dir: Path) -> int:
    """Writes <output_dir>/index.md and one page per recipe. Returns a process exit code."""
    if not readme.is_file():
        print(f"error: no recipes README at {readme}", file=sys.stderr)
        return 2

    try:
        recipes = discover_recipes(recipes_root)
    except RecipeError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2

    output_dir.mkdir(parents=True, exist_ok=True)

    # Stale pages from a removed recipe would keep 404-ing from the matrix; clear and rewrite.
    for existing in output_dir.glob("*.md"):
        existing.unlink()

    (output_dir / "index.md").write_text(render_recipes_index(readme, recipes), encoding="utf-8")
    for recipe in recipes:
        (output_dir / f"{recipe.name}.md").write_text(render_recipe_page(recipe), encoding="utf-8")

    print(f"showroom recipes: wrote {len(recipes)} recipe pages and an index to {output_dir}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--recipes-root", type=Path,
                        default=REPO_ROOT / "examples" / "showroom" / "recipes")
    parser.add_argument("--readme", type=Path,
                        default=REPO_ROOT / "examples" / "showroom" / "RECIPES.md")
    parser.add_argument("--output-dir", type=Path,
                        default=REPO_ROOT / "docs" / "showroom" / "recipes")
    args = parser.parse_args()
    return generate(args.recipes_root, args.readme, args.output_dir)


if __name__ == "__main__":
    sys.exit(main())
