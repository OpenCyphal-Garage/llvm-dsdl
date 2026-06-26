# Testing and CI

`llvm-dsdl` uses layered test gates:

- unit tests
- lit tests
- integration smoke/parity tests
- release-blocking lanes

## Common commands

```bash
ctest --preset test-ci-smoke
ctest --preset test-ci-full
ctest --preset test-ci-release-blocking
```

## Targeted runs

```bash
ctest --test-dir build/matrix/ci -C RelWithDebInfo -R llvmdsdl-lit --output-on-failure
ctest --test-dir build/matrix/ci -C RelWithDebInfo -R llvmdsdl-obj-backend-smoke --output-on-failure
```

## Docs site CI

A dedicated GitHub Pages workflow builds this manual with MkDocs Material and deploys on `main`.

See `.github/workflows/docs.yml`.
