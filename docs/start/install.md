# Install

Two ways to get `dsdlc`, `dsdl-opt`, and `dsdld` onto a machine: download a release build, or build
from source. Take the release binaries unless you intend to work on the compiler itself — they carry
the LLVM runtime they need, so nothing has to be installed first.

## Option 1: Use release binaries

Download binaries from:

- <https://github.com/OpenCyphal-Garage/llvm-dsdl/releases>

Put `dsdlc`/`dsdl-opt`/`dsdld` on your `PATH`.

### macOS: fetch with `curl`, not with a browser

The macOS binaries are ad-hoc signed rather than notarised, so Gatekeeper refuses to run them if
they carry `com.apple.quarantine`. A browser attaches that attribute to the downloaded archive and
`tar` copies it onto every extracted file — extracting from a shell does not avoid it, and the
first run fails with "Apple could not verify ... is free of malware". Downloading with `curl` never
attaches it:

```bash
curl -fLO <asset-url-from-the-releases-page>
tar -xzf llvm-dsdl-<version>-darwin-arm64.tar.gz
```

If you already downloaded through a browser, clear the attribute on the extracted directory:

```bash
xattr -d -r com.apple.quarantine llvm-dsdl-<version>-darwin-arm64
```

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
