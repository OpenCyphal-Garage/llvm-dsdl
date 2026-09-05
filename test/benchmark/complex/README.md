# Complex DSDL Benchmark Corpus

This directory contains a synthetic, large-scale DSDL corpus intended to stress
`llvm-dsdl` with a realistic civilian autonomous aerial-survey system model.

The corpus is organized under the root namespace `civildrone` and includes:

- many subsystem namespaces (airframe, navigation, perception, survey, etc.)
- message, union, and service types per subsystem
- deep cross-namespace references
- heavy reuse of `uavcan` regulated data types
- synthetic chained composite/variant workload families to amplify graph complexity
- deep vision stack coverage:
  - low-level camera/gimbal/optics/ISP control channels and services
  - video transport/codec/packetizer/depacketizer/streaming control flows
  - CV/ML pipelines (features, detections, classifications, tensors, graph state)
  - VSLAM/localization/mapping/fusion/planner/scenegraph style type graphs

The size is intentionally large (thousands of `.dsdl` files) to surface
combinatorial behaviour in discovery, parsing, semantic analysis, and downstream
lowering/codegen passes.

## Where it comes from

`generate_complex_dsdl.py` writes it, and the build runs that: the
`benchmark-corpus` target produces the corpus under
`<build-dir>/test/benchmark/complex/civildrone`, every benchmark target depends
on it, and every benchmark test requires it through a ctest fixture. Building or
running a benchmark is enough; there is no step to remember.

The output is a pure function of the generator and its three counts, so a timing
compared across revisions is measuring the compiler rather than the corpus.

To build it alone:

```bash
cmake --build --preset build-dev-homebrew-debug --target benchmark-corpus
```

Scale the synthetic workload with `--classic-workload-count`,
`--vision-workload-count` and `--megabundle-count`; `--help` gives the defaults.

Benchmark runs take the corpus as a root namespace, with
`--lookup-dir submodules/public_regulated_data_types/uavcan` for the regulated
types it references.

## Suggested sanity check

```bash
cmake --build --preset build-dev-homebrew-debug --target dsdlc benchmark-corpus
build/matrix/dev-homebrew/tools/dsdlc/Debug/dsdlc --target-language ast \
  build/matrix/dev-homebrew/test/benchmark/complex/civildrone \
  --lookup-dir submodules/public_regulated_data_types/uavcan >/dev/null
```
