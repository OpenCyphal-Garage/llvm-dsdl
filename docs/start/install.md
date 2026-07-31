# Install

Two ways to get `dsdlc`, `dsdl-opt`, and `dsdld` onto a machine: download a release build, or build
from source. Take the release binaries unless you intend to work on the compiler itself — they carry
the LLVM runtime they need, so nothing has to be installed first.

## Option 1: Use release binaries

Download binaries from:

- <https://github.com/OpenCyphal-Garage/llvm-dsdl/releases>

Put `dsdlc`/`dsdl-opt`/`dsdld` on your `PATH`.

## Option 2: Build from source

```bash
git clone https://github.com/OpenCyphal-Garage/llvm-dsdl.git
cd llvm-dsdl
git submodule update --init --recursive
cmake --workflow --preset install-bin-release-ci
```

Installed binaries are typically under:

- `build/matrix/ci/install/bin`

## Prerequisites

- CMake 3.25+
- Ninja
- LLVM + MLIR CMake packages (`LLVMConfig.cmake`, `MLIRConfig.cmake`)
- C++20-capable toolchain

See the full developer prerequisites in [Contributing](../development/contributing.md).
