# Contributing to llvm-dsdl

This is a developer-facing guide for building, testing, and contributing to
`llvm-dsdl`.

## 1. What This Document Covers

Use this guide to:

- set up a local development environment
- run project workflows and tests
- use the two install modes (`bin` and `dev`)
- ship changes with the expected validation and review quality

## 2. Repository Layout (Developer View)

- [`include/llvmdsdl/`](./include/llvmdsdl/): public headers for frontend, IR, semantics, transforms,
  codegen, and LSP
- [`lib/`](./lib/): implementation libraries
- [`tools/`](./tools/): user-facing binaries
  - `dsdlc`
  - `dsdl-opt`
  - `dsdld`
- [`test/`](./test/): unit, lit, integration, and benchmark suites
- [`runtime/`](./runtime/): language runtime scaffolds used by generators
- [`cmake/`](./cmake/): utility scripts used by custom targets and tests
- [`CMakePresets.json`](./CMakePresets.json): canonical configure/build/test/workflow automation

## 3. Prerequisites

Required:

- CMake `>= 3.25` (required for workflow presets)
- Ninja
- C++20-capable compiler toolchain
- LLVM + MLIR CMake packages (`LLVMConfig.cmake`, `MLIRConfig.cmake`)
- Git

Common optional tools (enable more checks/lanes):

- Python 3
- `llvm-lit` or `lit` Python module. LLVM tester
- `clang-format`
- `clang-tidy`
- `include-what-you-use`
- `cargo`/`rustc`, `go`, Node/TypeScript (`tsc`) for language-specific
  integration lanes

## 4. Clone and Initialize

```bash
git clone <repo-url> llvm-dsdl
cd llvm-dsdl
git submodule update --init --recursive
```

[`submodules/public_regulated_data_types`](./submodules/public_regulated_data_types) is needed for full integration and
`uavcan` generation paths.

## 5. Preset-First Development Workflow

List all presets:

```bash
cmake --list-presets=all
```

### 5.1 Configure

Use one configure preset:

```bash
cmake --preset dev-homebrew
# or
cmake --preset dev-llvm-env
# or
cmake --preset ci
```

If using `dev-llvm-env`, set:

```bash
export LLVM_DIR=/path/to/llvm/lib/cmake/llvm
export MLIR_DIR=/path/to/llvm/lib/cmake/mlir
```

### 5.2 Full Matrix Workflows

```bash
cmake --workflow --preset matrix-dev-homebrew
cmake --workflow --preset matrix-dev-llvm-env
cmake --workflow --preset matrix-ci
```

These run configured build/test sequences using current preset policy.

## 6. Install Modes

`llvm-dsdl` supports two install components:

- `bin`: install tools only (`dsdlc`, `dsdl-opt`, `dsdld`)
- `dev`: install development artifacts (libraries + headers)

The CMake custom targets are:

- `install-bin`
- `install-dev`

### 6.1 Manual Install Invocations

From an already-configured build tree:

```bash
cmake --build build/matrix/ci --config Release --target install-bin
cmake --build build/matrix/ci --config Debug --target install-dev
```

Default install prefix for each configure preset is under its matrix build dir,
for example:

- `build/matrix/ci/install`
- `build/matrix/dev-homebrew/install`
- `build/matrix/dev-llvm-env/install`

To change the install prefix, re-run configure with an explicit
`CMAKE_INSTALL_PREFIX`:

```bash
cmake --preset ci -DCMAKE_INSTALL_PREFIX=$PWD/out/install-custom
cmake --build build/matrix/ci --config Release --target install-bin
cmake --build build/matrix/ci --config Debug --target install-dev
```

## 7. Build and Test Commands

### 7.1 Build presets

Each build preset builds the default configuration (`Debug`).  Pass `--config <cfg>` to
override:

```bash
cmake --build --preset build-dev-homebrew
cmake --build --preset build-dev-homebrew --config RelWithDebInfo
cmake --build --preset build-dev-homebrew --config Release
```

### 7.2 Test presets

Smoke tests (exclude integration-labeled tests):

```bash
ctest --preset test-dev-homebrew-smoke
ctest --preset test-ci-smoke
```

Full suite (preset-defined full lane):

```bash
ctest --preset test-dev-homebrew-full
ctest --preset test-ci-full
```

Release-blocking architecture gates:

```bash
ctest --preset test-dev-homebrew-release-blocking
ctest --preset test-ci-release-blocking
```

These lanes include runtime benchmark threshold gates by default.
They also include execution-engine boundary validation (shared emitter orchestration guards).
They also include hard-cut integrity validation (no-shim canonical-path guard).
Threshold policy is 5% regression budget vs in-repo baselines in:
- [`test/integration/python_runtime_bench_thresholds.json`](./test/integration/python_runtime_bench_thresholds.json)
- [`test/integration/rust_runtime_bench_thresholds.json`](./test/integration/rust_runtime_bench_thresholds.json)
If an intentional performance shift is accepted, update these files in the same PR.

Convergence/runtime validator self-tests:

