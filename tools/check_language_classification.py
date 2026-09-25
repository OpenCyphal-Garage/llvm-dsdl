#!/usr/bin/env python3
# ===----------------------------------------------------------------------===#
#
# Part of the OpenCyphal project, under the MIT licence
# SPDX-License-Identifier: MIT
#
# ===----------------------------------------------------------------------===#
"""Reject a decision about a language made anywhere but its row.

`llvmdsdl/Support/LanguageTraits.h` is the classification: one row per language, stating what the
language can express, what its generated interface hands a body, and how the output composes its
declarations. Code that needs one of those facts reads the row. Code that compares a language
instead answers the question again, and a language added later has to be found in every such
place -- which is how `CLEAN_CODE.md`'s *Discipline* found capabilities decided in the driver and
five naming files.

So the compiler's sources may name a language as a value but may not decide on one:

* a comparison with a `Language` enumerator, `language == Language::Cpp`;
* a `case Language::...` label;
* a comparison with a language's `--target-language` spelling, `name == "cpp"`.

A spelling table -- how a language writes a name, a literal or a type -- is keyed on the language
by definition, and may do the first two. The driver dispatches to each backend, and may switch.
Those are the files in `ALLOWED`, each with its reason.

    python3 tools/check_language_classification.py
    python3 tools/check_language_classification.py --self-test
"""

from __future__ import annotations

import argparse
import re
import sys
import tempfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent

# Where the compiler's own sources are. Test drivers and the scripts that judge generated code are
# about one language each by construction, and are not held to this.
SCANNED = ("lib", "include", "tools/dsdlc")
SUFFIXES = {".cpp", ".h", ".hpp", ".inc", ".td"}

LANGUAGE_NAMES = ("c", "cpp", "rust", "go", "ts", "python")

COMPARISON = re.compile(
    r"(?:==|!=)\s*(?:llvmdsdl::)?Language::\w+|(?:llvmdsdl::)?\bLanguage::\w+\s*(?:==|!=)"
)
CASE_LABEL = re.compile(r"\bcase\s+(?:llvmdsdl::)?Language::\w+")
STRING_LITERAL = re.compile(r'"(?:\\.|[^"\\])*"')
NAME_COMPARISON = re.compile(
    r'(?:==|!=)\s*"(?:' + "|".join(LANGUAGE_NAMES) + r')"|"(?:' + "|".join(LANGUAGE_NAMES) + r')"\s*(?:==|!=)'
)

# What may decide on a language, and why. A path here is relative to the repository root.
ALLOWED = {
    "lib/Support/NamingPolicy.cpp": ({"comparison", "case"}, "how each language spells a name"),
    "lib/CodeGen/ConstantLiteralRender.cpp": ({"comparison", "case"}, "how each language spells a literal"),
    "lib/CodeGen/StorageTypeTokens.cpp": ({"comparison", "case"}, "how each language spells a storage type"),
    "tools/dsdlc/main.cpp": ({"case"}, "the driver dispatches to each backend"),
}


def strip_comments(text: str) -> list[str]:
    """Return @p text's lines with comments blanked and string literals kept.

    A comment that mentions `language == Language::C` explains a decision rather than making one.
    """
    out: list[str] = []
    in_block = False
    for line in text.splitlines():
        kept = []
        i = 0
        in_string: str | None = None
        while i < len(line):
            ch = line[i]
            if in_block:
                if line.startswith("*/", i):
                    in_block = False
                    i += 2
                else:
                    i += 1
                continue
            if in_string:
                kept.append(ch)
                if ch == "\\" and i + 1 < len(line):
                    kept.append(line[i + 1])
                    i += 2
                    continue
                if ch == in_string:
                    in_string = None
                i += 1
                continue
            if line.startswith("//", i):
                break
            if line.startswith("/*", i):
                in_block = True
                i += 2
                continue
            if ch in "\"'":
                in_string = ch
            kept.append(ch)
            i += 1
        out.append("".join(kept))
    return out


