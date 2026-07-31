#!/usr/bin/env python3
# ===----------------------------------------------------------------------===#
#
# Part of the OpenCyphal project, under the MIT licence
# SPDX-License-Identifier: MIT
#
# ===----------------------------------------------------------------------===#
"""Render the showroom's generated tree into browsable Markdown under docs/showroom.

The docs are generated rather than committed: the documentation workflow runs inside the toolshed
container, builds dsdlc, and runs this script as part of publishing, so what the site shows is the
compiler's current output rather than a copy of it. This script is what
`cmake --build <dir> --target showroom-docs` runs.

Each type page carries the authored DSDL in full, the wire-layout facts, and a declaration excerpt
per language. Excerpts rather than whole files: the complete generated tree is tens of thousands of
lines, and what a prospective user wants to see is the shape of the data structure and whether the
documentation survived the trip. The full output is what the `showroom` build target produces.

The index page is the showroom README rendered for the site rather than prose maintained here, so
that a contributor editing the README -- the file they would naturally reach for -- also updates the
documentation site, and the dirty-tree check in CI enforces the two staying in step.
"""

from __future__ import annotations

import argparse
import dataclasses
import os
import re
import sys
from pathlib import Path

# --------------------------------------------------------------------------------------------------
# Variant descriptions.
#
# `subdir` is the path below the variant root at which the lanyard namespace appears, and `suffixes`
# are the generated file extensions to look for. `style` selects the declaration extractor.

@dataclasses.dataclass(frozen=True)
class Variant:
    key: str
    title: str
    language: str
    fence: str
    subdir: str
    suffixes: tuple[str, ...]
    style: str
    note: str = ""


VARIANTS: tuple[Variant, ...] = (
    Variant("c", "C", "c", "c", "", (".h",), "c"),
    Variant("cpp-std", "C++ (std)", "cpp", "cpp", "", (".hpp",), "cpp"),
    Variant("cpp-pmr", "C++ (pmr)", "cpp", "cpp", "", (".hpp",), "cpp",
            "Polymorphic-allocator profile: variable-length fields route through std::pmr."),
    Variant("cpp-autosar", "C++ (autosar)", "cpp", "cpp", "", (".hpp",), "cpp",
            "AUTOSAR C++14 subset profile."),
    Variant("rust-std", "Rust (std)", "rust", "rust", "src", (".rs",), "rust"),
    Variant("rust-no-std-alloc", "Rust (no-std)", "rust", "rust", "src", (".rs",), "rust",
            "no_std + alloc profile, as a flight-controller firmware build would use."),
    Variant("go", "Go", "go", "go", "", (".go",), "go"),
    Variant("ts", "TypeScript", "ts", "typescript", "", (".ts",), "ts"),
    Variant("python", "Python", "python", "python", "", (".py",), "python"),
)

# Some backends nest the namespace under a package directory whose name is a CLI argument rather than
# a fixed string, so the namespace root is located by search instead of assumed.
NAMESPACE_ROOT = "lanyard"

# The documentation theme gives a code block about eighty monospace characters before it starts
# scrolling horizontally, and that width does not grow with the window -- a wider viewport goes to the
# sidebars. Generated *code* may exceed it and scroll; some serializer signatures are unavoidably long.
# Comments may not: their width is chosen by whoever writes the DSDL, so a comment that scrolls is a
# defect in the definition rather than a fact about the code.
#
# The widest comment prefix any backend adds is eight characters (`  /* ` ... ` */` in C, `    /// ` in
# Rust), so the DSDL budget is the rendered budget minus that.
RENDERED_CODE_COLUMNS = 80
MAX_COMMENT_PREFIX = 8
DSDL_COMMENT_COLUMNS = RENDERED_CODE_COLUMNS - MAX_COMMENT_PREFIX

# The marker the lanyard definitions actually write. It is prose, and the annotation is its first
# sentence: several definitions continue past it into commentary about why that transport is the
# floor, which belongs on the page but not in a table cell.
TRANSPORT_PATTERN = re.compile(r"^#\s*LEAST SUPPORTED TRANSPORT:\s*(.+?)\s*$", re.MULTILINE)


