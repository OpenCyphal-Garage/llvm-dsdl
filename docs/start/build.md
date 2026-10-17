# Build

The tree builds three static libraries, `llvmdsdlSupport`, `llvmdsdlFrontend` and
`llvmdsdlSemantics`, and their unit tests. There is no release to download on this branch.

## Prerequisites

- CMake 3.25 or newer and Ninja
- LLVM and MLIR CMake packages at the locked major, 22 (`LLVMConfig.cmake`, `MLIRConfig.cmake`)
- A C++23 compiler
- Python 3, for the tier 0 checks and the documentation index

## Configure, build, test

```bash
git clone https://github.com/OpenCyphal-Garage/llvm-dsdl.git
cd llvm-dsdl
git submodule update --init --recursive
cmake --preset ci
cmake --build --preset build-ci
ctest --preset test-ci-t0
```

`cmake --preset dev-homebrew` configures against Homebrew's LLVM on macOS; `dev-llvm-env` reads
`LLVM_DIR` and `MLIR_DIR` from the environment. Every preset uses the Ninja Multi-Config generator;
the test presets run the Debug configuration.

[Contributing](../development/contributing.md) covers the format and tidy checks, the install
component and the documentation site.