```bash
ctest --preset test-dev-homebrew-full -R 'llvmdsdl-(convergence-scorecard-selftest|convergence-matrix-reports-selftest|runtime-semantic-wrapper-allowlist-selftest|execution-engine-boundary-guard-selftest|hard-cut-integrity-guard-selftest)'
```

Convergence scorecards include helper-binding completeness classification
alongside verifier-first, contract-v2, and fallback-free dimensions.

CI accelerator-required lane:

```bash
ctest --preset test-ci-python-accel-required
```

### 7.3 Direct targeted test runs

```bash
ctest --test-dir build/matrix/dev-homebrew -C RelWithDebInfo --output-on-failure -R llvmdsdl-unit-tests
ctest --test-dir build/matrix/dev-homebrew -C RelWithDebInfo --output-on-failure -R llvmdsdl-lit
```

## 8. Generation and Tooling Targets

Generate all `uavcan` backends:

```bash
cmake --build --preset build-dev-homebrew --config RelWithDebInfo --target generate-uavcan-all
```

Common quality targets:

```bash
cmake --build --preset build-dev-homebrew --config RelWithDebInfo --target check-format
cmake --build --preset build-dev-homebrew --config RelWithDebInfo --target check-iwyu
cmake --build --preset build-dev-homebrew --config RelWithDebInfo --target check-clang-tidy
```

Formatting rewrite:

```bash
cmake --build --preset build-dev-homebrew --config RelWithDebInfo --target format-source
```

Convergence/parity/contract report targets:

```bash
cmake --build --preset build-dev-homebrew --config RelWithDebInfo --target convergence-report
cmake --build --preset build-dev-homebrew --config RelWithDebInfo --target parity-matrix-report
cmake --build --preset build-dev-homebrew --config RelWithDebInfo --target malformed-contract-report
cmake --build --preset build-dev-homebrew --config RelWithDebInfo --target determinism-matrix-report
cmake --build --preset build-dev-homebrew --config RelWithDebInfo --target release-blocking-report-gates
```

## 9. Optional Coverage Workflow

Enable coverage at configure time:

```bash
cmake -S . -B build/coverage -G "Ninja Multi-Config" \
  -DLLVM_DIR=/path/to/llvm/lib/cmake/llvm \
  -DMLIR_DIR=/path/to/llvm/lib/cmake/mlir \
  -DLLVMDSDL_ENABLE_LLVM_COVERAGE=ON
```

Run coverage pipeline:

```bash
cmake --build build/coverage --config RelWithDebInfo --target coverage-report -j1
```

## 10. Development Expectations

### 10.1 Keep behavior centralized

When touching backend code generation semantics, prefer shared planning and
helper layers in [`lib/CodeGen`](./lib/CodeGen) and avoid re-introducing backend-local duplicate
logic for core serdes semantics.
When touching runtime behavior above primitive bit/number operations, keep
exceptions explicit in [`runtime/semantic_wrapper_allowlist.json`](./runtime/semantic_wrapper_allowlist.json)
with owner and rationale.
Generate runtime semantic wrappers from templates before commit:
`python3 tools/runtime/generate_runtime_semantic_wrappers.py --repo-root .`
Validate allowlist integrity with the release-blocking integration lane
`llvmdsdl-runtime-semantic-wrapper-allowlist` (includes semantic-wrapper generation drift checks).
Use `llvmdsdl-runtime-semantic-wrapper-allowlist-selftest` to regression-test
validator and generation-check mutation coverage.

### 10.2 Add tests with behavior changes

For any semantic/codegen/runtime behavior change, include:

- at least one focused unit test and/or integration test
- updates to affected golden expectations if applicable

### 10.3 Keep docs in sync

If you change CLI behavior, targets, workflows, or runtime contracts, update:

- [`README.md`](./README.md)
- [`CONTRIBUTING.md`](./CONTRIBUTING.md)
- relevant docs under [`docs/`](./docs/)
- tool-specific docs (for example [`tools/dsdld/README.md`](./tools/dsdld/README.md))

## 11. Pull Request Checklist

Before opening a PR:

1. Rebase on latest target branch.
2. Run at least one full preset lane relevant to your change.
3. Run focused tests for touched areas.
4. Run formatting/lint checks when applicable.
5. Summarize exactly what was validated in the PR description.

In the PR description, include:

- configure/build/test commands used
- which preset/workflow was run
- any non-default options toggled
- risk areas and follow-up work (if any)

## 12. Troubleshooting Quick Notes

### CMake cannot find LLVM/MLIR

- verify `LLVM_DIR` and `MLIR_DIR`
- use `dev-homebrew` preset on macOS/Homebrew
- rerun configure after changing environment variables

### lit tests are not present

- install `llvm-lit` or Python `lit`
- rerun configure

### workflow preset missing

- ensure CMake version is `>= 3.25`
- verify with `cmake --list-presets=all`

### integration lanes fail due external toolchains

- verify Python/Rust/Go/Node toolchains installed for impacted lanes
- use smoke presets first to validate core build health

## 13. Useful References

- [`README.md`](./README.md)
- [`DESIGN.md`](./DESIGN.md)
- [`tools/dsdld/README.md`](./tools/dsdld/README.md)
