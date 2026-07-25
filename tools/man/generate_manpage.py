#!/usr/bin/env python3
#===----------------------------------------------------------------------===#
#
# Part of the OpenCyphal project, under the MIT licence
# SPDX-License-Identifier: MIT
#
#===----------------------------------------------------------------------===#

"""Generate a roff man page from a tool's own ``--help`` output.

Debian policy wants a man page for every program in ``/usr/bin``, and the help
text is already the authoritative description of each tool's interface. Deriving
the page from it means the two cannot drift: a new flag documented in ``--help``
appears in the man page at the next build, and there is no second copy to forget.

Two help dialects are in play, so the parser handles both rather than assuming one:

* ``dsdlc`` and ``dsdld`` print ``NAME`` / ``SYNOPSIS`` / ... sections, which map
  onto roff ``.SH`` sections directly.
* ``dsdl-opt`` prints LLVM's ``cl::opt`` format -- ``OVERVIEW:``, ``USAGE:``,
  then option groups.

What cannot be mapped structurally is emitted verbatim inside a ``.nf``/``.fi``
block. That is deliberate: a fragile parser producing subtly wrong roff is worse
than a faithful preformatted rendering of text the tool actually printed.

Note that this runs the tool it documents, so it needs a host-executable binary.
That is fine for native builds; a cross-build would need the pages generated on
the host side or shipped pre-built.
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

# Section headers as printed by dsdlc/dsdld: all-caps, at column 0.
_SECTION_RE = re.compile(r"^([A-Z][A-Z0-9 /-]*[A-Z0-9])$")


def _roff_escape(text: str) -> str:
    r"""Escape text for roff.

    A leading dot or apostrophe starts a roff request, and a backslash starts an
    escape; help text containing either would otherwise be silently reinterpreted
    as markup rather than printed.
    """
    text = text.replace("\\", "\\e")
    if text.startswith(".") or text.startswith("'"):
        text = "\\&" + text
    return text


def _run_help(tool: Path) -> str:
    # A tool that blocks instead of printing help would hang the build, so cap it
    # and fail loudly. stdin is closed for the same reason.
    try:
        proc = subprocess.run(
            [str(tool), "--help"],
            stdin=subprocess.DEVNULL,
            capture_output=True,
            text=True,
            timeout=60,
        )
    except subprocess.TimeoutExpired:
        raise SystemExit(f"{tool} did not respond to --help within 60s")
    out = proc.stdout.strip() or proc.stderr.strip()
    if not out:
        raise SystemExit(f"{tool} --help produced no output")
    return out


def _split_sections(help_text: str) -> dict[str, list[str]]:
    """Split dsdlc/dsdld-style help into {SECTION: [lines]}; empty if not that dialect."""
    sections: dict[str, list[str]] = {}
    current: str | None = None
    for line in help_text.splitlines():
        if _SECTION_RE.match(line):
            current = line
            sections[current] = []
        elif current is not None:
            sections[current].append(line)
    return sections


def _dedent_block(lines: list[str]) -> list[str]:
    stripped = [ln for ln in lines if ln.strip()]
    if not stripped:
        return []
    indent = min(len(ln) - len(ln.lstrip()) for ln in stripped)
    return [ln[indent:] if len(ln) >= indent else ln for ln in lines]


def _verbatim(lines: list[str]) -> list[str]:
    """Render lines as a preformatted roff block."""
    out = [".nf"]
    out += [_roff_escape(ln) if ln.strip() else "" for ln in _dedent_block(lines)]
    out.append(".fi")
    # Trim blank lines at the block edges so sections do not gain stray spacing.
    while len(out) > 2 and out[1] == "":
        del out[1]
    while len(out) > 2 and out[-2] == "":
        del out[-2]
    return out


def _summary_from(
    sections: dict[str, list[str]], help_text: str, tool_name: str, description: str | None
) -> str:
    """One-line 'name - description' for the NAME section."""
    if "NAME" in sections:
        for line in sections["NAME"]:
            if line.strip():
                return line.strip()
    # LLVM dialect: "OVERVIEW: dsdl-opt" -- which for dsdl-opt just repeats the
    # program name and says nothing, hence the caller-supplied description.
    m = re.search(r"^OVERVIEW:\s*(.+)$", help_text, re.MULTILINE)
    if m:
        overview = m.group(1).strip()
        if overview and overview != tool_name:
            return f"{tool_name} - {overview}"
    if description:
        return f"{tool_name} - {description}"
    return f"{tool_name} - part of the llvm-dsdl toolchain"


def _synopsis_from(sections: dict[str, list[str]], help_text: str, tool_name: str) -> list[str]:
    if "SYNOPSIS" in sections:
        return [ln.strip() for ln in sections["SYNOPSIS"] if ln.strip()]
    usage = re.findall(r"^USAGE:\s*(.+)$", help_text, re.MULTILINE)
    if usage:
        return [u.strip() for u in usage]
    return [f"{tool_name} [options]"]


def render(
    tool_name: str,
    version: str,
    help_text: str,
    date: str = "",
    description: str | None = None,
) -> str:
    sections = _split_sections(help_text)
    body: list[str] = []

    # .TH takes name, manual section, date, source, manual title. The date comes
    # from the release metadata rather than the clock, so rebuilding the same
    # source yields a byte-identical page.
    body.append(f'.TH {tool_name.upper()} 1 "{date}" "llvm-dsdl {version}" "llvm-dsdl Manual"')
    body.append(".SH NAME")
    body.append(_roff_escape(_summary_from(sections, help_text, tool_name, description)))

    body.append(".SH SYNOPSIS")
    body += _verbatim(_synopsis_from(sections, help_text, tool_name))

    # Reproduce the remaining sections in the order the tool printed them, so the
    # page tracks the help text's own organisation.
    consumed = {"NAME", "SYNOPSIS"}
    if sections:
        for name, lines in sections.items():
            if name in consumed or not any(ln.strip() for ln in lines):
                continue
            body.append(f".SH {name}")
            body += _verbatim(lines)
    else:
        # LLVM dialect, or anything unrecognised: everything below the USAGE line.
        remainder = help_text.splitlines()
        cut = 0
        for index, line in enumerate(remainder):
            if line.startswith("USAGE:"):
                cut = index + 1
                break
        body.append(".SH OPTIONS")
        body += _verbatim(remainder[cut:])

    body.append(".SH SEE ALSO")
    others = [t for t in ("dsdlc", "dsdl-opt", "dsdld") if t != tool_name]
    body.append(", ".join(f"\\fB{t}\\fR(1)" for t in others))

    body.append(".SH AUTHOR")
    # Plain text rather than a .UR/.UE hyperlink pair: those need link text between
    # them, and an empty pair is a lint error in mandoc.
    body.append("The OpenCyphal project.")
    body.append(".PP")
    body.append("Report bugs at https://github.com/OpenCyphal-Garage/llvm-dsdl/issues")

    return "\n".join(body) + "\n"


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tool", type=Path, required=True, help="Path to the built executable.")
    parser.add_argument("--name", help="Program name; defaults to the executable's file name.")
    parser.add_argument("--version", required=True, help="Version string for the page header.")
    parser.add_argument(
        "--date", default="",
        help="Page date (YYYY-MM-DD). Pass the release date, not today's, to keep the "
             "generated page reproducible.")
    parser.add_argument(
        "--description",
        help="One-line description, used only when the tool's --help does not carry one.")
    parser.add_argument("--output", type=Path, required=True, help="Destination .1 file.")
    args = parser.parse_args(argv)

    if not args.tool.is_file():
        raise SystemExit(f"tool not found: {args.tool}")

    name = args.name or args.tool.name
    page = render(name, args.version, _run_help(args.tool), args.date, args.description)

    args.output.parent.mkdir(parents=True, exist_ok=True)
    tmp = args.output.with_suffix(args.output.suffix + ".tmp")
    tmp.write_text(page, encoding="utf-8")
    tmp.replace(args.output)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