@dataclasses.dataclass
class SectionFacts:
    """Wire-layout facts for one serializable section (a message, or a service request/response)."""

    label: str
    extent_bytes: int | None = None
    max_serialized_bytes: int | None = None


@dataclasses.dataclass
class TypeInfo:
    full_name: str
    major: int
    minor: int
    port_id: int | None
    namespace: str
    short_name: str
    dsdl_path: Path
    dsdl_text: str
    sections: list[SectionFacts] = dataclasses.field(default_factory=list)

    @property
    def versioned_name(self) -> str:
        return f"{self.full_name}.{self.major}.{self.minor}"

    @property
    def slug(self) -> str:
        return f"{self.full_name}.{self.major}.{self.minor}".replace(".", "_")

    @property
    def stem(self) -> str:
        """Base name of the generated artifact, e.g. `EscStatus_1_0`."""
        return f"{self.short_name}_{self.major}_{self.minor}"

    @property
    def is_service(self) -> bool:
        return "\n---\n" in f"\n{self.dsdl_text}\n"

    @property
    def least_supported_transport(self) -> str:
        """The transport floor the definition declares, or "unspecified" when it declares none.

        Nested types -- a mission item, a mixer, an enumeration -- are not published on their own,
        so the question does not apply to them and they say so.
        """
        match = TRANSPORT_PATTERN.search(self.dsdl_text)
        if not match:
            return "unspecified"
        # ". " and not "." : the values contain periods of their own ("CAN 2.0B").
        return match.group(1).split(". ")[0].rstrip(".").strip()

    @property
    def summary(self) -> str:
        """First paragraph of the type's documentation block.

        The paragraph, not the first line: DSDL comments are hard-wrapped, so a first-line summary
        cuts wherever the author happened to hit the margin ("published by the flight controller at
        50"). The paragraph ends at the first blank comment line, which is where the author actually
        ended the thought.
        """
        paragraph: list[str] = []
        for line in self.dsdl_text.splitlines():
            stripped = line.strip()
            if stripped.startswith("#"):
                body = stripped.lstrip("#").strip()
                if not body:
                    break  # a bare `#` ends the opening paragraph
                paragraph.append(body)
                continue
            if stripped or paragraph:
                break
        return " ".join(paragraph)


# --------------------------------------------------------------------------------------------------
# DSDL discovery.

DSDL_NAME = re.compile(r"^(?:(?P<port>\d+)\.)?(?P<name>[A-Za-z_][A-Za-z0-9_]*)\.(?P<major>\d+)\.(?P<minor>\d+)\.dsdl$")


def check_comment_widths(types: list[TypeInfo]) -> list[str]:
    """Report DSDL comment lines too wide to render without horizontal scrolling."""
    problems: list[str] = []
    for info in types:
        for number, line in enumerate(info.dsdl_text.splitlines(), start=1):
            stripped = line.lstrip()
            if not stripped.startswith("#"):
                continue
            text = stripped[1:]
            text = text[1:] if text.startswith(" ") else text
            if len(text) > DSDL_COMMENT_COLUMNS:
                problems.append(
                    f"{info.dsdl_path}:{number}: comment is {len(text)} columns, "
                    f"budget is {DSDL_COMMENT_COLUMNS}"
                )
    return problems


def check_transport_declarations(types: list[TypeInfo]) -> list[str]:
    """Report published types that declare no least-supported transport.

    A tripwire for a silent failure this table has already had once: the marker was renamed on one
    side, nothing matched, and every row rendered as "unspecified" -- which reads as an answer, not
    as an absence, so the page looked fine while saying nothing. A default that can quietly become
    universal needs something asserting it stays rare.

    Scoped to types with a fixed port ID because those are exactly the ones the question applies to:
    they are published on their own, so they have a transport to be sized for. A nested type is a
    field of something else and inherits its budget.
    """
    return [
        f"{info.dsdl_path}: {info.versioned_name} has a fixed port ID but declares no "
        f"`# LEAST SUPPORTED TRANSPORT:` -- it would render as \"unspecified\""
        for info in types
        if info.port_id is not None and info.least_supported_transport == "unspecified"
    ]


