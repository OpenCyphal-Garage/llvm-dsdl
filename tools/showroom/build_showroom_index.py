#!/usr/bin/env python3
# ===----------------------------------------------------------------------===#
#
# Part of the OpenCyphal project, under the MIT licence
# SPDX-License-Identifier: MIT
#
# ===----------------------------------------------------------------------===#
"""Render the showroom's generated tree into browsable Markdown under docs/showroom.

The docs are checked into the repository because the documentation workflow builds mkdocs on a
runner that never compiles dsdlc, and therefore cannot generate them itself. This script is what
`cmake --build <dir> --target showroom-docs` runs; CI re-runs it and fails on a dirty tree.

Each type page carries the authored DSDL in full, the wire-layout facts, and a declaration excerpt
per language. Excerpts rather than whole files: the complete generated tree is tens of thousands of
lines, and what a prospective user wants to see is the shape of the data structure and whether the
documentation survived the trip. The full output is what the `showroom` build target produces.
"""

from __future__ import annotations

import argparse
import dataclasses
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

TIER_PATTERN = re.compile(r"^#\s*TRANSPORT TIER:\s*(.+?)\.?\s*$", re.MULTILINE)


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
    def transport_tier(self) -> str:
        match = TIER_PATTERN.search(self.dsdl_text)
        return match.group(1).strip() if match else "unspecified"

    @property
    def summary(self) -> str:
        """First line of the type's documentation block."""
        for line in self.dsdl_text.splitlines():
            stripped = line.strip()
            if stripped.startswith("#"):
                return stripped.lstrip("#").strip()
            if stripped:
                break
        return ""


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


