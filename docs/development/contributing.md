# Contributing

How to build the compiler, run its gates, and get a change accepted. This page is the short form;
`CONTRIBUTING.md` in the repository is the long one and takes precedence where they differ.

For complete developer workflow details, start with:

- [CONTRIBUTING.md](https://github.com/OpenCyphal-Garage/llvm-dsdl/blob/main/CONTRIBUTING.md)

## Fast path

```bash
git clone https://github.com/OpenCyphal-Garage/llvm-dsdl.git
cd llvm-dsdl
git submodule update --init --recursive
cmake --preset ci
cmake --build --preset build-ci
ctest --preset test-ci-smoke
```

## Quality checks

- `check-format`
- `check-clang-tidy`
- `check-iwyu`
- integration and release-blocking presets

Keep behavioral parity and matrix CI green before merge.