def discover_types(dsdl_root: Path) -> list[TypeInfo]:
    types: list[TypeInfo] = []
    for path in sorted(dsdl_root.rglob("*.dsdl")):
        match = DSDL_NAME.match(path.name)
        if not match:
            print(f"warning: skipping unrecognized DSDL file name: {path}", file=sys.stderr)
            continue
        relative_dir = path.parent.relative_to(dsdl_root)
        namespace_parts = [dsdl_root.name, *relative_dir.parts] if relative_dir.parts != (".",) else [dsdl_root.name]
        namespace_parts = [p for p in namespace_parts if p not in (".", "")]
        namespace = ".".join(namespace_parts)
        types.append(
            TypeInfo(
                full_name=f"{namespace}.{match.group('name')}",
                major=int(match.group("major")),
                minor=int(match.group("minor")),
                port_id=int(match.group("port")) if match.group("port") else None,
                namespace=namespace,
                short_name=match.group("name"),
                dsdl_path=path,
                dsdl_text=path.read_text(encoding="utf-8"),
            )
        )
    types.sort(key=lambda t: (t.namespace, t.short_name, t.major, t.minor))
    return types


# --------------------------------------------------------------------------------------------------
# Wire facts, read from the generated C headers.
#
# The C backend emits EXTENT_BYTES_ and SERIALIZATION_BUFFER_SIZE_BYTES_ as preprocessor constants,
# keyed by a mangled symbol whose FULL_NAME_AND_VERSION_ sibling names the section. Reading them is
# both easier and more stable than re-deriving the layout or parsing the MLIR module.

DEFINE = re.compile(r"^#define\s+(?P<symbol>\S+?)_(?P<key>FULL_NAME_AND_VERSION|EXTENT_BYTES|SERIALIZATION_BUFFER_SIZE_BYTES)_\s+(?P<value>.+?)\s*$")


def load_c_facts(c_root: Path) -> dict[str, SectionFacts]:
    """Map a versioned section name (`pkg.Type.Request.1.0`) to its facts."""
    # Keyed by (header, symbol), not by symbol alone: the C symbol prefix carries the type name but
    # not the version, so `EscStatus_1_0.h` and `EscStatus_2_0.h` both define
    # `lanyard__propulsion__EscStatus_EXTENT_BYTES_`. Merging them globally lets one version silently
    # overwrite the other's facts -- and since the winner is decided by directory iteration order, the
    # result is not even stable between runs.
    by_symbol: dict[tuple[Path, str], dict[str, str]] = {}
    for header in sorted(c_root.rglob("*.h")):
        for line in header.read_text(encoding="utf-8").splitlines():
            match = DEFINE.match(line)
            if match:
                by_symbol.setdefault((header, match.group("symbol")), {})[match.group("key")] = match.group("value")

    facts: dict[str, SectionFacts] = {}
    for fields in by_symbol.values():
        name = fields.get("FULL_NAME_AND_VERSION")
        if not name:
            continue
        name = name.strip().strip('"')
        label = "Request" if name.split(".")[-3:-2] == ["Request"] else (
            "Response" if name.split(".")[-3:-2] == ["Response"] else "Message"
        )
        facts[name] = SectionFacts(
            label=label,
            extent_bytes=_parse_int(fields.get("EXTENT_BYTES")),
            max_serialized_bytes=_parse_int(fields.get("SERIALIZATION_BUFFER_SIZE_BYTES")),
        )
    return facts


def _parse_int(raw: str | None) -> int | None:
    if raw is None:
        return None
    digits = re.match(r"\d+", raw.strip())
    return int(digits.group(0)) if digits else None