EXTRACTORS = {
    "c": lambda lines: _extract_braced(lines, re.compile(r"^typedef struct\b"), re.compile(r"^\}\s*\w+;")),
    "cpp": lambda lines: _extract_braced(lines, re.compile(r"^struct\s+\w+\s*\{"), re.compile(r"^\};")),
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
    out.append(f"| Transport tier | {info.transport_tier} |")
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

    for variant in VARIANTS:
        variant_root = generated_root / variant.key
        path = find_generated_file(variant_root, variant, info)
        out.append(f'=== "{variant.title}"')
        out.append("")
        if variant.note:
            out.append(f"    {variant.note}")
            out.append("")
        if path is None:
            out.append("    _Not generated for this variant._")
            out.append("")
            continue
        chunks = EXTRACTORS[variant.style](path.read_text(encoding="utf-8").splitlines())
        if not chunks:
            out.append("    _No aggregate declaration found in the generated artifact._")
            out.append("")
            continue
        out.append(f"    ```{variant.fence}")
        for chunk in chunks:
            for line in chunk.splitlines():
                out.append(f"    {line}" if line else "")
            out.append("")
        out.append("    ```")
        out.append("")

    return "\n".join(out).rstrip() + "\n"


def render_index(types: list[TypeInfo]) -> str:
    out: list[str] = []
    out.append("# Showroom")
    out.append("")
    out.append(
        "`lanyard` is a fictional vendor namespace of aerial-vehicle datatypes. It exists so that you "
        "can see what dsdlc produces for definitions shaped like the ones you are about to write, "
        "before you write them. Nothing here is a test fixture and nothing here is regulated: these "
        "are the sort of vendor-specific types a drone programme adds alongside the standard `uavcan` "
        "namespace, and they lean on the standard types wherever a standard type exists."
    )
    out.append("")
    out.append("Generate the full output for every supported language and profile with:")
    out.append("")
    out.append("```bash")
    out.append("cmake --build <build-dir> --target showroom")
    out.append("```")
    out.append("")
    out.append(
        "The tree lands under `<build-dir>/showroom/<variant>/`. The pages below carry the authored "
        "DSDL, the resulting wire-layout facts, and a declaration excerpt per language."
    )
    out.append("")

    out.append("## What each type demonstrates")
    out.append("")
    out.append("| Type | Port | Tier | Kind |")
    out.append("|---|---:|---|---|")
    for info in types:
        port = str(info.port_id) if info.port_id is not None else "--"
        kind = "service" if info.is_service else "message"
        out.append(
            f"| [`{info.versioned_name}`](types/{info.slug}.md) | {port} | {info.transport_tier} | {kind} |"
        )
    out.append("")

    out.append("## Versioning")
    out.append("")
    out.append(
        "Five migrations are laid out across the namespace, each answering a different question about "
        "when a change forces a major version bump."
    )
    out.append("")
    out.append("| Transition | Breaking | What it shows |")
    out.append("|---|---|---|")
    out.append(
        "| `ThrottleCommand` 0.1 -> 1.0 | n/a | A `0.x` definition promises nothing; promotion to 1.0 is "
        "where the compatibility promise begins, not a compatible change. |"
    )
    out.append(
        "| `VehicleState` 1.0 -> 1.1 | no | A field appended inside an unchanged `@extent`. Both minor "
        "versions share port 6210 and interoperate in both directions. |"
    )
    out.append(
        "| `VehicleState` 1.1 -> 2.0 | yes | A field retyped, a field replaced, and the extent grown -- "
        "any one of which forces a new major version and a new port. |"
    )
    out.append(
        "| `EscStatus` 1.0 -> 2.0 | yes | `@sealed` is a one-way door: a sealed type cannot gain a "
        "field, so extensibility costs a major version. |"
    )
    out.append(
        "| `LegacyBatteryPoll` 1.0 -> `BatteryStatus` 2.0 | yes | `@deprecated` marking a superseded "
        "service while both halves of the migration stay in the namespace. |"
    )
    out.append("")

    out.append("## Transport tiers")
    out.append("")
    out.append(
        "Every definition states the transport it was sized for, and most of them assert that budget "
        "with `@assert _offset_.max <= ...` so that a layout change breaks the build rather than "
        "quietly spilling into a multi-frame transfer."
    )
    out.append("")
    out.append("| Tier | Budget | Examples |")
    out.append("|---|---|---|")
    out.append(
        "| Classic CAN | 7 payload bytes in one frame | `EscStatus.1.0`, hand-packed to exactly 56 bits |"
    )
    out.append(
        "| CAN FD | 63 payload bytes in one frame | `GlobalPosition.1.0`, `RcInput.1.0`, `GimbalStatus.1.0` |"
    )
    out.append(
        "| Cyphal/UDP | a datagram, kilobytes | `MissionPlan.1.0`, `CameraFrameMetadata.1.0` |"
    )
    out.append("")

    out.append("## Language features covered")
    out.append("")
    out.append("| Feature | Where |")
    out.append("|---|---|")
    out.append("| `@sealed` | `EscStatus.1.0`, `RcInput.1.0`, `SubsystemReport.1.0` |")
    out.append("| `@extent` | `VehicleState.1.0`, `MissionPlan.1.0`, `Waypoint.1.0` |")
    out.append("| `@union` | `ControlSurfaces.1.0` |")
    out.append("| `@assert` | `EscStatus.1.0`, `GlobalPosition.1.0`, `CapturePhoto.1.0` |")
    out.append("| `@print` | `GlobalPosition.1.0` |")
    out.append("| `@deprecated` | `LegacyBatteryPoll.1.0` |")
    out.append("| Service request/response sections | `UploadMission.1.0`, `CapturePhoto.1.0` |")
    out.append("| Non-byte-aligned scalars | `EscStatus.1.0` (`uint14`, `int12`, `int9`, `uint4`) |")
    out.append("| `void` padding | `EscStatus.1.0`, `GlobalPosition.1.0`, `SubsystemReport.1.0` |")
    out.append("| Fixed-size arrays | `GlobalPosition.1.0` (`float16[9]`), `RcInput.1.0` (`uint11[16]`) |")
    out.append("| Variable-length arrays | `ThrottleCommand.1.0`, `MissionPlan.1.0`, `BatteryStatus.2.0` |")
    out.append("| Arrays of composites | `MissionPlan.1.0` (delimited), `SystemHealth.1.0` (sealed) |")
    out.append("| Cast modes | `CameraFrameMetadata.1.0` (`truncated` against `saturated`) |")
    out.append("| Constants as enumerations | `FlightMode.1.0`, `Waypoint.1.0`, `GlobalPosition.1.0` |")
    out.append("| Reuse of standard `uavcan` types | throughout; see `SubsystemReport.1.0` |")
    out.append("")

    out.append("## A note on documentation")
    out.append("")
    out.append(
        "Every comment block in these definitions reaches the generated source in all six languages, "
        "attached to the type, field, or constant it documents. That is the reason the definitions are "
        "commented as heavily as they are: the DSDL is the only place the documentation is written, and "
        "the generated code is where most people will read it."
    )
    out.append("")
    out.append(
        "Comment placement follows the OpenCyphal convention used by the regulated namespace -- the "
        "block goes *after* the field it documents and is followed by a blank line. A block placed "
        "before a field attaches to whatever precedes it instead."
    )
    out.append("")
    out.append("!!! note")
    out.append("")
    out.append(
        "    `@deprecated` reaches generated source in all six languages as a `Deprecated: ...` notice "
        "appended to the type's documentation, plus an `IS_DEPRECATED` metadata constant. Go reads the "
        "notice as a real deprecation, and TypeScript additionally gets a `/** @deprecated */` JSDoc "
        "block. Compile-time enforcement in C, C++, and Rust is opt-in behind "
        "`dsdlc --emit-deprecation-attributes`, because it turns documentation into a build diagnostic."
    )
    out.append("")

    return "\n".join(out).rstrip() + "\n"


# --------------------------------------------------------------------------------------------------

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dsdl-root", required=True, type=Path)
    parser.add_argument("--generated-root", required=True, type=Path)
    parser.add_argument("--mlir-file", required=False, type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    args = parser.parse_args()

    if not args.dsdl_root.is_dir():
        print(f"error: DSDL root does not exist: {args.dsdl_root}", file=sys.stderr)
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

    c_root = args.generated_root / "c"
    if c_root.is_dir():
        attach_facts(types, load_c_facts(c_root))
    else:
        print("warning: C output not found; wire-layout facts will be omitted", file=sys.stderr)

    types_dir = args.output_dir / "types"
    types_dir.mkdir(parents=True, exist_ok=True)

    # Drop pages for types that no longer exist so a rename cannot leave an orphan behind.
    expected = {f"{info.slug}.md" for info in types}
    for stale in types_dir.glob("*.md"):
        if stale.name not in expected:
            stale.unlink()

    for info in types:
        (types_dir / f"{info.slug}.md").write_text(render_type_page(info, args.generated_root), encoding="utf-8")

    (args.output_dir / "index.md").write_text(render_index(types), encoding="utf-8")

    print(f"showroom docs: wrote {len(types)} type pages and an index to {args.output_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