def check_file(path: Path, relative: str) -> list[str]:
    """Return a line per decision on a language that @p path makes and may not."""
    allowed_kinds = ALLOWED.get(relative, (set(), ""))[0]
    problems = []
    for number, line in enumerate(strip_comments(path.read_text(errors="replace")), start=1):
        # An enumerator inside a string literal is text, not a decision; a spelling compared with one
        # is the decision, so the name check reads the literals and the others do not.
        code = STRING_LITERAL.sub('""', line)
        for kind, pattern, what, text in (
            ("comparison", COMPARISON, "compares a Language enumerator", code),
            ("case", CASE_LABEL, "switches on a Language enumerator", code),
            ("name", NAME_COMPARISON, "compares a language's --target-language spelling", line),
        ):
            if kind in allowed_kinds:
                continue
            match = pattern.search(text)
            if match:
                problems.append(f"{relative}:{number}: {what}: `{match.group(0).strip()}`")
    return problems


def check_tree(root: Path) -> list[str]:
    """Return every problem under @p root's scanned directories."""
    problems = []
    for directory in SCANNED:
        base = root / directory
        if not base.is_dir():
            continue
        for path in sorted(base.rglob("*")):
            if path.suffix in SUFFIXES and path.is_file():
                problems += check_file(path, path.relative_to(root).as_posix())
    return problems


def self_test() -> int:
    """Hold the check to cases it must refuse and cases it must allow."""
    refused = {
        "lib/CodeGen/A.cpp": "bool f(Language language) { return language == Language::Cpp; }\n",
        "lib/CodeGen/B.cpp": "bool f(Language l) { return llvmdsdl::Language::Go != l; }\n",
        "lib/CodeGen/C.cpp": "int f(Language l) {\n  switch (l) {\n  case Language::C:\n    return 1;\n  }\n}\n",
        "tools/dsdlc/D.cpp": 'bool f(llvm::StringRef name) { return name == "python"; }\n',
        "include/llvmdsdl/E.h": 'inline bool f(std::string n) { return "rust" != n; }\n',
        "tools/dsdlc/main.cpp": "bool f(Language l) { return l == Language::Rust; }\n",
    }
    allowed = {
        "lib/CodeGen/F.cpp": "void f() { project(Language::Cpp, role, name); }\n",
        "lib/CodeGen/G.cpp": "// language == Language::C once decided this; the row does now.\nint g;\n",
        "lib/CodeGen/H.cpp": "/* case Language::Go:\n   was here */\nint h;\n",
        "lib/CodeGen/I.cpp": 'const char* s = "a == Language::C";\nbool x = name == "cpp2";\n',
        "lib/Support/NamingPolicy.cpp": "bool f(Language l) { return l == Language::Go; }\n",
        "tools/dsdlc/main.cpp": "void f(Language l) {\n  switch (l) {\n  case Language::C:\n    break;\n  }\n}\n",
        "test/unit/J.cpp": "bool f(Language l) { return l == Language::C; }\n",
    }
    failures = []
    with tempfile.TemporaryDirectory() as scratch:
        root = Path(scratch)
        for group, expect in ((refused, True), (allowed, False)):
            for relative, text in group.items():
                for stale in root.rglob("*"):
                    if stale.is_file():
                        stale.unlink()
                path = root / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(text)
                found = bool(check_tree(root))
                if found != expect:
                    failures.append(f"{relative}: expected {'a refusal' if expect else 'nothing'}, got the opposite")
    for failure in failures:
        print(failure, file=sys.stderr)
    if failures:
        return 1
    print(f"self-test: {len(refused)} refused and {len(allowed)} allowed as expected")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", type=Path, default=REPO_ROOT, help="the repository to check")
    parser.add_argument("--self-test", action="store_true", help="check the check rather than the tree")
    arguments = parser.parse_args()
    if arguments.self_test:
        return self_test()
    problems = check_tree(arguments.root)
    if not problems:
        print("no language is decided on outside its row")
        return 0
    for problem in problems:
        print(problem, file=sys.stderr)
    print(
        f"\n{len(problems)} decision(s) on a language outside llvmdsdl/Support/LanguageTraits.h. Read the"
        " row: add a column there if the fact is not one yet. A spelling table or the driver's dispatch"
        " belongs in ALLOWED with its reason.",
        file=sys.stderr,
    )
    return 1


if __name__ == "__main__":
    sys.exit(main())
