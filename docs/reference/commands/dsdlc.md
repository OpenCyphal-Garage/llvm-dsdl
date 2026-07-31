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

## Deprecation

A definition marked `@deprecated` always generates a `Deprecated: …` notice in its documentation
comment and an `IS_DEPRECATED` metadata constant (`DSDL_IS_DEPRECATED` in TypeScript and Python,
`<TYPE>_IS_DEPRECATED_` in C), in every language. Two backends get native tooling support from this
for free: Go recognises the `Deprecated: ` doc paragraph, and TypeScript is additionally given a
`/** @deprecated … */` JSDoc block, which is what `tsc` and editors read.

C, C++, and Rust additionally get a language-native attribute — `__attribute__((deprecated))`,
`[[deprecated]]`, and `#[deprecated]` respectively — so that naming the type produces a compiler
diagnostic. This is **on by default**: a deprecation only the reader of a comment can see is a
deprecation nobody acts on, and two dozen definitions in the standard `uavcan` namespace are
deprecated (all of `uavcan.file`, `uavcan.internet.udp`, `ExecuteCommand.1.0`–`1.2`, the
`magnetic_field_strength` SI types, and more).

Each generated file suppresses deprecation diagnostics across its own body (`#pragma GCC diagnostic
ignored "-Wdeprecated-declarations"`, or `#![allow(deprecated)]` in Rust). Generated code must not warn
about itself: a deprecated type is named by its own declaration, by its serializer signatures, and by
any type that embeds it as a field — `uavcan.file.Path.1.0` is deprecated and embedded by five other
definitions. The suppression is scoped to the generated file, so *including* generated headers is
clean under `-Werror` and only your own code naming a deprecated type is diagnosed.

`--no-deprecation-attributes` suppresses the attributes, leaving the notice and the metadata constant
in place. Reach for it when a `-Werror` build depends on a deprecated definition that has no migration
target yet. `--emit-deprecation-attributes` is still accepted and now selects the default.

The `obj` backend never emits these attributes: it compiles the C or C++ it generates as part of its
own pipeline, and warnings there would be about code the user never sees.

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

## Note

For exact up-to-date switch semantics, run:

```bash
dsdlc --help
```
