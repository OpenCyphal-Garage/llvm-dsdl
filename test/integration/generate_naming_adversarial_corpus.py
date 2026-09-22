#!/usr/bin/env python3
# ===----------------------------------------------------------------------===//
#
# Part of the OpenCyphal project, under the MIT licence
# SPDX-License-Identifier: MIT
#
# ===----------------------------------------------------------------------===//

"""Write the DSDL corpus that puts a generated name against another generated name.

`Discovery` rejects a corpus in which two DSDL names reach one generated identifier, so that
class is already gated. The class this corpus is for is the other one: a DSDL name reaching an
identifier the *backend* emits for every type -- a trait the bodies name unqualified, a module
the crate root declares, an accessor composed from another field, the synthetic union tag. No
DSDL name collides with another here, so the corpus generates; each one collides with something
a backend writes.

Every axis below is a defect that reached review rather than a hazard imagined at a desk. A new
axis is a name added to a list, and the gate compiles the result in every backend under that
language's own judge.
"""

from __future__ import annotations

import argparse
import pathlib
import shutil

# Names the generated bodies reach without qualifying them, per language. A definition of one of
# these names declares something the bodies already mean by that word.
#
# Rust: `impl Default for X`, and `Ok`/`Err` as the Result variants, which an empty definition
# reaches because it is a unit struct and so takes the value namespace too.
# Go, TypeScript and Python: the type is a package or module item beside the generated ones.
PRELUDE_TYPES = [
    "Default",
    "Ok",
    "Err",
    "Clone",
    "Option",
    "Result",
    "Box",
    "Vec",
    "Iterator",
    "Error",
    "Object",
    "Number",
    "Array",
    "Bytes",
]

# Keywords across the six targets, which the stropping stage escapes. Each is a type name here so
# the escape is exercised where a type is declared, imported and referred to.
# `const`, `type`, `bool`, `byte`, `enum`, `struct`, `super`, `template`, `self`, `auto`, `and`,
# `or`, `not`, `optional`, `aligned`, `saturated`, `truncated`, `true`, `false` and the DOS device
# names are reserved by DSDL itself and cannot be definition names, so they are not here: the
# frontend rejects them before any backend sees them.
KEYWORD_TYPES = [
    "Break",
    "Class",
    "Def",
    "Impl",
    "Match",
    "Interface",
    "Map",
    "Func",
    "Let",
    "Mut",
    "Pub",
    "Use",
    "Null",
    "Range",
    "Defer",
    "Chan",
    "Var",
    "Return",
    "Await",
    "Async",
    "Lambda",
    "Pass",
]

# Namespace components that reach a name the generated root already declares: the Rust crate root
# carries `extern crate alloc` under no_std and declares its runtime modules, and the bodies of
# every language are written in terms of paths rooted at these.
ROOT_NAMESPACES = ["alloc", "core", "std", "lib", "dsdlRuntime", "dsdlGen", "index"]

# Names the backends emit beside a section's own, as constants or as members.
CLAIMED_MEMBERS = [
    "FULL_NAME",
    "FULL_NAME_AND_VERSION",
    "EXTENT_BYTES",
    "SERIALIZATION_BUFFER_SIZE_BYTES",
    "IS_DEPRECATED",
    "UNION_OPTION_COUNT",
    "WIRE_FLAT",
    "HOST_IMAGE",
]

SEALED = "@sealed\n"


