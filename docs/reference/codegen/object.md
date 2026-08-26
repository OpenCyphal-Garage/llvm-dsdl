# Object Backend (`-l obj`)

The object backend compiles generated sources into static objects and optional archives.

## ABI lanes

## C lane (`--obj-abi-language c`)

- Stages generated C under `.obj_stage_c`
- Compiles C translation units to `.o`
- Optionally archives into `.a`
- Publishes the staged headers into `--outdir`

## Headers

The staged sources are an intermediate this backend compiles itself, but the headers are the only way
to call what ends up in the archive, so they are copied into `--outdir` and reported by
`--list-outputs` like any other output. The layout matches what the `c` backend produces, so
`-I<outdir>` plus the archive is a complete interface from a single invocation.

The staging directories remain, and remain private: nothing should read `.obj_stage_c` or
`.obj_stage_cpp`, and their contents are not declared outputs.

## C++ lane (`--obj-abi-language cpp`)

- Uses canonical profile-agnostic C++ ABI types
- Exports C++ ABI symbols plus C-callable shim symbols
- Stages under `.obj_stage_cpp` (including nested C stage)

### Published layout

This lane publishes several header sets rather than one, because it has several interfaces. Each gets
a directory, and `-I<outdir>` plus the archive is the whole thing:

```
<outdir>/
  std/<ns>/X_1_0.hpp          the profile a C++ consumer includes -- pick one
  pmr/<ns>/X_1_0.hpp
  autosar/<ns>/X_1_0.hpp
  abi/<ns>/X_1_0_abi.hpp      the canonical ABI type the profiles alias
  c_shim/<ns>/X_1_0_c_shim.h  the C-callable shim symbols
  c/<ns>/X_1_0.h              the C struct the ABI type wraps
  c/dsdl_runtime.h            the C runtime
  dsdl_cppabi_runtime.hpp     the C++ runtime
  dsdl_runtime.h              forwards to `c/dsdl_runtime.h`
```

Include a profile header and the rest follows: the profile aliases the ABI type, which includes the
C struct under `c/`. The root `dsdl_runtime.h` exists so that a translation unit reaching the C
runtime by its unqualified name finds it wherever it was included from.

Note that the C headers are under `c/`, which is *not* where the C lane puts them. They are an
implementation detail of the ABI type here rather than the interface, and the generated ABI headers
include them at that path.

## Endianness semantics

The DSDL wire format is little-endian on every target, so `serialize_`/`deserialize_`
perform explicit little-endian bit assembly and are independent of host endianness.
`--target-endianness` selects the codegen/legalization strategy; it does not change
wire-contract semantics. There is no byte-swap step.

- `little`: all fast paths are available, including the zero-copy view helpers
  (`try_deserialize_view_` / `try_serialize_view_`) for alias-eligible fixed-size
  sealed layouts.
- `big`: `serialize_` / `deserialize_` are fully supported and produce byte-identical
  wire output to `little` (verified by the object-backend smoke test). The zero-copy
  view helpers are disabled and return `-DSDL_RUNTIME_ERROR_INVALID_ARGUMENT`.

**Note:** the big-endian path is validated by compiling the
`LLVMDSDL_TARGET_ENDIANNESS_BIG` code path and asserting wire byte-parity on a
little-endian host; it is not yet exercised on real big-endian hardware in CI.

## Example

```bash
dsdlc --target-language obj path/to/ns \
  --obj-abi-language cpp \
  --target-endianness little \
  --obj-archive-name my_dsdl \
  --jobs 12 \
  --outdir out/obj
```
