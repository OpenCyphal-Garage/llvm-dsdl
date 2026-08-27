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

## Inspecting what a parity lane built

The C/Go parity lanes generate into a timestamped scratch tree and delete it when they pass, so on a
green run there is nothing left to look at. Set `LLVMDSDL_KEEP_RUN_OUTPUT` to keep it:

```bash
LLVMDSDL_KEEP_RUN_OUTPUT=1 ctest --test-dir build/matrix/ci -C RelWithDebInfo -R llvmdsdl-uavcan-c-go-parity
```

The lane prints where the tree is and what is in each part of it:

```
run-<stamp>-<nonce>/c        generated C
run-<stamp>-<nonce>/go       generated Go
run-<stamp>-<nonce>/harness  the harness sources, after token substitution
run-<stamp>-<nonce>/build    compiled artefacts
```

`harness/` is usually the interesting one. Those sources are checked in with `@V1_0@`-style tokens
that resolve to a version suffix or to nothing depending on the naming scheme under test — see
`cmake/HarnessTypeNameTokens.cmake` — so what a harness was *actually* compiled against is not
something you can read off the source tree. It also makes an unsubstituted token visible:

```bash
grep -rE '@C?V[0-9]+_[0-9]+@' <run dir>    # any hit is a token the scheme did not define
```

Applies to `llvmdsdl-uavcan-c-go-parity` and `llvmdsdl-signed-narrow-c-go-parity`, and to their
`-optimized` variants. `-DKEEP_RUN_OUTPUT=ON` does the same when invoking the script directly with
`cmake -P`.

A failing lane already keeps its tree — that is how you debug one. Each run now clears previous run
directories first, so what you find is from the run you just did.

## Docs site CI

A dedicated GitHub Pages workflow builds this manual with MkDocs on every push to `main`, and
deploys it only when a release is published. The served site therefore tracks the last release
rather than the tip of `main`, so its instructions match a version readers can actually download.
Maintainers can publish a patch between releases with a manual dispatch.

See `.github/workflows/docs.yml`, and CONTRIBUTING.md section 11.7 for the publishing procedure.