def attach_facts(types: list[TypeInfo], facts: dict[str, SectionFacts]) -> None:
    for info in types:
        if info.is_service:
            for label in ("Request", "Response"):
                key = f"{info.full_name}.{label}.{info.major}.{info.minor}"
                if key in facts:
                    info.sections.append(facts[key])
        else:
            key = info.versioned_name
            if key in facts:
                info.sections.append(facts[key])


# --------------------------------------------------------------------------------------------------
# Declaration excerpts.
#
# Each extractor finds the aggregate declarations for a type, together with the documentation block
# immediately above them, and stops before the serialization machinery. The openers are anchored at
# column zero because every backend emits top-level declarations unindented.

COMMENT_PREFIXES = ("//", "///", "/*", "*", "#")


def _leading_comment_start(lines: list[str], index: int) -> int:
    """Walk back over the contiguous comment block (and attributes) directly above `index`."""
    start = index
    while start > 0:
        candidate = lines[start - 1].strip()
        if not candidate:
            break
        if candidate.startswith(COMMENT_PREFIXES) or candidate.startswith(("#[", "@dataclass", "@")):
            start -= 1
            continue
        break
    return start


def _extract_braced(lines: list[str], opener: re.Pattern[str], closer: re.Pattern[str]) -> list[str]:
    """Collect every `opener ... closer` block at column zero, with its leading comment block."""
    chunks: list[str] = []
    index = 0
    while index < len(lines):
        if opener.match(lines[index]):
            start = _leading_comment_start(lines, index)
            end = index
            while end < len(lines) and not closer.match(lines[end]):
                end += 1
            if end < len(lines):
                chunks.append("\n".join(lines[start : end + 1]))
                index = end + 1
                continue
        index += 1
    return chunks


def _extract_ts(lines: list[str]) -> list[str]:
    """TypeScript renders a struct as an interface but a union as a discriminated `type` alias."""
    chunks = _extract_braced(lines, re.compile(r"^export interface\s+\w+\s*\{"), re.compile(r"^\}"))
    index = 0
    opener = re.compile(r"^export type\s+\w+\s*=")
    while index < len(lines):
        if opener.match(lines[index]):
            start = _leading_comment_start(lines, index)
            end = index
            while end < len(lines) and not lines[end].rstrip().endswith(";"):
                end += 1
            if end < len(lines):
                chunks.append("\n".join(lines[start : end + 1]))
                index = end + 1
                continue
        index += 1
    return chunks


def _extract_python(lines: list[str]) -> list[str]:
    """Python keeps its methods inside the class, so cut at the first method definition."""
    chunks: list[str] = []
    index = 0
    opener = re.compile(r"^class\s+\w+")
    while index < len(lines):
        if opener.match(lines[index]):
            start = _leading_comment_start(lines, index)
            end = index + 1
            while end < len(lines):
                stripped = lines[end].strip()
                if stripped.startswith(("def ", "@")) or (lines[end] and not lines[end][0].isspace()):
                    break
                end += 1
            chunks.append("\n".join(lines[start:end]).rstrip())
            index = end
            continue
        index += 1
    return chunks


# A `@deprecated` definition carries a language-native attribute, which lands between the closing
# brace and the typedef name in C and between `struct` and the name in C++. Both patterns tolerate it;
# without that the deprecated types are the one kind of definition these pages cannot show.
EXTRACTORS = {
    "c": lambda lines: _extract_braced(
        lines, re.compile(r"^typedef struct\b"), re.compile(r"^\}\s*(?:__attribute__\(\(.*?\)\)\s*)*\w+;")
    ),
    "cpp": lambda lines: _extract_braced(
        lines, re.compile(r"^struct\s+(?:\[\[.*?\]\]\s+)?\w+\s*\{"), re.compile(r"^\};")
    ),
    "rust": lambda lines: _extract_braced(lines, re.compile(r"^pub struct\s+\w+\s*\{"), re.compile(r"^\}")),
    "go": lambda lines: _extract_braced(lines, re.compile(r"^type\s+\w+\s+struct\s*\{"), re.compile(r"^\}")),
    "ts": _extract_ts,
    "python": _extract_python,
}


