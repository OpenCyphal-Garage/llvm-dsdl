# `dsdlc`

`dsdlc` is the primary compiler/codegen driver.

## Languages

`--target-language` supports:

- `ast`
- `mlir`
- `c`
- `cpp`
- `rust`
- `go`
- `ts`
- `python`
- `obj`

## Common options

- `--target-language, -l <lang>`
- `--outdir, -O <dir>`
- `--lookup-dir, -I <dir>` (repeatable)
- `--jobs, -j <N>` parallelism hint (currently used by `obj` compile stage)
- `--dry-run, -d`
- `--list-inputs`
- `--list-outputs`
- `-MD`
- `--optimize-lowered-serdes`
- `--no-deprecation-attributes`

## Dependency files

`-MD` writes a make-style `.d` beside each generated output listing the `.dsdl` files it was built
from: its own definition plus the transitive closure of the composite types it references.

Definitions from the [embedded `uavcan` catalog](#embedded-uavcan-catalog) are compiled into the
binary and have no source file, so outputs drawing on them name **the `dsdlc` executable** as the
prerequisite instead — upgrading the compiler rebuilds what its catalog produced. An output mixing
local and embedded definitions lists its real inputs and the executable.

Every path emitted in a depfile or by `--list-inputs` exists on disk, so both can be fed to a build
system verbatim.

## Embedded uavcan catalog

For the standard `uavcan.*` namespace, `dsdlc` ships an embedded catalog used by the `mlir` and
codegen targets. Types referencing core `uavcan` definitions resolve without external `uavcan`
source roots. `--no-embedded-uavcan` disables it, after which such references must resolve against
a `--lookup-dir`.

The catalog is consulted automatically during dependency resolution, and can be named directly as a
target with `+`.

### Targeting the catalog with `+`

A positional target beginning with `+` names the embedded catalog instead of the filesystem:

```bash
dsdlc --target-language c --outdir out +uavcan.node.Heartbeat.1.0
```

| Selector | Selects |
| --- | --- |
| `+uavcan` | Every type in the catalog |
| `+uavcan.node` | Every type under a namespace |
| `+uavcan.node.Heartbeat` | Every version of one type |
| `+uavcan.node.Heartbeat.1.0` | One exact version |

`+` targets behave as explicit targets: their dependency closure is generated too, and
`--omit-dependencies` restricts output to what was named. They mix freely with filesystem targets,
and a local definition sharing a type key shadows the embedded one.

Namespace matching is anchored at a dot boundary, so `+uavcan.n` selects nothing rather than
standing in for `+uavcan.node`. A selector matching nothing is an error with a did-you-mean; an
unavailable version reports the versions the catalog carries.

Requires `--target-language` of `mlir` or a codegen language, and conflicts with
`--no-embedded-uavcan`. The sigil stays significant after `--`; a file literally named `+x` is
reached as `./+x`.

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

`--no-deprecation-attributes` suppresses the attributes, leaving the notice and the metadata constant
in place.

The `obj` backend never emits these attributes.

## Object backend options

- `--target-endianness <little|big>` (required for `obj`; wire is little-endian on both — `big` disables the zero-copy view fast-path, see [Endianness semantics](../codegen/object.md#endianness-semantics))
- `--target-triple <triple>`
- `--obj-archive-name <name>`
- `--obj-abi-language <c|cpp>`
- `--obj-no-archive`

## Examples

Generate C++ (`std` + `pmr`):

```bash
dsdlc --target-language cpp path/to/ns --cpp-profile both --outdir out/cpp
```

Generate object files only:

```bash
dsdlc --target-language obj path/to/ns \
  --target-endianness little \
  --obj-no-archive \
  --jobs 8 \
  --outdir out/obj
```
