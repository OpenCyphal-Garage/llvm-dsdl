<div class="hero" markdown>

# llvm-dsdl User Manual

Compiler tooling for OpenCyphal DSDL, built on MLIR and LLVM.

</div>

`llvm-dsdl` ships three user-facing tools:

- `dsdlc` for compile/codegen workflows
- `dsdl-opt` for dialect/pass pipeline work
- `dsdld` for language-server workflows

<div class="grid cards" markdown>

- :material-rocket-launch: __Start Here__

  ---

  Install quickly and generate your first artifacts.

  [Install](getting-started/install.md) · [Quick Start](getting-started/quickstart.md)

- :material-console-line: __CLI Reference__

  ---

  Learn command surfaces and backend-specific options.

  [dsdlc](cli/dsdlc.md) · [dsdl-opt](cli/dsdl-opt.md) · [dsdld](cli/dsdld.md)

- :material-cube-outline: __Backends__

  ---

  Understand language targets and object-code output.

  [Overview](backends/overview.md) · [Object Backend](backends/object.md)

- :material-source-branch: __Design__

  ---

  Read architecture and contract boundaries.

  [Architecture](design/architecture.md) · [Dialect and Contracts](design/dialect-contracts.md)

- :material-shield-check: __Validation__

  ---

  Track parity and malformed-input behavior.

  [Parity Matrix](PARITY_MATRIX.md) · [Malformed Input Matrix](MALFORMED_INPUT_CONTRACT_MATRIX.md)

- :material-tools: __Development__

  ---

  Build, test, and contribute confidently.

  [Contributing](development/contributing.md) · [Testing and CI](development/testing.md)

</div>

## Source of truth

- Project README: [README.md](https://github.com/thirtytwobits/llvm-dsdl/blob/main/README.md)
- Design details: [DESIGN.md](https://github.com/thirtytwobits/llvm-dsdl/blob/main/DESIGN.md)
- Contributor workflow: [CONTRIBUTING.md](https://github.com/thirtytwobits/llvm-dsdl/blob/main/CONTRIBUTING.md)