def find_generated_file(variant_root: Path, variant: Variant, info: TypeInfo) -> Path | None:
    """Locate the artifact for `info` within a variant's output tree.

    The namespace directory is found by search rather than construction because some backends nest it
    under a package directory named from a CLI argument.
    """
    search_root = variant_root / variant.subdir if variant.subdir else variant_root
    if not search_root.is_dir():
        return None
    candidates = {info.stem.lower(), _snake(info.short_name) + f"_{info.major}_{info.minor}"}
    for suffix in variant.suffixes:
        for path in sorted(search_root.rglob(f"*{suffix}")):
            if NAMESPACE_ROOT not in path.parts:
                continue
            if path.stem.lower() in candidates:
                return path
    return None


def _snake(name: str) -> str:
    step = re.sub(r"(.)([A-Z][a-z]+)", r"\1_\2", name)
    return re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", step).lower()


# --------------------------------------------------------------------------------------------------
# Rendering.

def render_type_page(info: TypeInfo, generated_root: Path) -> str:
    out: list[str] = []
    out.append(f"# {info.versioned_name}")
    out.append("")
    if info.summary:
        out.append(info.summary)
        out.append("")

    out.append("| | |")
    out.append("|---|---|")
    out.append(f"| Full name | `{info.full_name}` |")
    out.append(f"| Version | {info.major}.{info.minor} |")
    out.append(f"| Kind | {'Service' if info.is_service else 'Message'} |")
    out.append(f"| Fixed port ID | {info.port_id if info.port_id is not None else 'none (nested type)'} |")
    out.append(f"| Least supported transport | {info.least_supported_transport} |")
    out.append("")

    if info.sections:
        out.append("## Wire layout")
        out.append("")
        out.append("| Section | Extent (bytes) | Max serialized (bytes) |")
        out.append("|---|---:|---:|")
        for section in info.sections:
            extent = "sealed" if section.extent_bytes is None else str(section.extent_bytes)
            maximum = "-" if section.max_serialized_bytes is None else str(section.max_serialized_bytes)
            out.append(f"| {section.label} | {extent} | {maximum} |")
        out.append("")
        out.append(
            "A sealed type reports its extent as its exact serialized size; a delimited type reports the "
            "declared `@extent`, which bounds what a reader must be prepared to receive."
        )
        out.append("")

    out.append("## Definition")
    out.append("")
    out.append("```python")
    out.append(info.dsdl_text.rstrip())
    out.append("```")
    out.append("")

    out.append("## Generated code")
    out.append("")
    out.append(
        "Declaration excerpts only -- the serialization bodies are omitted for length. Build the "
        "`showroom` target for the complete output in every language and profile."
    )
    out.append("")

    # One heading per variant rather than a tab set. Content tabs need the theme to supply the CSS
    # that hides the inactive panes; under a theme that does not, every pane renders at once with the
    # radio inputs showing -- which is worse than plain sections. Headings also put each language in
    # the page's table of contents, which the tabs never did.
    for variant in VARIANTS:
        variant_root = generated_root / variant.key
        path = find_generated_file(variant_root, variant, info)
        out.append(f"### {variant.title}")
        out.append("")
        if variant.note:
            out.append(variant.note)
            out.append("")
        if path is None:
            out.append("_Not generated for this variant._")
            out.append("")
            continue
        chunks = EXTRACTORS[variant.style](path.read_text(encoding="utf-8").splitlines())
        if not chunks:
            out.append("_No aggregate declaration found in the generated artifact._")
            out.append("")
            continue
        out.append(f"```{variant.fence}")
        for index, chunk in enumerate(chunks):
            if index:
                out.append("")
            out.extend(chunk.splitlines())
        out.append("```")
        out.append("")

    return "\n".join(out).rstrip() + "\n"


# --------------------------------------------------------------------------------------------------
# Index page.
#
# The index is the showroom README rendered for the documentation site, not a second document that
# says the same things in different words. Two directives let the one source serve both readers:
#
#   <!-- showroom-docs: skip -->        drops the enclosing section from the site page
#   <!-- showroom-docs: type-table -->  expands to the generated table of types
#
# Both are HTML comments, so GitHub renders the README with no trace of them. A directive occupies a
# line of its own, and `skip` is only honoured as the first non-blank line of a section body.

