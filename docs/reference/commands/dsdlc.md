# `dsdlc`

`dsdlc` is the primary compiler/codegen driver. Run `dsdlc --help` for languages, target syntax,
and the full option set.

This page covers behaviour that switch descriptions do not carry.

## Path arguments

Every option taking a filesystem path reads it the same way. A leading `~` is the invoking user's
home directory, expanded by `dsdlc` rather than by the shell, so a path built by a build system or
read from a configuration file resolves the way one typed at a prompt does. `.` and `..` fold away
lexically, before anything is opened. An absolute path is used as given.

A relative path is measured from `--outdir` when it names a file the run writes, and from the
working directory when it names one the run reads. So `--outdir gen --prune-manifest .dsdlc/types`
writes `gen/.dsdlc/types`, and moving the generated tree is one flag rather than several. Write
outside `--outdir` by giving an absolute path.

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
catalogue](#embedded-uavcan-catalogue) and all [support code](#support-code) are compiled into the
binary. Those rules name **the `dsdlc` executable** as their prerequisite instead — upgrading the
compiler rebuilds what it produced. An output mixing local and embedded definitions lists its real
inputs and the executable.

A [vocabulary](../codegen/vocabulary.md) file passed with `--vocabulary` is an input of every
output the run writes, so each depfile names it and `--list-inputs` lists it.

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
embedded catalogue, and definitions — and a run owns only the files it emits. A run that swept
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

## Embedded uavcan catalogue

For the standard `uavcan.*` namespace, `dsdlc` ships an embedded catalogue used by the `mlir` and
codegen targets. Types referencing core `uavcan` definitions resolve without external `uavcan`
source roots.

The catalogue is consulted automatically during dependency resolution, and can be named directly as a
target with the `+` sigil.

`+` targets behave as explicit targets: their dependency closure is generated too, and
`--omit-dependencies` restricts output to what was named. They mix freely with filesystem targets,
and a local definition sharing a type key shadows the embedded one.

Namespace matching is anchored at a dot boundary, so `+uavcan.n` selects nothing rather than
standing in for `+uavcan.node`. A selector matching nothing is an error with a did-you-mean; an
unavailable version reports the versions the catalogue carries.

## Type versions

Only the **newest version of each type** is generated, and the run reports what it left out:

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

A deprecated definition is usually one a newer version replaced, so this default drops almost every
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
`[[deprecated]]`, and `#[deprecated(note = …)]` respectively — so naming the type produces a compiler
diagnostic. The Rust attribute carries the notice as its message, which rustc prints in the warning.
This is **on by default**.

Generated code does not trip its own attribute. In C the attribute is on the typedef, and generated
code names the type through its struct tag, `struct <name>`, which carries no attribute. In C++ and
Rust the struct is declared as `<name>_` and `<name>` is a deprecated alias of it; generated code
names the struct. Compiling generated code is clean under `-Werror` or `-D warnings`, and only your
own code naming a deprecated type is diagnosed.

The `obj` backend never emits these attributes.

## `@aliasable`

`@aliasable` asserts that a section's serialised form is a contiguous byte image: fixed length,
sealed, not a union, every field a whole number of bytes beginning on a byte boundary, and every
composite field a type that holds the same. A definition that asserts it and does not hold it is an
error, naming the field that blocked the layout.

```dsdl
@aliasable
float32 x
float32 y
float32 z
@sealed
```

It is a flag, like `@union` and `@sealed`, and takes no expression. It scopes to a section, so a
service asserts for its request and its response independently. It requires `@sealed` rather than
implying it: a directive that reads as an assertion should not change the wire format.

Use it on a type whose decode cost is part of its design. Without it, a schema change that costs
such a type its layout is silent — the code still generates, and the reader pays for a decode it
was written not to need.

Every generated type reports the same verdict whether or not it asserts it, as `WIRE_FLAT` and a
`WIRE_FLAT_REASON` naming what blocked it. C, C++, Rust and Go carry `HOST_IMAGE` beside it, which
answers a second question: whether the generated structure is that byte image on the target that
compiles it. The two differ whenever a width the wire carries in five bytes is held in eight, or
where a structure aligns a field the wire does not. A TypeScript or Python object has no byte image
to compare, so those two report the wire verdict alone; the accessors are what the property buys
them.

A wire-flat type's scalar fields, and the elements of its fixed arrays of scalars, have accessors
beside the serialisation functions: a getter that reads one field off a serialised buffer, and a
setter that writes one into it, each at the field's fixed offset and in the member's own type. C
takes the buffer as a pointer and a size, C++ as a span — the type the
[vocabulary](../codegen/vocabulary.md) binds: `std::span` for `std` and `pmr`, and for `autosar`
whatever a `--vocabulary` file names — and the other languages as a slice or a view. An element accessor takes the
element's index after the buffer. A nested composite field has a getter alone, answering the buffer
from the field's offset for the nested type's own accessors to read: C answers a pointer and writes
what remains through a size pointer, which may be null; the other languages answer a span, a slice
or a view, which carries its count. A field at offset nought answers the buffer the getter was
handed; in C that includes a null one, with a size of zero. A getter answers what `deserialize_`
puts in the field, on a short buffer too, where both zero-extend, reads an index at or past the
array's capacity as zero, and in C reads a null buffer as an empty one whatever size it is handed;
a setter answers the runtime's error, refusing such an index, a buffer too short for the field,
and in C a null buffer: a code in C, C++, TypeScript and Python, `Result<(), Error>` in Rust and an
`error` in Go. C spells them `<type>__get_<field>_` and
`<type>__set_<field>_`, C++ as static members `get_<field>` and `set_<field>`, Rust as associated
functions of the same names, Go as `<Type>Get<Field>` and `<Type>Set<Field>`, TypeScript as
`get<Type><Field>` and `set<Type><Field>`, Python as static methods `get_<field>` and `set_<field>`.

A union whose options are all flat and of one length, sealed, has its tag at a fixed offset and
every option at the offset after it, so it has the same accessors: the tag as a member named
`_tag_`, with a getter and a setter in the tag's own width, and each option's accessors at that
one offset. An option's setter writes the value and not the tag; the tag's setter selects, with the
option's tag constant. Such a union is not wire-flat and cannot assert `@aliasable`.

`--aliasable-only` emits the accessors and neither the object type nor the serialisation. Every
targeted type must carry `@aliasable` or be nested by a type that does; each that is neither fails
the run, named. The files keep their names; Go's endianness guard, which belongs to the folded
bodies, is not among them.

`--aliasable-views` holds each composite field of an `@aliasable` type as a view of the buffer the
holder was deserialised from, in place of a decoded copy: the field's bytes and their count, for the
nested type's accessors to read. The holder's deserialise skips the field and its serialise copies
the view. A buffer that ends inside the field leaves a short view, which the accessors read as
zeros past its end and which serialises zero-filled; an initialised object holds an empty view,
which serialises as the nested type's default. An array of an `@aliasable` type is one view per
element: a fixed array's held in place, a variable-length one's beside its count. A holder of a view
is not a host image. A field of a union, and a field whose type is wire-flat without asserting it,
are decoded as usual. C holds a view as `dsdl_runtime_view_t`, a pointer and a size; C++ as the
profile's [span](../codegen/vocabulary.md#span), which the nested type's accessors take as it is
held; Rust as `&'a [u8]`, which gives the holder, and every type that holds one, a lifetime
parameter and a deserialise that borrows the buffer for it; Go as `[]byte`; TypeScript as
`Uint8Array`; Python as `memoryview`. A file names no type it holds only as a view.

A host-image type asserts the verdict where it is compiled: the generated structure carries a
static assertion on its size and on each member's offset. On the byte-image targets — C, `obj`,
C++, Rust and Go — its serialise and deserialise are one move of the object's bytes when the target
triple, the host's when none is given, is little-endian, and the generated code refuses a
big-endian build with the reason and the fix. Under the C++ PMR profile a host image carries no
memory resource: it allocates nothing, and the pointer would widen the structure past the image.

> ⚠️ `@aliasable` is an llvm-dsdl extension. The reference implementation rejects an unknown
> directive, so a namespace using it does not parse under pydsdl or generate under Nunavut. It is
> kept out of the differential corpus for that reason. Proposing it upstream is the intent; until
> then, a namespace meant to stay portable should not use it.
