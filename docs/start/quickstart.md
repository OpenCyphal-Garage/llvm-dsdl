# Quick Start

Compile a DSDL namespace into generated code. Each block below is a complete command; run them
against a root namespace directory of your own, or against the `lanyard` namespace in the
[Showroom](../showroom/index.md) if you do not have one yet.

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

## See the output before you write anything

The [Showroom](../showroom/index.md) is a namespace of plausible vendor-specific drone datatypes
generated into every supported language and profile. Build the whole tree with:

```bash
cmake --build <build-dir> --target showroom
```

## Next

- CLI options: [dsdlc](../reference/commands/dsdlc.md)
- Backend behaviour: [Backend Overview](../reference/codegen/backends.md)
- Object backend details: [Object Backend](../reference/codegen/object.md)
