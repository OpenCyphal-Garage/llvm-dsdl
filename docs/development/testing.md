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

## Comparing the generated C against Nunavut

`llvmdsdl-serdes-instruction-comparison` counts what the generated serialisers execute to encode
and decode each of the differential parity lane's ten types, for three implementations, and reports
the ratios:

- `nnvg` — the pinned peer's C, compiled like ours.
- `c` — this project's C, compiled by `CMAKE_C_COMPILER` at `-O2 -DNDEBUG`.
- `obj` — the object dsdlc emits itself, through its own LLVM pipeline at O2.

`c` and `obj` are the same plans down two lowering routes, so the gap between those two is the
routes and the gap from `nnvg` is the plans. The C compiler differs from the LLVM dsdlc is linked
against, which is part of the `obj`/`c` difference; pointing `C_COMPILER` at clang narrows it to the
routes.

The `-optimized` sibling measures dsdlc's `--optimize-lowered-serdes` output the same way. Both
carry the `bench` label, which `test-ci-full` excludes:

```bash
ctest --preset test-ci-bench -R llvmdsdl-serdes-instruction-comparison
```

Each measurement runs twice under cachegrind, at zero iterations and at the configured count; the
difference over the trip count is the per-iteration cost, and everything fixed cancels. The figures
are exact rather than approximate: a run at 50 iterations and a run at 1000 agreed to the thousandth
of an instruction on all twenty measurements. `LLVMDSDL_SERDES_INSTRUCTION_ITERATIONS` sets the
count. `LLVMDSDL_SERDES_INSTRUCTION_OPTIMISATION_FLAGS` sets the flags the two C columns are built
with. They reach the C compiler only — the object column is dsdlc's own pipeline and does not move
with them. Two settings answer further questions: dropping `-DNDEBUG` measures what the runtime's
assert guards cost (62 instructions on Heartbeat encode, and they fold away on the peer's inlined
bodies where they cannot on ours), and adding `-flto` measures what our C costs with the optimiser
able to see into it as it can into the peer's headers.

The result is reported, not gated. Nunavut is a peer used for corroboration rather than an oracle,
and that holds for speed as much as for bytes. The report carries the raw counts, which is what a
threshold on this project's own side would be built from.

The lane skips where valgrind is absent, which includes macOS arm64. In CI it runs in the
`benchmarks` job, which renders both reports into the job summary and uploads them.

Reading a report:

- The instruction columns are per operation, with the harness's own loop subtracted.
- `fixture` carries a digest of the measured payload. A figure that moves between runs is either
  the code or the payload; the digest says which, and every implementation in a run has to report
  the same one or the lane fails.
- Our two implementations define the same entry points, so one binary cannot hold both. The lane
  builds a driver per implementation and measures the peer and the empty loop in both; a
  disagreement there fails the lane, because it would mean the two drivers are not measuring the
  same thing.
- `fixture` says where the measured payload came from. `random` is a wire image drawn at random and
  accepted by both implementations. `zero` is the image a zeroed object encodes to, which is what a
  type carrying delimiter headers gets: a header has to agree with the length that follows it, and
  random bytes almost never satisfy that. On a variable-length type a `zero` fixture is a small
  payload, so the ratio beside it is a ratio at a small payload.
- Every implementation is called through a function pointer, so the ratios compare bodies. This
  project emits its serialisers out of line, into `.c` files and into objects, and Nunavut emits
  `static inline` definitions into its headers, so the peer's body is visible to the optimiser
  inside the harness and this project's is not — which is also what happens at a user's call site.

## Docs site CI

A dedicated GitHub Pages workflow builds this manual with MkDocs on every push to `main`, and
deploys it only when a release is published. The served site therefore tracks the last release
rather than the tip of `main`, so its instructions match a version readers can actually download.
Maintainers can publish a patch between releases with a manual dispatch.

See `.github/workflows/docs.yml`, and CONTRIBUTING.md section 11.7 for the publishing procedure.
