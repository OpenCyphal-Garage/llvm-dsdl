# Architecture

`llvm-dsdl` compiles DSDL through a shared semantic core and a custom MLIR dialect.

```mermaid
flowchart LR
  A[".dsdl files"] --> B["Frontend\n(discovery + parser)"]
  B --> C["Semantic analysis"]
  C --> D["DSDL MLIR dialect"]
  D --> E["Executable lowering + validation passes"]
  E --> F{"Backend emitters"}
  F --> G["C/C++/Rust/Go/TS/Python"]
  F --> H["obj (.o/.a)"]
```

## Why this shape

- Single source of truth for semantics
- Strong pass and contract boundaries
- Better backend parity and testability

## The dialect is the contract boundary

The custom `dsdl` MLIR dialect is what separates frontend semantics from backend rendering. Everything
above it decides what a definition *means*; everything below it decides how that meaning is spelled in
a particular language.

- Schema and serialization plans are represented as explicit IR ops, and every fact they carry is a
  declared attribute the verifier checks
- Passes stamp and validate lowered contract metadata
- Backends consume facts from validated lowered state, never from raw IR

### Pass sequence

1. `lower-dsdl-exec`
2. `dsdl-annotate-aliasability` — conservative aliasability *annotator*; stamps metadata only
3. optional `optimize-dsdl-lowered-serdes`
4. `build-dsdl-plan-bodies` — every plan becomes a serialize and a deserialize function of plan operations
5. `convert-dsdl-to-emitc` for C; `convert-dsdl-to-llvm` and `emit-dsdl-runtime` for objects

### Boundary guarantees

- Raw or unlowered IR reaching a backend is caught by a contract version and producer-identity guard.
  That guard detects *unlowered state*, not field-level semantic-compatibility drift.
- Backend emitters stay aligned, because they read the same validated facts.
- Unsupported and malformed states produce deterministic diagnostics.

The serialize/deserialize step order the emitters render from this state is itself a contract, and a
published one: see [Emit Order](../reference/codegen/emit-order.md).

## Canonical references

- Design source: [DESIGN.md](https://github.com/OpenCyphal-Garage/llvm-dsdl/blob/main/DESIGN.md)
- Dialect definitions: `include/llvmdsdl/IR/*`
- Transform passes: `include/llvmdsdl/Transforms/*`, `lib/Transforms/*`
- Lowered contract: `include/llvmdsdl/Transforms/LoweredSerDesContract.h`,
  `lib/Transforms/LoweredSerDesContractValidation.cpp`, `lib/CodeGen/MlirLoweredFacts.cpp`