DIRECTIVE = re.compile(r"^<!--\s*showroom-docs:\s*(?P<name>[a-z-]+)\s*-->\s*$")
HEADING = re.compile(r"^(?P<hashes>#{1,6})\s")
FENCE = re.compile(r"^\s*(?:```|~~~)")

# Links in the README resolve against examples/showroom/. The site page lives at docs/showroom/, so
# every link that survives into it has to be re-pointed: at the site when the target is under docs/,
# and at the repository otherwise. Kept in step with `repo_url` in mkdocs.yml.
REPO_BLOB_URL = "https://github.com/OpenCyphal-Garage/llvm-dsdl/blob/main"
INLINE_LINK = re.compile(r"\[(?P<text>[^]]*)\]\((?P<target>[^)\s]+)\)")

REPO_ROOT = Path(__file__).resolve().parents[2]

INDEX_BANNER = (
    "<!-- Generated from examples/showroom/README.md by tools/showroom/build_showroom_index.py.\n"
    "     Edit the README and re-run the `showroom-docs` target; edits made here are overwritten. -->"
)


class ReadmeError(Exception):
    """A directive in the showroom README could not be honoured."""


def _rewrite_link_target(target: str, readme_dir: Path, output_dir: Path) -> str:
    """Re-point a README-relative link so that it still resolves from docs/showroom/index.md."""
    if "://" in target or target.startswith(("#", "/", "mailto:", "<")):
        return target
    path_part, _, fragment = target.partition("#")
    anchor = f"#{fragment}" if fragment else ""
    if not path_part:
        return target
    resolved = (readme_dir / path_part).resolve()
    try:
        relative_to_repo = resolved.relative_to(REPO_ROOT)
    except ValueError:
        # Outside the source tree entirely; there is nothing sensible to rewrite it to.
        return target
    if relative_to_repo.parts[:1] == ("docs",):
        return Path(os.path.relpath(resolved, output_dir)).as_posix() + anchor
    return f"{REPO_BLOB_URL}/{relative_to_repo.as_posix()}{anchor}"


def _rewrite_links(line: str, readme_dir: Path, output_dir: Path) -> str:
    def substitute(match: re.Match) -> str:
        target = _rewrite_link_target(match.group("target"), readme_dir, output_dir)
        return f"[{match.group('text')}]({target})"

    return INLINE_LINK.sub(substitute, line)


def render_type_table(types: list[TypeInfo]) -> list[str]:
    rows = ["| Type | Port | Least supported transport | Kind |", "|---|---:|---|---|"]
    for info in types:
        port = str(info.port_id) if info.port_id is not None else "--"
        kind = "service" if info.is_service else "message"
        rows.append(
            f"| [`{info.versioned_name}`](types/{info.slug}.md) | {port} | {info.least_supported_transport} | {kind} |"
        )
    return rows


def _section_is_skipped(lines: list[str], body_start: int) -> bool:
    """True when the first non-blank line of a section body is the `skip` directive."""
    for line in lines[body_start:]:
        if not line.strip():
            continue
        if HEADING.match(line):
            return False
        directive = DIRECTIVE.match(line)
        return bool(directive and directive.group("name") == "skip")
    return False


