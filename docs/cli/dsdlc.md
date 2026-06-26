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

## Object backend options

- `--target-endianness <little|big>` (required for `obj`)
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
