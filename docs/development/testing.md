# Testing and CI

Tests carry a tier label. Tier 0 runs on every build and every pull request; later tiers join with
the code they test.

| Tier | Label | Holds |
|---|---|---|
| 0 | `t0` | the unit tests, the BitLengthSet exactness suite, the lexer and parser fuzzers, the source checks that need no tool |

## Common commands

```bash
ctest --preset test-ci-t0
ctest --preset test-ci-full
```

`test-ci-full` runs every tier. The `dev-homebrew` and `dev-llvm-env` presets have the same pair.
Every test preset runs the Debug configuration with every core.

## CI

`.github/workflows/ci.yml` runs, per pull request: repository hygiene, the golden-label check,
lint, tier 0 on Linux in the toolshed image and on macOS with Homebrew LLVM, the strict docs
build, and the Dafny models under `spec/dafny`. `coverage.yml` measures LLVM source coverage of
the libraries over the same tests.

A test that skips counts as a failure in CI: `tools/check_test_skips.py` reads the JUnit report
ctest writes and fails the job on any test that did not run.

## Docs site CI

`docs.yml` builds the manual on every push to `main` and deploys it only when a release is
published, so the served site tracks the last release rather than the tip of `main`. Maintainers
can publish between releases with a manual dispatch.