def render_index(types: list[TypeInfo], readme: Path, output_dir: Path) -> str:
    source = readme.read_text(encoding="utf-8").splitlines()
    readme_dir = readme.parent.resolve()

    rendered: list[str] = [INDEX_BANNER, ""]
    skip_level: int | None = None
    in_fence = False

    for number, line in enumerate(source, start=1):
        if FENCE.match(line):
            in_fence = not in_fence
            if skip_level is None:
                rendered.append(line)
            continue

        if not in_fence:
            heading = HEADING.match(line)
            if heading:
                level = len(heading.group("hashes"))
                if skip_level is not None and level <= skip_level:
                    skip_level = None
                if skip_level is None and _section_is_skipped(source, number):
                    skip_level = level
                    continue

            directive = DIRECTIVE.match(line)
            if directive:
                name = directive.group("name")
                if name == "skip":
                    if skip_level is None:
                        raise ReadmeError(
                            f"{readme}:{number}: `showroom-docs: skip` is only honoured as the first "
                            "line of a section body"
                        )
                elif name == "type-table":
                    if skip_level is None:
                        rendered.extend(render_type_table(types))
                else:
                    raise ReadmeError(
                        f"{readme}:{number}: unknown directive `showroom-docs: {name}`"
                    )
                continue

        if skip_level is None:
            rendered.append(line if in_fence else _rewrite_links(line, readme_dir, output_dir))

    # Dropping a section leaves the blank lines that surrounded it stacked against each other.
    collapsed: list[str] = []
    in_fence = False
    for line in rendered:
        if FENCE.match(line):
            in_fence = not in_fence
        elif not in_fence and not line.strip() and collapsed and not collapsed[-1].strip():
            continue
        collapsed.append(line)

    return "\n".join(collapsed).rstrip() + "\n"


# --------------------------------------------------------------------------------------------------

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dsdl-root", required=True, type=Path)
    parser.add_argument("--generated-root", required=True, type=Path)
    parser.add_argument("--mlir-file", required=False, type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument("--readme", required=True, type=Path,
                        help="showroom README; rendered into the index page")
    args = parser.parse_args()

    if not args.dsdl_root.is_dir():
        print(f"error: DSDL root does not exist: {args.dsdl_root}", file=sys.stderr)
        return 1
    if not args.readme.is_file():
        print(f"error: README does not exist: {args.readme}", file=sys.stderr)
        return 1
    if not args.generated_root.is_dir():
        print(
            f"error: generated root does not exist: {args.generated_root}\n"
            "       build the `showroom` target first",
            file=sys.stderr,
        )
        return 1

    types = discover_types(args.dsdl_root)
    if not types:
        print(f"error: no DSDL definitions under {args.dsdl_root}", file=sys.stderr)
        return 1

    problems = check_comment_widths(types)
    if problems:
        print(
            f"error: {len(problems)} DSDL comment line(s) exceed the "
            f"{DSDL_COMMENT_COLUMNS}-column budget and would scroll in the rendered docs:",
            file=sys.stderr,
        )
        for problem in problems[:20]:
            print(f"  {problem}", file=sys.stderr)
        if len(problems) > 20:
            print(f"  ... and {len(problems) - 20} more", file=sys.stderr)
        return 1

    problems = check_transport_declarations(types)
    if problems:
        print(
            f"error: {len(problems)} published definition(s) declare no least-supported transport:",
            file=sys.stderr,
        )
        for problem in problems:
            print(f"  {problem}", file=sys.stderr)
        return 1

    c_root = args.generated_root / "c"
    if c_root.is_dir():
        attach_facts(types, load_c_facts(c_root))
    else:
        print("warning: C output not found; wire-layout facts will be omitted", file=sys.stderr)

    # Rendered before anything is written so that a malformed directive fails without leaving the
    # docs tree half-updated.
    try:
        index_text = render_index(types, args.readme, args.output_dir)
    except ReadmeError as error:
        print(f"error: {error}", file=sys.stderr)
        return 1

    types_dir = args.output_dir / "types"
    types_dir.mkdir(parents=True, exist_ok=True)

    # Drop pages for types that no longer exist so a rename cannot leave an orphan behind.
    expected = {f"{info.slug}.md" for info in types}
    for stale in types_dir.glob("*.md"):
        if stale.name not in expected:
            stale.unlink()

    for info in types:
        (types_dir / f"{info.slug}.md").write_text(render_type_page(info, args.generated_root), encoding="utf-8")

    (args.output_dir / "index.md").write_text(index_text, encoding="utf-8")

    print(f"showroom docs: wrote {len(types)} type pages and an index to {args.output_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
