# `dsdl-opt`

`dsdl-opt` is the pass-driver for the `dsdl` MLIR dialect.

Use it to inspect and transform lowered DSDL IR through specific pass pipelines.

## Typical use

```bash
dsdlc --target-language mlir path/to/ns > input.mlir
dsdl-opt input.mlir -pass-pipeline='builtin.module(lower-dsdl-exec,dsdl-annotate-aliasability,dsdl-legalize-endianness)'
```

## Why use it

- Debug pass behavior in isolation
- Validate contract attributes across pipeline boundaries
- Build reproducible IR test cases for lit/unit tests

## Related

- [Dialect and Contracts](../design/dialect-contracts.md)
- [Architecture](../design/architecture.md)
