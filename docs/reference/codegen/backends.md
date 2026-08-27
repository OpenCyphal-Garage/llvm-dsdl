# Backend Overview

`dsdlc` shares one frontend/semantic pipeline and dispatches to multiple backend emitters.

## Targets

- `c`: C headers + implementation
- `cpp`: C++ model/runtime outputs (`std`, `pmr`, `autosar`)
- `rust`: crate/module output with profile/runtime controls
- `go`: module/package output
- `ts`: typed model + runtime helpers
- `python`: package + runtime loader/specialization
- `obj`: compiled `.o` + optional `.a`

## Shared principles

- One semantic interpretation per DSDL source set
- Contract-validated lowering boundaries
- Deterministic file planning
- Backend parity coverage in CI

## Identifier naming

A DSDL name is not always a legal identifier in the target: `break` is a keyword almost everywhere,
`fooBar` and `foo_bar` fold together in the backends that snake_case, and a name like `FULL_NAME`
collides with a constant every generated type already carries. `dsdlc` renames only where it must,
by one rule per language, and tells you when it does.

Renames that change a **path** — an output file name or a package directory — are reported without
being asked for, because nothing else would tell you:

```console
$ dsdlc --target-language go my_dsdl --go-module example.com/m --outdir out
my_dsdl/ns/Break.1.0.dsdl:1:1: note: 'Break' is emitted as 'break_' for target language 'go'
```

Renames *inside* a file are not reported: the generated source carries them, and the volume would
bury the path-level notes. The naming manifest records every name — per target language, each type's
file stem and namespace path and every field and constant identifier — so a build rule can reference
a generated symbol without reimplementing the projection. Targets that emit no source (`ast`, `mlir`)
report every language at once.

Two distinct types that would land on the same output file or the same type name are rejected rather
than renamed, because choosing which one to rename would depend on directory traversal order and
make the output non-reproducible.

### Python packaging constraint

The generated package mirrors the DSDL namespace, so `uavcan.time` becomes a directory named `time`.
Under absolute imports that is harmless. It stops being harmless if the **output directory itself**
is placed on `sys.path`, because `import time` may then resolve to the generated package instead of
the standard library.

Put the *parent* of the root namespace on `sys.path` — or install the output as a package — and
import through the full path (`import uavcan.time.SynchronizedTimestamp_1_0`). `dsdlc` does not
rename the directory to avoid this: the namespace is correct, renaming it would propagate into every
import path, and it would diverge from the five other backends over a hazard that correct packaging
already avoids.

## Object code path

The `obj` lane runs executable lowering and emits compiled artifacts via host compiler toolchains.

See [Object Backend](object.md) for endianness, ABI, and artifact details.
