# llvm-dsdl

![llvm-dsdl logo](./llvm-dsdl.png)

[![CI](https://img.shields.io/github/actions/workflow/status/OpenCyphal-Garage/llvm-dsdl/ci.yml?branch=main&label=CI)](https://github.com/OpenCyphal-Garage/llvm-dsdl/actions/workflows/ci.yml)
[![Docs](https://img.shields.io/badge/docs-user%20manual-blue)](https://opencyphal-garage.github.io/llvm-dsdl/)

`llvm-dsdl` compiles [Cyphal DSDL](https://github.com/OpenCyphal/specification/tree/master/specification/dsdl)
into serialisation code, through an MLIR dialect that admits other interface languages later.

The compiler is being rebuilt on the `reset/hourglass` branch. In the tree: the DSDL frontend
(discovery, lexer, parser), the semantic analysis (constant evaluation, bit-length sets, layout),
their unit tests, the Dafny models of the serialisation under `spec/dafny`, and the build, CI,
packaging and documentation infrastructure. The dialects, the code generators and the tools
follow, each to a golden its own ecosystem defines. [RESET.md](./RESET.md) is the plan;
[DESIGN.md](./DESIGN.md) is the architecture; [AGENTS.md](./AGENTS.md) governs every line of
prose and holds the rule that generated output takes its shape from approved goldens.

## Build

```bash
git clone https://github.com/OpenCyphal-Garage/llvm-dsdl.git
cd llvm-dsdl
git submodule update --init --recursive
cmake --preset ci
cmake --build --preset build-ci
ctest --preset test-ci-t0
```

The `ci` preset finds LLVM and MLIR 22 through `LLVM_DIR` and `MLIR_DIR`; `dev-homebrew` uses
Homebrew's on macOS. [CONTRIBUTING.md](./CONTRIBUTING.md) has the rest.
