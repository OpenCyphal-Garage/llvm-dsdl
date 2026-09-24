#!/usr/bin/env python3
"""Assert the container carries every style judge, and report the versions it found.

`CLEAN_CODE.md` phase 1 points each language's own style judge at the generated code, because a
compiler accepts an un-idiomatic name by design. A judge the container lacks would let its lane pass
by finding nothing rather than by there being nothing to find, which is how a missing toolchain
sheds a language's tests.

This installs nothing. A judge that is not here belongs in the toolshed image.
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

# A Go tool installed with `go install` lands in GOPATH/bin, and a container's GOPATH at run time
# need not be the one the image built with, so the likely roots are searched rather than assumed.
GO_BIN_ROOTS = ("/usr/local/bin", "/usr/local/go/bin", "/root/go/bin", "/opt/go/bin")

# eslint reads a `.ts` file only through the TypeScript parser, so an eslint without one lints
# nothing and reports that as success. Either package provides it.
PARSER_PACKAGES = ("typescript-eslint", "@typescript-eslint/parser")


def go_env(name: str) -> str:
    """Return a value from `go env`, or an empty string where go is absent."""
    if shutil.which("go") is None:
        return ""
    try:
        return subprocess.run(
            ["go", "env", name], capture_output=True, text=True, timeout=60, check=True
        ).stdout.strip()
    except (subprocess.SubprocessError, OSError):
        return ""


def find_command(name: str, *, go_tool: bool = False) -> tuple[str | None, list[str]]:
    """Locate @p name, returning the path and every place that was looked.

    @p go_tool widens the search to where `go install` puts a binary, which is worth doing only for
    a tool that is distributed that way.
    """
    looked = ["PATH"]
    found = shutil.which(name)
    if found:
        return found, looked
    if not go_tool:
        return None, looked

    roots = [root for root in (f"{go_env('GOPATH')}/bin", f"{go_env('GOROOT')}/bin") if root != "/bin"]
    roots += list(GO_BIN_ROOTS)
    for root in roots:
        looked.append(root)
        candidate = Path(root) / name
        if candidate.is_file() and os.access(candidate, os.X_OK):
            return str(candidate), looked
    return None, looked


def version_of(command: str, *args: str) -> str:
    """Return the first line @p command prints for its version, or a note that it did not."""
    try:
        result = subprocess.run(
            [command, *(args or ("--version",))], capture_output=True, text=True, timeout=120
        )
    except (subprocess.SubprocessError, OSError) as exc:
        return f"(would not run: {exc})"
    line = (result.stdout or result.stderr).strip().splitlines()
    return line[0] if line else "(said nothing)"


def npm_global_root() -> str:
    """Return npm's global root, which is where an image-installed package sits."""
    if shutil.which("npm") is None:
        return ""
    try:
        return subprocess.run(
            ["npm", "root", "-g"], capture_output=True, text=True, timeout=120, check=True
        ).stdout.strip()
    except (subprocess.SubprocessError, OSError):
        return ""


def find_parser() -> tuple[str | None, str, list[str]]:
    """Locate the TypeScript parser, returning its package, version and where it was looked for."""
    root = npm_global_root()
    looked = [root or "(npm root -g gave nothing)"]
    if shutil.which("node") is None:
        return None, "", looked

    environment = dict(os.environ)
    if root:
        environment["NODE_PATH"] = root
    for package in PARSER_PACKAGES:
        try:
            result = subprocess.run(
                ["node", "-p", f"require('{package}/package.json').version"],
                capture_output=True,
                text=True,
                timeout=120,
                env=environment,
            )
        except (subprocess.SubprocessError, OSError):
            continue
        if result.returncode == 0 and result.stdout.strip():
            return package, result.stdout.strip(), looked
    return None, "", looked


# Which judges are distributed as a Go tool, and so may sit off PATH entirely.
GO_TOOLS = frozenset({"staticcheck"})

JUDGES = {
    "staticcheck": "Go style judge (ST1003 and the SA checks)",
    "ruff": "Python style judge",
    "eslint": "TypeScript style judge",
    "cargo-clippy": "Rust style judge, beyond what rustc denies",
}


def print_path(name: str) -> int:
    """Print where @p name is, for a caller that cannot rely on PATH holding it.

    The build registers a judge lane only for a judge it can find, so this is the one lookup both
    the assertion and the lane registration ask, rather than each guessing at install locations.
    """
    path, looked = find_command(name, go_tool=name in GO_TOOLS)
    if path is None:
        print(f"{name} not found; looked in {', '.join(looked)}", file=sys.stderr)
        return 1
    print(path)
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--path",
        metavar="JUDGE",
        choices=sorted(JUDGES),
        help="print where one judge is and exit, rather than asserting them all",
    )
    arguments = parser.parse_args()
    if arguments.path:
        return print_path(arguments.path)

    # eslint 10 is where `no-useless-assignment` begins, and it reports the accessor that binds a
    # size the language's signature does not return -- the same defect `SA4006` and `F841` name, at
    # the same count in all three. An eslint behind 10 finds none of them, so a lane on it would
    # ratchet in a shape the other two judges report. The other judges take no floor: the versions
    # tried report identically, rule for rule.
    missing: list[str] = []
    found: dict[str, str] = {}

    for name, purpose in JUDGES.items():
        path, looked = find_command(name, go_tool=name in GO_TOOLS)
        if path is None:
            missing.append(f"command: {name} ({purpose}; looked in {', '.join(looked)})")
            continue
        found[name] = version_of(path)

    if "eslint" in found:
        digits = "".join(c for c in found["eslint"].lstrip("v").split(".")[0] if c.isdigit())
        if not digits or int(digits) < 10:
            missing.append(
                f"version: eslint {found['eslint']} is behind 10, where no-useless-assignment begins"
            )

    # Required, not optional: eslint alone answers `interface` with `Parsing error: Unexpected
    # token interface`, and a configuration whose `files` misses `.ts` lints nothing and exits
    # zero. An image carrying eslint without the parser can host no TypeScript judge.
    package, version, looked = find_parser()
    if package is None:
        missing.append(
            "module: "
            + " or ".join(PARSER_PACKAGES)
            + f" (TypeScript parser for eslint; looked under {', '.join(looked)})"
        )
    else:
        found[package] = version

    for name, version in found.items():
        print(f"{name:<28} {version}")
    sys.stdout.flush()

    if not missing:
        return 0

    marker = "::error::" if os.environ.get("GITHUB_ACTIONS") == "true" else ""
    print(f"{marker}the container is missing a style judge CLEAN_CODE.md phase 1 needs:", file=sys.stderr)
    for item in missing:
        print(f"{marker}  {item}", file=sys.stderr)
    print("These belong in the toolshed image rather than being installed per run.", file=sys.stderr)
    print(
        "Environment: "
        + json.dumps(
            {"GOPATH": go_env("GOPATH"), "GOROOT": go_env("GOROOT"), "npm root -g": npm_global_root()}
        ),
        file=sys.stderr,
    )
    return 1


if __name__ == "__main__":
    sys.exit(main())
