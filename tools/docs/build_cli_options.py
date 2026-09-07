#!/usr/bin/env python3
# ===----------------------------------------------------------------------===#
#
# Part of the OpenCyphal project, under the MIT licence
# SPDX-License-Identifier: MIT
#
# ===----------------------------------------------------------------------===#

"""Render a tool's own ``--help`` into its published options page.

The help text is the authoritative description of the tool's interface, so the
page is derived from it rather than written beside it: a new flag appears on the
site at the next publish, and there is no second copy to keep in step. This is
the same derivation ``tools/man/generate_manpage.py`` performs for the man pages.

The help is emitted verbatim inside a fenced block. A parser that rewrote it into
tables would have to model the help's structure, and a subtly wrong table is worse
than a faithful rendering of what the tool printed.

Running the tool is what makes the page authoritative, so this needs a
host-executable binary. A cross-build has to generate the page on the host side.
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path
from typing import List, Tuple

HELP_TIMEOUT_SECONDS = 60

PAGE = """# `{name}` Options

Every option `{name}` accepts, as the binary itself prints it. This page is generated from `--help`
at publish time, so it carries what the tool in that build accepts.

[`{name}`]({name}.md) covers what the options are for.

{fence}text
{help_text}
{fence}
"""


def _run_help(name: str, tool: Path) -> str:
    """Return the tool's ``--help`` output, or exit with a diagnostic."""
    # A tool that blocks instead of printing help would hang the publish, so cap it
    # and fail loudly. stdin is closed for the same reason.
    try:
        proc = subprocess.run(
            [str(tool), "--help"],
            stdin=subprocess.DEVNULL,
            capture_output=True,
            text=True,
            timeout=HELP_TIMEOUT_SECONDS,
        )
    except subprocess.TimeoutExpired:
        raise SystemExit(f"{name}: did not respond to --help within {HELP_TIMEOUT_SECONDS}s")
    except OSError as ex:
        raise SystemExit(f"{name}: could not run {tool}: {ex}")
    # The hand-written help texts write to stdout; the stderr fallback covers a
    # tool that chooses the other stream.
    out = proc.stdout.strip() or proc.stderr.strip()
    if not out:
        raise SystemExit(f"{name}: --help produced no output")
    return out


def _fence_for(text: str) -> str:
    """A fence longer than any backtick run in @p text, so the block cannot be closed early."""
    longest = max((len(m.group(0)) for m in re.finditer(r"`+", text)), default=0)
    return "`" * max(3, longest + 1)


def _parse_tool(spec: str) -> Tuple[str, Path]:
    name, sep, path = spec.partition("=")
    if not sep or not name or not path:
        raise argparse.ArgumentTypeError(f"expected NAME=PATH, got {spec!r}")
    return name, Path(path)


def parse_args(argv: List[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Render a tool's published options page from its --help.")
    parser.add_argument("--tool", required=True, type=_parse_tool, metavar="NAME=PATH", help="the tool to document")
    parser.add_argument("--output", required=True, type=Path, help="Markdown page to write.")
    return parser.parse_args(argv)


def main(argv: List[str]) -> int:
    args = parse_args(argv)
    name, path = args.tool
    help_text = _run_help(name, path)
    rendered = PAGE.format(name=name, fence=_fence_for(help_text), help_text=help_text)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    # Rewriting an unchanged page would restamp its mtime and make every downstream
    # step look stale, so write only on a real change.
    if not args.output.exists() or args.output.read_text(encoding="utf-8") != rendered:
        args.output.write_text(rendered, encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
