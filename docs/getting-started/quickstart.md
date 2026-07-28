# Quick Start

## Show tool help

```bash
dsdlc --help
dsdlc --version
```

## Generate C from a namespace

```bash
dsdlc --target-language c path/to/root_namespace --outdir out/c
```

## Generate profile-agnostic C++ ABI object code

```bash
dsdlc --target-language obj path/to/root_namespace \
  --obj-abi-language cpp \
  --target-endianness little \
  --jobs 8 \
  --outdir out/obj-cpp
```

## Useful workflow flags

- `--dry-run`: plan and validate only
- `--list-inputs`: print resolved input set
- `--list-outputs`: print output set
- `-MD`: emit make-style dependency files

## See the output before you write anything

The [Showroom](../showroom/index.md) is a namespace of plausible vendor-specific drone datatypes
generated into every supported language and profile, so you can read what the compiler produces for
definitions shaped like yours before committing to them. Build the whole tree with:

```bash
cmake --build <build-dir> --target showroom
```

## Next

- CLI options: [dsdlc](../cli/dsdlc.md)
- Backend behavior: [Backend Overview](../backends/overview.md)
- Object backend details: [Object Backend](../backends/object.md)
