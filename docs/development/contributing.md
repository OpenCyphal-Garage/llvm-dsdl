# Contributing

How to build the tree, run its checks, and get a change accepted. This page is the short form;
`CONTRIBUTING.md` in the repository is the long one and takes precedence where they differ.

- [CONTRIBUTING.md](https://github.com/OpenCyphal-Garage/llvm-dsdl/blob/main/CONTRIBUTING.md)
- [AGENTS.md](https://github.com/OpenCyphal-Garage/llvm-dsdl/blob/main/AGENTS.md), which governs
  every line of prose in the repository and holds the rule that generated output takes its shape
  from approved goldens

## Fast path

```bash
git clone https://github.com/OpenCyphal-Garage/llvm-dsdl.git
cd llvm-dsdl
git submodule update --init --recursive
cmake --preset ci
cmake --build --preset build-ci
ctest --preset test-ci-t0
```

## Checks

- `check-format` and `check-clang-tidy`, the targets CI's lint job runs
- `ctest --preset test-ci-t0`, tier 0
- `mkdocs build --strict`, after `docs-generate`, for a change under `docs/`
