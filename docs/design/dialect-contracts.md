# Dialect and Contracts

The custom `dsdl` MLIR dialect is the intermediate contract boundary between frontend semantics and backend rendering.

## Key ideas

- Schema and serialization plans are represented as explicit IR ops
- Passes stamp/validate lowered contract metadata
- Backends consume facts from validated lowered state

## Important pass sequence

- `lower-dsdl-exec`
- `dsdl-annotate-aliasability` (conservative aliasability *annotator*; stamps metadata only)
- `dsdl-legalize-endianness` (validates/stamps target endianness; performs no byte reordering)
- optional optimization pipeline

## Contract benefits

- Detects raw/unlowered IR reaching a backend via a contract version + producer-identity guard (not field-level semantic-compatibility drift detection)
- Keeps backend emitters aligned
- Enables deterministic diagnostics for unsupported/malformed states

## Where to read details

- `include/llvmdsdl/Transforms/LoweredSerDesContract.h`
- `lib/Transforms/LoweredSerDesContractValidation.cpp`
- `lib/CodeGen/MlirLoweredFacts.cpp`
