# Contributing

For complete developer workflow details, start with:

- [CONTRIBUTING.md](https://github.com/thirtytwobits/llvm-dsdl/blob/main/CONTRIBUTING.md)

## Fast path

```bash
git clone https://github.com/thirtytwobits/llvm-dsdl.git
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
