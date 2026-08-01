# `dsdlc`

`dsdlc` is the primary compiler/codegen driver. Run `dsdlc --help` for languages, target syntax,
and the full option set.

This page covers behavior that switch descriptions do not carry.

## Support code

Support code is everything a backend emits that is not derived from a definition. It is rendered
from content compiled into `dsdlc`, and `--generate-support` selects when it is written.

| Backend | Support artifacts |
| --- | --- |
| `c` | `dsdl_runtime.h` |
| `cpp` | `dsdl_runtime.h`, `dsdl_runtime.hpp` (per profile) |
| `rust` | `Cargo.toml`, `src/dsdl_runtime.rs`, `src/dsdl_runtime_semantic_wrappers.rs` |
| `go` | `go.mod`, `dsdlruntime/dsdl_runtime.go` |
| `ts` | `package.json`, `dsdl_runtime.ts` |
| `python` | `pyproject.toml`, `_dsdl_runtime.py`, `_runtime_loader.py`, `py.typed` |

Under `never`, Python still writes the `__init__.py` chain its generated modules import through.

## Dependency files

`-MD` writes a make-style `.d` beside each generated output listing the `.dsdl` files it was built
from: its own definition plus the transitive closure of the composite types it references.

Some outputs have no `.dsdl` source to name: definitions from the [embedded `uavcan`
catalog](#embedded-uavcan-catalog) and all [support code](#support-code) are compiled into the
binary. Those rules name **the `dsdlc` executable** as their prerequisite instead — upgrading the
compiler rebuilds what it produced. An output mixing local and embedded definitions lists its real
inputs and the executable.

Every path emitted in a depfile or by `--list-inputs` exists on disk, so both can be fed to a build
system verbatim.

## Embedded uavcan catalog

For the standard `uavcan.*` namespace, `dsdlc` ships an embedded catalog used by the `mlir` and
codegen targets. Types referencing core `uavcan` definitions resolve without external `uavcan`
source roots.

The catalog is consulted automatically during dependency resolution, and can be named directly as a
target with the `+` sigil.

`+` targets behave as explicit targets: their dependency closure is generated too, and
`--omit-dependencies` restricts output to what was named. They mix freely with filesystem targets,
and a local definition sharing a type key shadows the embedded one.

Namespace matching is anchored at a dot boundary, so `+uavcan.n` selects nothing rather than
standing in for `+uavcan.node`. A selector matching nothing is an error with a did-you-mean; an
unavailable version reports the versions the catalog carries.

## Deprecation

A definition marked `@deprecated` generates a `Deprecated: …` notice in its documentation comment and
an `IS_DEPRECATED` metadata constant (`DSDL_IS_DEPRECATED` in TypeScript and Python,
`<TYPE>_IS_DEPRECATED_` in C), in every language. Go recognises the `Deprecated: ` doc paragraph, and
TypeScript is additionally given a `/** @deprecated … */` JSDoc block.

C, C++, and Rust additionally get a language-native attribute — `__attribute__((deprecated))`,
`[[deprecated]]`, and `#[deprecated]` respectively — so naming the type produces a compiler
diagnostic. This is **on by default**.

Each generated file suppresses deprecation diagnostics across its own body (`#pragma GCC diagnostic
ignored "-Wdeprecated-declarations"`, or `#![allow(deprecated)]` in Rust). The suppression is scoped
to the generated file: including generated headers is clean under `-Werror`, and only your own code
naming a deprecated type is diagnosed.

The `obj` backend never emits these attributes.
