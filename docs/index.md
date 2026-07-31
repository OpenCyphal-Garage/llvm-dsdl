# llvm-dsdl User Manual

Compiler tooling for OpenCyphal DSDL, built on MLIR and LLVM.

`llvm-dsdl` ships three user-facing tools:

- `dsdlc` for compile/codegen workflows
- `dsdl-opt` for dialect/pass pipeline work
- `dsdld` for language-server workflows

## Where to go

| Section | What it covers | Pages |
|---|---|---|
| Start here | Install quickly and generate your first artifacts | [Install](getting-started/install.md) · [Quick Start](getting-started/quickstart.md) |
| CLI reference | Command surfaces and backend-specific options | [dsdlc](cli/dsdlc.md) · [dsdl-opt](cli/dsdl-opt.md) · [dsdld](cli/dsdld.md) |
| Backends | Language targets and object-code output | [Overview](backends/overview.md) · [Object Backend](backends/object.md) |
| Showroom | Real definitions with their wire layout and generated code | [Overview](showroom/index.md) |
| Design | Architecture and contract boundaries | [Architecture](design/architecture.md) · [Dialect and Contracts](design/dialect-contracts.md) |
| Validation | Parity and malformed-input behavior | [Parity Matrix](PARITY_MATRIX.md) · [Malformed Input Matrix](MALFORMED_INPUT_CONTRACT_MATRIX.md) |
| Development | Build, test, and contribute | [Contributing](development/contributing.md) · [Testing and CI](development/testing.md) |

## Source of truth

- Project README: [README.md](https://github.com/thirtytwobits/llvm-dsdl/blob/main/README.md)
- Design details: [DESIGN.md](https://github.com/thirtytwobits/llvm-dsdl/blob/main/DESIGN.md)
- Contributor workflow: [CONTRIBUTING.md](https://github.com/thirtytwobits/llvm-dsdl/blob/main/CONTRIBUTING.md)
