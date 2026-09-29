#!/usr/bin/env python3
# ===----------------------------------------------------------------------===#
#
# Part of the OpenCyphal project, under the MIT licence
# SPDX-License-Identifier: MIT
#
# ===----------------------------------------------------------------------===#
"""Count where each emitter writes generated text, and hold the counts to a baseline.

An emission site is a place in an emitter's source that writes generated text:

- a call to a `SourceWriter`'s `line`, `open`, `close`, `midway`, `raw` or `blank`;
- a source line that streams a string with `<<`;
- a string literal that ends a generated line, holding `\\n`, outside those;
- an element of a list of generated lines -- a `std::vector<std::string>` that a function outside
  the `BodySpelling` returns -- that holds a string literal or a concatenation.

Each language's sources are counted in three figures. `body` is its `BodySpelling`, which spells a
body the IR states. `declaration` is every other site in its declaration half, the files listed in
DECLARATION: what `CLEAN_CODE.md`'s declaration renderer replaces. `support` is its packaging
writers, the files listed in SUPPORT, which no renderer of the tree writes. Every file in the
emitter directory belongs to one language. The count is a proxy -- a site that writes one token and
a site that assembles a signature count alike -- but it moves when string-built code is added or
removed.

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
FIGURES = ("body", "declaration", "support")

DECLARATION = {
    "C": ("C.cpp", "CHeaderRender.cpp", "CIncludes.cpp"),
    "Cpp": ("Cpp.cpp",),
    "Rust": ("Rust.cpp",),
    "Go": ("Go.cpp",),
    "Ts": ("Ts.cpp",),
    "Python": ("Python.cpp",),
}
SUPPORT = {
    "C": (),
    "Cpp": (),
    "Rust": ("RustPackaging.cpp",),
    "Go": ("GoPackaging.cpp",),
    "Ts": ("TsPackaging.cpp",),
    "Python": ("PythonPackaging.cpp",),
}

TOKEN = re.compile(
    r"""
    (?P<comment>//[^\n]*|/\*.*?\*/)
  | (?P<directive>^[ \t]*\#[^\n]*)
  | (?P<raw>R"(?P<delimiter>[^(\s]*)\(.*?\)(?P=delimiter)")
  | (?P<string>"(?:\\.|[^"\\\n])*")
  | (?P<char>'(?:\\.|[^'\\\n])+')
  | (?P<name>[A-Za-z_]\w*)
  | (?P<punct><<|::|->|[^\sA-Za-z_0-9])
  | (?P<number>[0-9][\w.']*)
    """,
    re.VERBOSE | re.DOTALL | re.MULTILINE,
)
WRITER_CALLS = {"line", "open", "close", "midway", "raw", "blank"}
LIST_APPENDS = {"push_back", "emplace_back"}
SPELLING = re.compile(r"\b(?:class|struct)\s+\w+\s+final\s*:\s*public\s+(?:llvmdsdl::)?BodySpelling\b")
CLOSING = {"(": ")", "{": "}", "[": "]"}


class Token:
    """One token of an emitter's source, and the line it starts on."""

    def __init__(self, kind: str, text: str, line: int) -> None:
        self.kind = kind
        self.text = text
        self.line = line

    def is_string(self) -> bool:
        """Whether the token is a string literal."""
        return self.kind in ("string", "raw")

    def ends_a_line(self) -> bool:
        """Whether the token is a string literal holding a line break."""
        return (self.kind == "string" and "\\n" in self.text) or (self.kind == "raw" and "\n" in self.text)


def tokenise(source: str) -> list[Token]:
    """Split @p source into tokens, dropping comments and preprocessor directives."""
    tokens = []
    for match in TOKEN.finditer(source):
        kind = match.lastgroup if match.lastgroup != "delimiter" else "raw"
        if kind in ("comment", "directive"):
            continue
        tokens.append(Token(kind, match.group(0), source.count("\n", 0, match.start()) + 1))
    return tokens


def matching(tokens: list[Token], opening: int) -> int:
    """The index of the token that closes the bracket at @p opening."""
    close = CLOSING[tokens[opening].text]
    depth = 0
    for index in range(opening, len(tokens)):
        if tokens[index].text == tokens[opening].text:
            depth += 1
        elif tokens[index].text == close:
            depth -= 1
            if depth == 0:
                return index
    raise SystemExit(f"unbalanced {tokens[opening].text} at line {tokens[opening].line}")


def spelling_lines(source: str, path: Path) -> range | None:
    """The lines of @p source's `BodySpelling` subclass, if it declares one."""
    lines = source.splitlines()
    start = next((i for i, line in enumerate(lines) if SPELLING.search(line)), None)
    if start is None:
        return None
    indent = len(lines[start]) - len(lines[start].lstrip())
    end = next((i for i in range(start + 1, len(lines)) if lines[i].startswith(" " * indent + "};")), None)
    if end is None:
        raise SystemExit(f"{path}: the BodySpelling subclass at line {start + 1} does not close")
    return range(start + 1, end + 2)


def built_as_string(tokens: list[Token]) -> bool:
    """Whether an element's @p tokens build a string: a literal, or a concatenation."""
    return any(token.is_string() or token.text == "+" for token in tokens)


def list_elements(tokens: list[Token], first: int, last: int) -> list[int]:
    """The lines each element of a list of generated lines starts on, in the function body between
    @p first and @p last: each element of a braced list assigned or returned, and each append, that
    is built as a string."""
    starts = []
    index = first
    while index < last:
        token = tokens[index]
        if token.text == "{" and tokens[index - 1].text in ("=", "return"):
            close = matching(tokens, index)
            element = index + 1
            cursor = element
            while cursor <= close:
                if tokens[cursor].text in CLOSING and cursor != close:
                    cursor = matching(tokens, cursor) + 1
                    continue
                if tokens[cursor].text == "," or cursor == close:
                    if element < cursor and built_as_string(tokens[element:cursor]):
                        starts.append(tokens[element].line)
                    element = cursor + 1
                cursor += 1
            index = close + 1
            continue
        if token.text in LIST_APPENDS and tokens[index - 1].text == "." and tokens[index + 1].text == "(":
            close = matching(tokens, index + 1)
            if built_as_string(tokens[index + 2 : close]):
                starts.append(token.line)
            index = close + 1
            continue
        index += 1
    return starts


def line_list_bodies(tokens: list[Token]) -> list[tuple[int, int]]:
    """The body of each function returning a `std::vector<std::string>`, as a range of tokens."""
    shape = ["std", "::", "vector", "<", "std", "::", "string", ">"]
    bodies = []
    for index in range(len(tokens) - len(shape) - 2):
        if [t.text for t in tokens[index : index + len(shape)]] != shape:
            continue
        name, parameters = tokens[index + len(shape)], tokens[index + len(shape) + 1]
        if name.kind != "name" or parameters.text != "(":
            continue
        cursor = matching(tokens, index + len(shape) + 1) + 1
        while tokens[cursor].kind == "name":
            cursor += 1
        if tokens[cursor].text == "{":
            bodies.append((cursor, matching(tokens, cursor)))
    return bodies


def sites(path: Path) -> tuple[list[int], range | None]:
    """The line of each emission site in @p path, and the lines of its `BodySpelling`."""
    source = path.read_text()
    tokens = tokenise(source)
    spelling = spelling_lines(source, path)
    found: list[int] = []
    consumed = [False] * len(tokens)

    index = 0
    while index < len(tokens) - 2:
        token = tokens[index]
        if (
            token.text in WRITER_CALLS
            and tokens[index - 1].text in (".", "->")
            and tokens[index + 1].text == "("
            and tokens[index - 2].kind == "name"
        ):
            close = matching(tokens, index + 1)
            found.append(token.line)
            for inner in range(index, close + 1):
                consumed[inner] = True
            index = close + 1
            continue
        index += 1

    streamed = set()
    for index, token in enumerate(tokens[:-1]):
        if token.text == "<<" and tokens[index + 1].is_string() and not consumed[index]:
            streamed.add(token.line)
    found.extend(streamed)

    for index, token in enumerate(tokens):
        if token.ends_a_line() and not consumed[index] and token.line not in streamed:
            found.append(token.line)

    for first, last in line_list_bodies(tokens):
        if spelling is not None and tokens[first].line in spelling:
            continue
        found.extend(list_elements(tokens, first + 1, last))
    return found, spelling


def count(language: str) -> dict[str, int]:
    """Count @p language's emission sites in each of the three figures."""
    figures = dict.fromkeys(FIGURES, 0)
    spellings = 0
    for name in DECLARATION[language]:
        found, spelling = sites(EMITTERS / name)
        if spelling is not None:
            spellings += 1
            figures["body"] += sum(1 for line in found if line in spelling)
        figures["declaration"] += sum(1 for line in found if spelling is None or line not in spelling)
    if spellings != 1:
        raise SystemExit(f"{language}: {spellings} classes deriving BodySpelling in its declaration half")
    for name in SUPPORT[language]:
        found, spelling = sites(EMITTERS / name)
        if spelling is not None:
            raise SystemExit(f"{name}: a support file declares a BodySpelling")
        figures["support"] += len(found)
    return figures


def check_membership() -> None:
    """Fail unless every source in the emitter directory belongs to exactly one language."""
    claimed = [name for language in LANGUAGES for name in DECLARATION[language] + SUPPORT[language]]
    present = sorted(path.name for path in EMITTERS.glob("*.cpp"))
    unclaimed = sorted(set(present) - set(claimed))
    missing = sorted(set(claimed) - set(present))
    doubled = sorted({name for name in claimed if claimed.count(name) > 1})
    if unclaimed or missing or doubled:
        for name in unclaimed:
            print(f"{name}: belongs to no language; add it to DECLARATION or SUPPORT", file=sys.stderr)
        for name in missing:
            print(f"{name}: listed but not present", file=sys.stderr)
        for name in doubled:
            print(f"{name}: listed for two languages", file=sys.stderr)
        raise SystemExit(1)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--baseline", type=Path, help="compare against this file, or rewrite it with --update")
    parser.add_argument("--update", action="store_true", help="rewrite the baseline from this count")
    parser.add_argument("--list", metavar="FILE", help="print the line of each site in this emitter source")
    arguments = parser.parse_args()

    check_membership()
    if arguments.list is not None:
        found, spelling = sites(EMITTERS / arguments.list)
        for line in sorted(found):
            print(f"{arguments.list}:{line}{' body' if spelling is not None and line in spelling else ''}")
        return 0

    counts = {language: count(language) for language in LANGUAGES}
    for language, figures in counts.items():
        print(f"{language:<8}" + "".join(f"   {figure} {figures[figure]:>5}" for figure in FIGURES))
    print(f"{'total':<8}" + "".join(f"   {f} {sum(c[f] for c in counts.values()):>5}" for f in FIGURES))

    if arguments.baseline is None:
        return 0
    if arguments.update:
        arguments.baseline.write_text(json.dumps(counts, indent=2) + "\n")
        print(f"baseline rewritten: {arguments.baseline}")
        return 0

    recorded = json.loads(arguments.baseline.read_text())
    rises = [
        f"{language} {figure}: {counts[language][figure]}, baseline {recorded.get(language, {}).get(figure)}"
        for language in LANGUAGES
        for figure in FIGURES
        if counts[language][figure] > recorded.get(language, {}).get(figure, -1)
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
