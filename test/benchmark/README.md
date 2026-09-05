# Codegen Benchmark Suite

This directory contains benchmark workloads and tooling intended to track
multi-language code generation throughput.

## Workload

- `complex/` contains a large synthetic civilian autonomous drone DSDL corpus.

## Harness

- `benchmark_codegen.py` benchmarks `dsdlc` generation for:
  - `c`
  - `cpp` (`--cpp-profile both`)
  - `rust` (`--rust-profile std`)
  - `go`
  - `ts`
  - `python`
  - optional C A/B experiment lanes:
    - `embedC`: embedded `uavcan` MLIR catalog enabled (no lookup dirs passed to `dsdlc`)
    - `noEmbedC`: embedded `uavcan` MLIR catalog disabled (`--no-embedded-uavcan`)
- `benchmark_lsp.py` benchmarks `dsdld` request latency for:
  - mixed request replay (`replay`)
  - workspace index cold/warm runs (`index-bench`)

## Thresholds

- `complex_codegen_thresholds.json` stores baseline timings and threshold profiles:
  - `dev_ab` for strict same-host A/B comparisons.
  - `ci_oom` for broad CI order-of-magnitude regression detection.

## Typical workflow

1. Record benchmark timings.
2. Generate/update threshold config with desired margins.
3. Run checks against `dev_ab` and/or `ci_oom`.

The committed threshold file starts as a template with zero baselines.
You must calibrate it before `check` mode can run.

CMake utility targets are available:

- `benchmark-codegen-record`
- `benchmark-codegen-init-thresholds`
- `benchmark-codegen-check-dev-ab`
- `benchmark-codegen-check-ci-oom`
- `benchmark-codegen-embedc-compare`
- `benchmark-lsp-replay`
- `benchmark-lsp-index-cold-warm`

When `-DLLVMDSDL_ENABLE_BENCHMARK_TESTS=ON` is configured and thresholds are
calibrated, per-language CTest lanes are also available:

- `llvmdsdl-codegen-benchmark-ci-oom-c`
- `llvmdsdl-codegen-benchmark-ci-oom-cpp`
- `llvmdsdl-codegen-benchmark-ci-oom-rust`
- `llvmdsdl-codegen-benchmark-ci-oom-go`
- `llvmdsdl-codegen-benchmark-ci-oom-ts`
- `llvmdsdl-codegen-benchmark-ci-oom-python`

### Live status during long runs

The harness prints heartbeat lines while each language command is running, e.g.:

- elapsed time
- generated-file count so far

CMake knob:

- `LLVMDSDL_CODEGEN_BENCHMARK_STATUS_INTERVAL_SEC` (default `15`, set `0` to disable)
- `LLVMDSDL_CODEGEN_BENCHMARK_MAX_RSS_MIB` (default `0`, set non-zero to fail on peak RSS cap)

Example calibration flow:

```bash
cmake --build <build-dir> --config RelWithDebInfo --target benchmark-codegen-record
cmake --build <build-dir> --config RelWithDebInfo --target benchmark-codegen-init-thresholds
```

Then copy the generated thresholds file from:

`<build-dir>/test/benchmark/complex-codegen/complex_codegen_thresholds.generated.json`

to:

`test/benchmark/complex_codegen_thresholds.json`

### Run benchmark for a single language

Direct harness usage (example: Python only):

```bash
python3 test/benchmark/benchmark_codegen.py record \
  --dsdlc build/matrix/dev-homebrew/tools/dsdlc/RelWithDebInfo/dsdlc \
  --root-namespace-dir build/matrix/dev-homebrew/test/benchmark/complex/civildrone \
  --lookup-dir submodules/public_regulated_data_types/uavcan \
  --out-base-dir build/matrix/dev-homebrew/test/benchmark/complex-codegen/generated \
  --report-json build/matrix/dev-homebrew/test/benchmark/complex-codegen/python-only-record.json \
  --languages python \
  --status-interval-sec 10
```

Use a namespace root for `--root-namespace-dir` (`.../civildrone`) and provide
`uavcan` as a lookup root. Do not pass the wrapper directory
the generated corpus as a root namespace.

CMake-driven single-language run:

```bash
cmake -S . -B build/matrix/dev-homebrew \
  -DLLVMDSDL_CODEGEN_BENCHMARK_LANGUAGES=python \
  -DLLVMDSDL_CODEGEN_BENCHMARK_STATUS_INTERVAL_SEC=10
cmake --build build/matrix/dev-homebrew --config RelWithDebInfo --target benchmark-codegen-record -j1
```

### Compare `embedC` vs `noEmbedC`

Run a side-by-side C-only benchmark on the complex dataset:

```bash
python3 test/benchmark/benchmark_codegen.py record \
  --dsdlc build/matrix/dev-homebrew/tools/dsdlc/RelWithDebInfo/dsdlc \
  --root-namespace-dir build/matrix/dev-homebrew/test/benchmark/complex/civildrone \
  --lookup-dir submodules/public_regulated_data_types/uavcan \
  --out-base-dir build/matrix/dev-homebrew/test/benchmark/complex-codegen/embedc-compare-generated \
  --report-json build/matrix/dev-homebrew/test/benchmark/complex-codegen/embedc-compare-report.json \
  --languages embedC,noEmbedC \
  --iterations 3
```

Or use the CMake convenience target:

```bash
cmake --build build/matrix/dev-homebrew --config RelWithDebInfo --target benchmark-codegen-embedc-compare -j1
```

## LSP Benchmarks

Run the mixed replay benchmark:

```bash
cmake --build <build-dir> --config RelWithDebInfo --target benchmark-lsp-replay
```

Run the cold/warm index benchmark:

```bash
cmake --build <build-dir> --config RelWithDebInfo --target benchmark-lsp-index-cold-warm
```

Reports are written under:

- `<build-dir>/test/benchmark/lsp-benchmark/replay-report.json`
- `<build-dir>/test/benchmark/lsp-benchmark/index-cold-warm-report.json`

Both reports include p50/p95/p99 latency metrics suitable for CI artifact
tracking and host A/B comparisons.
