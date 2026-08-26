# `dsdlc`

`dsdlc` is the primary compiler/codegen driver. Run `dsdlc --help` for languages, target syntax,
and the full option set.

This page covers behaviour that switch descriptions do not carry.

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

## Pruning stale output

`dsdlc` writes what it is asked for; without `--prune-manifest` it does not remove what it wrote
last time. Delete a definition and its generated header stays in `--outdir`, still on the include
path, so code that names a type nobody defines any more keeps compiling.

`--prune-manifest <file>` closes that. The run records its outputs in `<file>` and, on the next run,
deletes the outputs the previous manifest listed that it no longer produces. Directories emptied
by pruning are removed too, so a deleted namespace leaves no shape behind.

**One manifest per invocation, not per output directory.** Generation
[decomposes](#support-code) — a build may split one namespace across several runs for support, the
embedded catalog, and definitions — and a run owns only the files it emits. A run that swept
`--outdir` would delete the files its siblings had just written, so each tranche is given its own
manifest and prunes only what it owns:

```bash
dsdlc -l c --outdir gen --generate-support only  --prune-manifest .dsdlc/support
dsdlc -l c --outdir gen --generate-support never --prune-manifest .dsdlc/builtin +uavcan.node
dsdlc -l c --outdir gen --generate-support never --omit-dependencies \
      --prune-manifest .dsdlc/types dsdl/myns
```

Removals are confined to `--outdir`: a manifest naming anything outside it is a hard error rather
than a deletion, since a manifest is an input and an input that can name any path is a way to turn a
stale file into an arbitrary `rm`. A manifest in an unrecognised format is treated as absent — a
format change should cost one stale file, not every configured tree.

The flag is ignored under `--dry-run` and the `--list-*` modes, which imply it. A dry run that
deleted files while reporting that it wrote none would be worse than either.

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

## Type versions

Only the **newest version of each type** is generated. `uavcan` carries 189 definitions across 167
type names; a default run generates 167 of them and says so:

```
note: generating the newest version of each type; 22 older version(s) were not generated.
```

A corpus holding several versions of a type forces a choice on everything downstream — Go compiles a
namespace as one package and cannot hold two versions of a type at all, and C and C++ share a scope
across versions — while most code speaks one version.

Newest is per **full name**, not per major version: `Foo.1.0`, `Foo.1.1` and `Foo.2.0` leave only
`Foo.2.0`. Per-major would match Cyphal's compatibility model, where majors are incompatible, but it
leaves a type that has two majors still carrying two versions — the thing this exists to prevent.

Naming a version keeps it, and affects no other type. Any of the version-precise target spellings
does it; `dsdlc --help` gives their syntax.

### Limits

The narrowing applies to the set of types asked for, not to the finished output. A version that
survives may still *reference* an older one — a field of type `Dep.1.0` in a definition that is
itself the newest — and dependency resolution keeps what it needs:

```
note: kept 1 older version(s) that a newer definition still references: ns.Dep:1:0
```

So the output is usually single-version per type without being guaranteed to be, and the backends
keep their own guards, described below.

### Deprecated types

A definition is deprecated because a newer version replaced it, so this default drops almost every
`@deprecated` type as a side effect. Generating every version brings them back, with the deprecation
attributes and notices described under [Deprecation](#deprecation).

## Type-name versioning

A generated type name does not carry the definition's version: `uavcan.node.Heartbeat.1.0` becomes
`Heartbeat` in C++, Go, TypeScript and Python, `uavcan__node__Heartbeat` in C, and
`uavcan_node_Heartbeat` in Rust. Code that handles two versions of one type at once needs them kept
apart, and can ask for the version to be included.

Output file names carry the version either way, so the choice changes what you *write*, not what you
include or import. Under both, the name follows from the definition alone; it never depends on what
else was in the invocation.

What an unversioned name costs depends on what scopes the type, and arises only where a corpus
carries two versions of one type — which the newest-version default prevents:

| Language | Scope holding the type | Two versions, unversioned |
|---|---|---|
| Rust, TypeScript, Python | a module per type *and version* | no conflict |
| C, C++ | a scope shared across versions | generates; a translation unit including both stops on an `#error` |
| Go | a package per namespace, shared across versions | cannot be generated; `dsdlc` refuses, naming the type and its versions |

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