def write(path: pathlib.Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def emit_prelude(root: pathlib.Path) -> None:
    """A definition named after something a backend's bodies name unqualified."""
    for name in PRELUDE_TYPES:
        write(root / "adv" / "prelude" / f"{name}.1.0.dsdl", f"uint8 value\n{SEALED}")
    # An empty definition is a unit struct in Rust, which takes the value namespace as well as the
    # type namespace; that is what makes `Ok` and `Err` reach the Result variants.
    for name in ("Ok", "Err"):
        write(root / "adv" / "prelude_empty" / f"{name}.1.0.dsdl", SEALED)
    # And one module reaching several of them at once, so the imports compete in one scope.
    fields = "\n".join(
        f"adv.prelude.{name}.1.0 f{index}" for index, name in enumerate(PRELUDE_TYPES)
    )
    write(root / "adv" / "prelude" / "Holder.1.0.dsdl", f"{fields}\n{SEALED}")


def emit_keywords(root: pathlib.Path) -> None:
    """A definition named after a keyword in one or more of the targets."""
    for name in KEYWORD_TYPES:
        write(root / "adv" / "keywords" / f"{name}.1.0.dsdl", f"uint8 value\n{SEALED}")
    fields = "\n".join(
        f"adv.keywords.{name}.1.0 f{index}" for index, name in enumerate(KEYWORD_TYPES)
    )
    write(root / "adv" / "keywords" / "Holder.1.0.dsdl", f"{fields}\n{SEALED}")


def emit_root_namespaces(root: pathlib.Path) -> None:
    """A namespace component that reaches a name the generated root declares."""
    for component in ROOT_NAMESPACES:
        write(root / component / "Held.1.0.dsdl", f"uint8 value\n{SEALED}")


def emit_self_shadow(root: pathlib.Path) -> None:
    """A definition whose own name is its dependency's.

    An import shares one namespace with the items the module declares, so the two meet. The
    declaration is the type's public API and keeps the name; the import is what moves.

    Off by default, because TypeScript does not survive it: `projectCompositeImports` allocates no
    local name for an import, so the module imports `Owner`, `makeOwner` and the two body functions
    beside its own, and tsc answers TS2440. Rust reached the same shape and was given an import
    scope; TypeScript has none yet. `--include-self-shadow` is the reproduction, and turning it on
    is the first step of giving TypeScript one.
    """
    write(root / "adv" / "shadow" / "inner" / "Owner.1.0.dsdl", f"uint8 value\n{SEALED}")
    write(
        root / "adv" / "shadow" / "outer" / "Owner.1.0.dsdl",
        f"adv.shadow.inner.Owner.1.0 inner\n{SEALED}",
    )


def emit_claimed_namespaces(root: pathlib.Path) -> None:
    """A namespace component that is itself a name the type role claims.

    A candidate alias is composed from the namespace and the short name, so a component that the
    projection escapes on its own would put its separator in the middle of the alias. `alpha` sorts
    first and takes the bare name, which leaves the other two to be qualified.
    """
    for component in ("alpha", "default", "err"):
        write(root / "adv" / "nsclaim" / component / "Shared.1.0.dsdl", f"uint8 value\n{SEALED}")
    fields = "\n".join(
        f"adv.nsclaim.{component}.Shared.1.0 f_{component}"
        for component in ("alpha", "default", "err")
    )
    write(root / "adv" / "nsclaim" / "Holder.1.0.dsdl", f"{fields}\n{SEALED}")


def emit_cross_namespace(root: pathlib.Path) -> None:
    """One short name in several namespaces, and a module that holds all of them.

    The short name is the whole of the type name wherever a module carries the namespace, so the
    imports of the holder compete and all but the first take a qualifier.
    """
    for component in ("a", "b", "c", "d"):
        write(root / "adv" / "crossns" / component / "Shared.1.0.dsdl", f"uint8 value\n{SEALED}")
    fields = "\n".join(
        f"adv.crossns.{component}.Shared.1.0 f_{component}" for component in ("a", "b", "c", "d")
    )
    write(root / "adv" / "crossns" / "Holder.1.0.dsdl", f"{fields}\n{SEALED}")


def emit_members(root: pathlib.Path) -> None:
    """Members that reach a name a backend emits beside them."""
    base = root / "adv" / "members"
    # A union option whose accessor composes onto the synthetic tag's.
    write(base / "TagUnion.1.0.dsdl", f"@union\nuint32 tag_\nfloat32 real\nuint8 other\n{SEALED}")
    # A field whose name composes onto another field's accessor.
    write(base / "Accessors.1.0.dsdl", f"uint8 get_foo\nuint8 foo\nuint8 set_bar\nuint8 bar\n{SEALED}")
    # Fields named after the constants the backends declare beside them.
    claimed = "\n".join(f"uint8 {name}" for name in CLAIMED_MEMBERS)
    write(base / "Claimed.1.0.dsdl", f"{claimed}\n{SEALED}")
    # The same, as a union, where the option-count constant is emitted too.
    options = "\n".join(f"uint8 {name}" for name in CLAIMED_MEMBERS[:4])
    write(base / "ClaimedUnion.1.0.dsdl", f"@union\n{options}\n{SEALED}")
    # Names that fold onto one identifier under a case projection.
    write(base / "CaseFold.1.0.dsdl", f"uint8 fooBar\nuint8 foo_bar\nuint8 FooBar\nuint8 FOO_BAR\n{SEALED}")


def emit_sections(root: pathlib.Path) -> None:
    """Services named after their own sections, and after the alias that stands for one."""
    base = root / "adv" / "sections"
    body = f"uint8 a\n{SEALED}---\nuint8 b\n{SEALED}"
    for name in ("Request", "Response", "Query"):
        write(base / f"{name}.1.0.dsdl", body)


def emit_deprecated(root: pathlib.Path) -> None:
    """Deprecated definitions, whose declared name carries a marker the projection folds away."""
    base = root / "adv" / "deprecated"
    write(base / "Gone.1.0.dsdl", f"@deprecated\nuint8 value\n{SEALED}")
    write(base / "GoneService.1.0.dsdl", f"@deprecated\nuint8 a\n{SEALED}---\nuint8 b\n{SEALED}")


def emit_version_multiplicity(root: pathlib.Path) -> None:
    """The axes that need several versions of one definition present at once.

    These live under roots of their own because only a versioned type name can express them: C and
    C++ refuse the unversioned form with a sentinel, and Go cannot generate it at all, one package
    being one namespace. A pass that generates unversioned names leaves these out rather than
    asserting a shape the schemes disagree about.
    """
    # Several versions held together, where the namespace runs out of components to qualify with.
    for minor in range(4):
        write(root / "advx" / f"Ver.1.{minor}.dsdl", f"uint8 value\n{SEALED}")
    fields = "\n".join(f"advx.Ver.1.{minor} f{minor}" for minor in range(4))
    write(root / "advx" / "Holder.1.0.dsdl", f"{fields}\n{SEALED}")

    # The same, deprecated: the ordinal follows a name that already ends in the deprecation marker.
    for minor in range(3):
        write(root / "advdep" / f"Legacy.1.{minor}.dsdl", f"@deprecated\nuint8 value\n{SEALED}")
    held = "\n".join(f"advdep.Legacy.1.{minor} f{minor}" for minor in range(3))
    write(root / "advdep" / "Holder.1.0.dsdl", f"{held}\n{SEALED}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--outdir", required=True, type=pathlib.Path)
    parser.add_argument(
        "--include-self-shadow",
        action="store_true",
        help="add the axis where a definition's own name is its dependency's, which TypeScript "
        "does not yet survive",
    )
    parser.add_argument(
        "--no-version-multiplicity",
        action="store_true",
        help="leave out the axes that need several versions of one definition at once, which only "
        "a versioned type name can express",
    )
    args = parser.parse_args()

    root = args.outdir
    if root.exists():
        shutil.rmtree(root)
    root.mkdir(parents=True)

    emit_prelude(root)
    emit_keywords(root)
    emit_root_namespaces(root)
    emit_cross_namespace(root)
    emit_claimed_namespaces(root)
    if args.include_self_shadow:
        emit_self_shadow(root)
    emit_members(root)
    emit_sections(root)
    emit_deprecated(root)
    if not args.no_version_multiplicity:
        emit_version_multiplicity(root)

    written = sorted(p for p in root.rglob("*.dsdl"))
    print(f"naming adversarial corpus: {len(written)} definitions under {root}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
