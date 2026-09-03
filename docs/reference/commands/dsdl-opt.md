# `dsdl-opt`

`dsdl-opt` is the pass-driver for the `dsdl` MLIR dialect.

Use it to inspect and transform lowered DSDL IR through specific pass pipelines.

## Typical use

```bash
dsdlc --target-language mlir path/to/ns > input.mlir
dsdl-opt input.mlir -pass-pipeline='builtin.module(lower-dsdl-exec,dsdl-annotate-aliasability)'
```

## Uses

- Debug pass behaviour in isolation
- Validate contract attributes across pipeline boundaries
- Build reproducible IR test cases for lit/unit tests

## Related

- [Architecture](../../development/architecture.md) — the dialect as a contract boundary, and the
  pass sequence these pipelines are built from
