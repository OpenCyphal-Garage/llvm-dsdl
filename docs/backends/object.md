# Object Backend (`-l obj`)

The object backend compiles generated sources into static objects and optional archives.

## Primary flags

- `--target-language obj`
- `--target-endianness <little|big>` (required)
- `--obj-abi-language <c|cpp>` (`c` default)
- `--target-triple <triple>` (optional)
- `--obj-archive-name <name>`
- `--obj-no-archive`
- `--jobs, -j <N>` compile parallelism (auto if omitted)

## ABI lanes

## C lane (`--obj-abi-language c`)

- Stages generated C under `.obj_stage_c`
- Compiles C translation units to `.o`
- Optionally archives into `.a`

## C++ lane (`--obj-abi-language cpp`)

- Uses canonical profile-agnostic C++ ABI types
- Exports C++ ABI symbols plus C-callable shim symbols
- Stages under `.obj_stage_cpp` (including nested C stage)

## Endianness semantics

Wire semantics remain OpenCyphal-compatible. Target endianness controls codegen/legalization strategy, not wire contract semantics.

## Example

```bash
dsdlc --target-language obj path/to/ns \
  --obj-abi-language cpp \
  --target-endianness little \
  --obj-archive-name my_dsdl \
  --jobs 12 \
  --outdir out/obj
```
