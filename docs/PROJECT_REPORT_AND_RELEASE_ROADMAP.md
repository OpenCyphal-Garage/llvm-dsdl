# llvm-dsdl — Deep Architectural Review & Production-Readiness Report Card

> _Generated 2026-06-28 against commit `c18867a`. Source references are written as
> `path:line` for the tree at review time; line numbers will drift as the code
> evolves. This document is intended to be committed and iterated on — update
> verdicts and check off roadmap items as the gaps close._

> **Update 2026-07-03 — P0 "Truthful assurance docs" + "Behavioral gates" landed.**
> Work done in this pass, plus corrections to this report where the original review was
> itself stale or overstated (verified against the current tree):
> - **CI is committed** (`​.github/workflows/ci.yml`, `coverage.yml`, `docs.yml`) and the
>   report gates already **hard-fail the build** (`message(FATAL_ERROR …)` in the
>   `cmake/Run*Report.cmake` wrappers, run by the `release-blocking-report-gates` target).
>   G8 / rec 9's "CI uncommitted" and G4/G5's "gates are theater" premises were stale.
> - **Behavioral gates:** the parity, malformed-input, and determinism scorecards now
>   consume **executed ctest pass/fail** (JUnit), not `ctest -N` test-name presence — a
>   cell is `covered` only if a matching test ran and passed (fail/skip/absent ⇒ uncovered).
>   The convergence scorecard is **relabeled** as an infrastructure-consistency lint (it is
>   inherently a marker check and cannot be made behavioral cheaply). See §5 P0.
> - **Passes renamed/redescribed:** `dsdl-prove-zero-overhead` → `dsdl-annotate-aliasability`
>   (drops "proof" language; it is a conservative annotator); `dsdl-legalize-endianness`
>   documented as validation-only (no byte reordering).
> - **Big-endian is NOT "unimplemented".** The original G5/G6/§3 "big-endian absent /
>   needs byte-swap" claim is **overstated** — see the corrected notes below. DSDL wire is
>   always little-endian, `serialize_`/`deserialize_` are host-endianness-agnostic, and the
>   `-l obj … --target-endianness big` smoke test asserts big==little byte-parity; only the
>   zero-copy *view* fast-path is (correctly) disabled on big-endian targets.

**Scope reviewed:** ~45k LOC C++ (`lib/`, `include/`, `tools/`), ~3.6k LOC multi-language runtime, ~17k LOC tests, the MLIR `dsdl` dialect, 6 codegen backends, the LSP, and the build/CI/convergence tooling. Method: docs read first (DESIGN.md, `docs/design/*`, the four matrix docs), then implementation, then **adversarial verification of every headline claim** against the actual code. Version under review: **`0.1.0`**.

## 1. Verdict

This is a **genuinely sophisticated advanced prototype** — far above the median "code generator" — built by someone who understands compilers. The MLIR investment is real, the frontend is hardened, the runtime read-path inherits battle-tested bounds-safety, and there is a real differential-testing culture.

But there is a consistent, structural problem that dominates the production-readiness assessment:

> **The assurance *narrative* substantially overstates what the implementation *proves*.** "Convergence = 100," "zero-overhead proof," "producer/consumer drift detection," "verifier-first invariant enforcement," "fallback-free," and "release-blocking malformed/parity gates" are presented as guarantees. On inspection, the three headline scorecards are **marker-presence / test-name-presence metrics**, two of the headline passes are **annotators/validators with grandiose names**, and the strongest assurance claim (differential parity against the reference compiler) covers **5 hand-picked types and does not run in CI**.

The engineering substance grades around a **B−**; the assurance *claims* grade around a **C−**. Closing that gap — partly by building the verification the docs already promise, partly by relabeling what can't be — is the central task of going from prototype to high-assurance. No agent found a `critical` defect, but the aggregate is **0 critical / 23 high / 40 medium / 23 low**, and several "high"s are real safety gaps (unbounded LSP allocation, no sanitizer/native-fuzz coverage of generated decoders, unenforced primitive bit-widths).

Claim audit tally across the review: **28 holds · 32 partial · 5 overstated · 2 false · 9 unverifiable.**

---

## 2. Efficacy Against Stated Design Goals

| # | Stated goal | Verdict | Grade |
|---|-------------|---------|------:|
| G1 | "Shared semantics, multiple syntaxes" | **Partial** — shared *planning*, per-backend *rendering* | C+ |
| G2 | LLVM/MLIR as real infrastructure | **Holds** — genuinely operational | A− |
| G3 | Contract boundaries / drift detection | **Overstated** — presence/identity guard, not drift detection | C |
| G4 | Backend parity / convergence = 100 | **Overstated → addressed (2026-07-03)** — parity/malformed/determinism now behavioral (executed pass/fail); convergence relabeled as a lint | C−→B |
| G5 | Malformed safety + determinism + release-blocking gates | **Partial → improved (2026-07-03)** — real read-path safety; gates now behavioral; big-endian present (not "absent") | C |
| G6 | Zero-overhead proof / verifier-first / fallback-free | **Overstated → partly addressed (2026-07-03)** — pass renamed `dsdl-annotate-aliasability` (drops "proof"); verifier-first/fallback-free still post-hoc/lint | C− |
| G7 | DSDL v1.0 spec conformance | **Partial** — grammar strong; primitive widths unenforced; parity narrow | C+ |
| G8 | Reproducible/deterministic builds | **Partial** — good presets; catalog & nondeterminism unguarded; CI uncommitted | B− |

### G1 — "Shared semantics, multiple syntaxes" → **Partial (shared planning, per-backend rendering)**
This is the load-bearing claim, and the truth is in the middle. A **real, substantial shared layer exists**: `lib/CodeGen/MlirLoweredFacts.cpp`, `lib/CodeGen/RuntimeLoweredPlan.cpp`, `LoweredRenderIR`, `NativeHelperContract`/`HelperSymbolResolver`, and one traversal (`lib/CodeGen/NativeEmitterTraversal.cpp`) drive field ordering, union dispatch, and helper-binding requirements uniformly. The C backend genuinely delegates to MLIR (`convert-dsdl-to-emitc` + EmitC), reimplementing nothing.

But the actual **control-flow emission is hand-written per backend.** `emitSerializeUnion` is independently coded in `lib/CodeGen/RustEmitter.cpp:559` and `lib/CodeGen/GoEmitter.cpp:585` — same intended behavior, six separate hand-written renderings. A bug like "mask-before-validate vs validate-before-mask" in one backend would not be caught by the convergence machinery. So G1 is *aspirationally true and structurally partial*: the semantics are **planned once and rendered six times**, with cross-language agreement enforced by tests rather than by construction.

### G2 — MLIR as real infrastructure → **Holds**
This is the project's strongest claim and it is **true**. There is a proper ODS dialect (`include/llvmdsdl/IR/DSDLOps.td`, `DSDLTypes.td`, `DSDLAttrs.td`), ops with **real verifiers** (`SerializationPlanOp::verify()`, `IOOp::verify()` reject malformed union/array/cast metadata), a working pass pipeline, and a genuine EmitC lowering path producing real C. MLIR is operational infrastructure here, not scaffolding. (Minor: `dsdl.field`/`dsdl.constant` ops appear to be dead — defined but unconsumed — and should be removed or documented.)

### G3 — Contract boundaries / drift detection → **Overstated**
The contract mechanism is real and enforced at the consumer (codegen aborts on a missing/unsupported contract, with negative tests). **But it is not drift detection.** Producer stamp and consumer check both read the *same* constant `kLoweredSerDesContractMajor = 2` (`include/llvmdsdl/Transforms/LoweredSerDesContract.h:28`); within a build they cannot disagree. `lib/Transforms/LoweredSerDesContractValidation.cpp:46` checks version-equality and producer-string-equality. So it detects *"the lowering pass didn't run / raw IR was fed to a backend"* — a useful guard — but the docs' "detects producer/consumer drift early" implies semantic-compatibility checking that doesn't exist (no field-level compatibility rules, no payload divergence detection).

### G4 — Convergence / parity = 100 → **Overstated (the number measures markers, not behavior)**
Verified in the source directly. `tools/convergence/convergence_report.py:180-282` computes the "14/14 shared" score by **`re.search`-ing each emitter's `.cpp` for call-string markers** like `collectLoweredFactsFromMlir(`, `renderSectionHelperBindings(`, `unionTagValidate`. A backend scores 100 by *mentioning* the shared helpers; it would still score 100 after weakening a validation step, as long as the marker strings remain. The same pattern holds for the **parity** scorecard (`tools/convergence/parity_matrix_report.py`: a cell is "covered" if a `ctest -N` *test name* matches a regex — it never runs the harness) and the **malformed** scorecard (`tools/convergence/malformed_contract_matrix_report.py:237`: same `ctest -N` name-presence).

Real behavioral testing *does* exist and is good — e.g. C↔Go generated, compiled, CGO-linked, round-tripped over 128 random + 265 directed cases with hard-fail on byte mismatch. The problem is purely that the **published "100" scores certify label presence, not the behavior the docs imply.** They should be relabeled as infrastructure-consistency lints.

### G5 — Malformed safety + determinism + release-blocking gates → **Partial**
Genuinely good: the generated C **read path is bounds-safe** — it inherits the Nunavut/libcanard `copy_bits` primitive that clamps each read window to the buffer via `saturate_fragment_bits`/`choose_min` (`runtime/dsdl_runtime.h:98+`), and variable-array decode validates the length prefix *before* the element loop (no OOB writes from inflated counts).

Weak where it matters most — ~~**(a)** there is **no ASan/UBSan/MSan anywhere**~~ and ~~**(b)** only the *Python* runtime … is fuzzed~~ **(both addressed 2026-07-03: the `ci-asan` preset + `sanitizers` CI lane run ASan/UBSan over the generated C/C++/Go decoders and a coverage-guided libFuzzer lane over the native C deserializers on the real corpus — see P0)**; **(c)** ~~the "release-blocking malformed gate" is the name-presence metric above~~ **(fixed 2026-07-03: the malformed/parity/determinism gates now consume executed ctest pass/fail — behavioral, not name-presence)**; **(d)** the release `copy_bits` path uses `assert()` guards that vanish under `NDEBUG`. For an avionics-adjacent decoder of untrusted bytes, this is the single most important gap.

### G6 — Zero-overhead proof / verifier-first / fallback-free → **Overstated**
All three were read directly:

- **`dsdl-annotate-aliasability`** (renamed 2026-07-03 from `dsdl-prove-zero-overhead`; `lib/Transforms/Passes.cpp`) now matches its honest description — "annotate plans with *conservative* aliasability facts." It stamps `zoh_alias_eligible` on fixed-size/sealed/byte-aligned layouts; that flag flows out only as a generated **boolean constant** (`ZOH_ALIAS_ELIGIBLE = true/false`) and **does not switch the serializer to a zero-copy path**. It proves nothing about emitted-code overhead. (It *is* well unit-tested as an annotator, incl. a negative case.) *(The emitted `ZOH_ALIAS_ELIGIBLE`/`zoh_alias_*` surface was deliberately left unrenamed — it is a generated-API and doesn't itself claim a proof.)*
- **"verifier-first"** is **post-hoc**: invariants are validated on already-lowered IR, not enforced during lowering (`lib/Lowering/LowerToMLIR.cpp` adds no MLIR verifiers/constraints).
- **"fallback-free"** is scored by grepping cmake gate files for regexes.

### G7 — DSDL v1.0 conformance → **Partial**
Strong: the lexer/parser went through **6 documented rounds of grammar conformance** and reserved-identifier enforcement; all six directives (`@union/@sealed/@extent/@assert/@print/@deprecated`) are parsed and semantically checked; **delimited (non-sealed) bit-length sets are modeled correctly** (32-bit delimiter header + 0..extent), matching pydsdl/Nunavut.

Real conformance bug found and verified live ~~**primitive bit-width constraints are not enforced.** `uint100` and `float8` pass parse + semantic analysis and emit structurally-valid MLIR~~ **(fixed 2026-07-10:** the frontend parser and the `dsdl.io` verifier now reject out-of-range primitive widths — int/uint [1,64], float {16,32,64}, void [1,64] — with pydsdl-style diagnostics; see P0). *(Originally: `lib/Frontend/Parser.cpp:725` only checked uint32 fit; no range check in `lib/Semantics/Analyzer.cpp` or the scalar IR verifier.)* The reference compiler rejects these with diagnostics. And the **differential parity vs Nunavut covers exactly 5 files** (`test/integration/RunDifferentialParity.cmake:99`), **disables byte comparison for the union and float cases**, and **is skipped in CI** (the workflow never provisions `nunavut`/`pydsdl`).

### G8 — Reproducible builds → **Partial**
Good preset/workflow discipline and depfile support. But: the **545 KB embedded UAVCAN MLIR catalog** (`lib/CodeGen/UavcanEmbeddedMlir.inc`) ships a declared SHA-256 that is **never validated at runtime** and is **regenerated by a standalone script CMake never invokes** — so a stale or corrupt catalog ships silently. `unordered_map`/`unordered_set` appear in 10 codegen files (iteration-order determinism risk, only partly covered by determinism tests). And **CI itself is uncommitted** (`ci.yml`, `coverage.yml`, `.github/actions/` are untracked), so the "release-blocking" lanes aren't yet enforced by the repo.

---

## 3. Subsystem Scorecard (0 = prototype, 10 = high-assurance-ready)

| Subsystem | Score | One-line assessment |
|-----------|:----:|---------------------|
| Frontend (lexer/parser/discovery) | **8** | Hardened: fuzzed, crash-fixed, grammar-conformant. Add recursion/element caps. |
| Codegen Rust/Go/TS/Python emitters | **8** | High-quality, idiomatic output; ~200–500 LOC duplicated control-flow. |
| IR dialect + lowering | **7.5** | Real ODS dialect + verifiers; dead ops; lowering lacks proactive verifiers. |
| Semantics | **7** | Solid BitLengthSet algebra; **unchecked `Rational` int64 overflow**, unbounded `repeatRange` expansion. |
| Runtime (multi-language) | **7** | Read-path bounds-safe; **empty semantic-wrapper allowlist**; no isolated cross-language primitive tests. |
| Codegen C/C++/Object | **6.5** | Clean C-via-EmitC; object backend exec is shell-safe; `targetTriple` input under-validated. |
| Transforms | **6** | Real contract enforcement; zero-overhead/endianness passes renamed/redescribed (2026-07-03); big-endian **implemented** — wire is LE, `serialize_`/`deserialize_` host-agnostic, only the zero-copy view fast-path is disabled on BE. |
| Codegen shared layer | **6** | Genuine shared planning; semantics still re-rendered per backend. |
| LSP (`dsdld`) | **6** | Reuses compiler core (good); ~~unbounded `Content-Length` allocation (OOM DoS)~~ **capped + overflow-safe (2026-07-10)**; DocumentStore thread-safety still open. |
| Tools / CLI | **6** | Good arg/exit discipline; embedded-catalog freshness & integrity ungated. |
| Build / CI / test infra | **6** | Strong test culture; **gates are name-presence metrics**; silent toolchain skips; CI uncommitted. |
| **Headline-claim integrity** | **5** | The dominant production risk: docs promise proofs the code doesn't deliver. |

---

## 4. Top Recommendations (by leverage)

**Highest leverage — make the claims true or relabel them:**

1. **Re-found the three scorecards on behavior, not markers.** ✅ **Done (2026-07-03) for parity/malformed/determinism** — they consume `ctest` **pass/fail** (JUnit) via `tools/convergence/ctest_results.py`; a cell is covered only if a matching test ran and passed. Convergence relabeled as an **infrastructure-consistency lint** (`docs/CONVERGENCE_SCORECARD.md`) rather than made behavioral (it is inherently a marker check). Deriving convergence from generated-output/AST equivalence remains a worthwhile P1 deepening.
2. ✅ **Done (2026-07-03).** Renamed `dsdl-prove-zero-overhead` → `dsdl-annotate-aliasability` and dropped "proof" language; documented `dsdl-legalize-endianness` as validation-only (no byte reordering). ~~mark `--target-endianness big` EXPERIMENTAL/unsupported until byte-swap logic exists~~ — **withdrawn as overstated:** DSDL wire is always little-endian, so there is no byte-swap to implement; `serialize_`/`deserialize_` are host-endianness-agnostic and byte-parity-tested against little-endian in the `-l obj` smoke test. Only the zero-copy *view* fast-path is disabled on BE (returns an error), which is correct, not missing.
3. **Build the verification the docs already promise:** ✅ **Nunavut differential parity now runs in CI (2026-07-10)** — provisioned + loudly required; byte comparison always-on (non-float byte-exact, float byte-exact except NaN payloads); coverage broadened 6 → 10 cases incl. a byte-exact non-float union, fixed+variable arrays, nested composites, and a narrow scalar. Remaining: `register.Value` byte-exact (gated on the float→double codegen fix, P2) + `reg`/UDRAL namespace + `port.List` (needs larger harness buffers). See the P0 entry.

**Safety / high-assurance:**

4. ✅ **Done (2026-07-03).** Added the `sanitizers` CI lane (linux/toolshed, Clang): ASan+UBSan over the generated native C/C++/Go decoders (via the parity harnesses recompiled instrumented) and a coverage-guided **libFuzzer lane over the generated C deserializers** on the real UAVCAN corpus, covering the nested-delimited / unions-of-composites / variable-array shapes. See the P0 entry below for the components. *(Remaining P1 deepening: expand the fuzzed type set beyond the curated 7 and enable byte-for-byte serialize re-check assertions inside the fuzz round-trip.)*
5. ✅ **Done (2026-07-10).** Primitive bit-length constraints are now enforced in the frontend parser *and* the `dsdl.io` IR verifier, with per-kind diagnostics. Per the Cyphal Specification these are **signed int [2,64]** (not 1 — `int1` is invalid), **unsigned int [1,64]**, **float ∈ {16,32,64}**, **void [1,64]**. See the P0 entry below. *(This corrects the earlier "int/uint 1..64" shorthand — signed and unsigned have different minimums.)*
6. **Bound the LSP:** ✅ **`Content-Length` cap done (2026-07-10)** — bounded + overflow-safe before allocation (`lib/LSP/JsonRpcIO.cpp`; see P0). Remaining: make `DocumentStore` access thread-safe (tracked under P1).
7. **Harden semantics:** checked/`__int128` `Rational` multiply; bound `BitLengthSet` expansion against adversarial `repeatRange`; cap array capacity.

**Integrity / reproducibility:**

8. **Gate the embedded catalog:** validate its SHA-256 at runtime; wire generation + `--check` freshness into CMake/CI so a stale submodule fails the build.
9. **Commit CI** and fix gate ordering (`if: always()` lets failures slip); make toolchain-missing test skips **loud** (emit a coverage manifest) so local green ≠ false confidence.
10. **Populate or justify** the empty `semantic_wrapper_allowlist.json`; add **isolated** cross-language primitive equivalence tests (sign-extend, float16 pack/unpack, copy_bits) so paired bugs can't mask each other.

---

## 5. Production-Readiness Report Card → work to reach high-assurance public release

**Current standing: an advanced prototype (overall ≈ C+/B−).** Strong bones; oversold guarantees; a handful of real safety gaps. The work below is the prototype→high-assurance path, in priority order.

### P0 — Release-blocking (must close before any "high-assurance" claim)

- [x] **Truthful assurance docs.** *(Done 2026-07-03.)* Convergence relabeled as an infrastructure-consistency lint; parity/malformed carry explicit structural-vs-behavioral mode banners; `dsdl-prove-zero-overhead` → `dsdl-annotate-aliasability` (drops "proof"); `dsdl-legalize-endianness` documented validation-only; big-endian docs corrected (`docs/backends/object.md`). Big-endian **not** marked unsupported — that premise was overstated (big-endian is implemented; see the 2026-07-03 update note).
- [x] **Behavioral gates.** *(Done 2026-07-03.)* Parity/malformed/determinism scorecards consume executed ctest pass/fail via JUnit (`tools/convergence/ctest_results.py`); a cell is `covered` only if a matching test ran and passed (fail/skip/absent ⇒ uncovered). Gates hard-fail on regression (already did) and now on behavioral coverage loss. CI feeds the suite's JUnit into `release-blocking-report-gates` via the `LLVMDSDL_REPORT_GATE_JUNIT` cache var; missing results fail loudly (no silent "no data = pass"). Red-team verified: flipping one parity test to fail breaks the gate. *(Remaining nuance: the in-suite ctest coverage tests still run structurally as a fast pre-check; the authoritative behavioral gate is the post-suite target.)*
- [x] **Sanitizers + native decoder fuzzing in CI.** *(Done 2026-07-03.)* New `ci-asan`
  preset + `sanitizers` CI job (linux/toolshed, Clang-forced): ASan/UBSan over the
  generated **C and C++** decoders via the existing parity harness recompiled
  instrumented (`RunCppCParity.cmake` gains a `SANITIZE` knob →
  `llvmdsdl-uavcan-cpp-c-parity-sanitized`), and over the **Go** harness's generated-C
  side (`RunCGoParity.cmake`, linux-only variant; Go-native decoders are memory-safe
  by construction and already panic-fail the parity harness). New **coverage-guided
  libFuzzer lane** (`test/integration/NativeDecoderFuzz.c` + `RunNativeDecoderFuzz.cmake`
  → `llvmdsdl-native-decoder-fuzz`) feeds arbitrary bytes into the generated
  `deserialize_` entrypoints for the adversarial shapes (union `Frame`, nested-delimited
  `port.List`, variable-array `ExecuteCommand`, narrow scalars) under ASan+UBSan, with
  auto-emitted valid seeds + a committed regression corpus
  (`test/fuzz/corpus/native_decoder/`). Bounded `-runs` on PRs, deep run on the weekly
  cron; crash reproducers upload as CI artifacts. Degrades loudly to an ASan/UBSan
  corpus replay when a toolchain lacks compiler-rt libFuzzer (never a silent no-op).
  All builds use the Debug config so `NDEBUG` is absent and the runtime `copy_bits`
  asserts still fire. *(Verified locally sans-ASan: harness compiles in all 3 modes,
  200k coverage-guided runs clean, 7 valid seeds emitted; the ASan runtime itself was
  unrunnable only on the macOS Apple-Silicon dev box — a known shadow-memory startup
  hang — so the lane is intentionally Linux-only.)*
- [~] **Reference-parity in CI**, byte-exact incl. unions/floats, broad type coverage.
  *(Runs in CI + byte-exact for all non-float and finite-float cases — 2026-07-10.)*
  **(a) Now runs in CI, non-silently.** The `linux` job provisions pinned
  nunavut+pydsdl checkouts (`.github/workflows/ci.yml`; consumed as source trees
  via `PYTHONPATH`, no pip) and configures with
  `-DLLVMDSDL_REQUIRE_DIFFERENTIAL_PARITY=ON`, so a missing reference compiler is
  a **hard configure error**, not a silent skip (the repo paths are overridable
  cache vars in `test/integration/CMakeLists.txt`). **(b) Byte comparison is now
  always on** (removed the opt-in `--strict-float-byte-parity` flag): all
  non-float types are byte-exact, and `Real32` (float32[<=64]) is byte-exact for
  every finite/inf value with a **NaN-payload carve-out** (verified clean to 500k
  random iterations). **(c) The `register.Value` union stays structural
  (rc+size)**, not byte-exact — root cause found: our C codegen normalizes a
  saturated float through a `float→double→float` round-trip
  (`(double)(obj->value.elements[i])` → double saturation helper → `(float)`),
  which canonicalizes a signaling NaN's mantissa MSB; the reference passes the
  raw float. Both are valid NaNs (Cyphal/IEEE-754 don't require preserving NaN
  payloads), so this is spec-permitted, not a correctness bug — but it blocks
  byte-exact parity for any float-carrying type. **(d) Coverage broadened 6 → 10
  cases (2026-07-10)** across new wire shapes, all byte-exact and verified clean
  to 300k iterations: `node.port.SubjectIDList.1.0` (a **byte-exact non-float
  tagged union** — sparse list / bool[8192] bitset / total, the union coverage
  the float-variant `register.Value` can't provide),
  `pnp.NodeIDAllocationData.2.0` (fixed `byte[16]` + variable-length optional),
  `diagnostic.Record.1.1` (nested composite timestamp + `uint8[<=255]`), and
  `time.SynchronizedTimestamp.1.0` (narrow non-byte-aligned `uint56`). **Remaining:**
  (i) fix the codegen to avoid the float→double round-trip (P2 below) → then flip
  `register.Value` to byte-exact; (ii) extend into the `reg`/UDRAL namespace
  (`node.port.List.1.0`, ~8.5 KB, needs the harness I/O buffers enlarged).
- [x] **Spec-conformance fix:** reject out-of-range primitive widths at frontend + IR verifier.
  *(Done 2026-07-10.)* The frontend now enforces the Cyphal Specification
  primitive bit-length ranges (from the "Serializable types" section) in
  `lib/Frontend/Parser.cpp` with per-kind diagnostics, so `int1`, `uint100`,
  `float8`, `int128`, and `void100` are rejected instead of lowering to
  non-conformant MLIR. **The spec gives signed and unsigned integers different
  minimums** — signed **[2, 64]** ("ranging from 2 to 64, inclusive"; the
  single-bit case is `bool`), unsigned **[1, 64]** — plus float ∈ **{16, 32, 64}**
  and void **[1, 64]**. (The "int/uint 1..64" shorthand in rec 5 / G7 below was
  imprecise; the spec text and its integer-type table are the authority, and
  `int1` matches the lexical name pattern `int[1-9]\d*` but is out of range.)
  Diagnostics read e.g. `invalid signed integer bit length 1; must be in the
  range [2, 64]`. As defense-in-depth, `IOOp::verify()` (`lib/IR/DSDLOps.cpp`)
  re-checks scalar `bit_length` against the same per-kind ranges (void also
  admits a degenerate 0-bit padding the lowering synthesizes and later drops),
  so no downstream pass or hand-authored IR can smuggle an out-of-range scalar
  into codegen. Covered by new negative+boundary cases in
  `test/unit/ParserTests.cpp` and the
  `test/lit/dsdl-io-invalid-scalar-bit-length.mlir` /
  `dsdl-io-invalid-signed-bit-length.mlir` verifier tests. *(Nuance: zero-width
  forms `uint0`/`int0`/`float0`/`void0` are already rejected one level up by the
  grammar's `[1-9]` leading-digit rule, so they fail with a type-resolution
  diagnostic rather than the bit-length message.)*
- [x] **Memory-safety hardening:** LSP allocation cap; ~~remove `assert()`-only guards from the release `copy_bits` path~~.
  *(Done 2026-07-10.)* **LSP allocation cap:** `JsonRpcStdioTransport` now bounds
  the framed payload — a `Content-Length` above `kDefaultMaxContentLength` (64 MiB,
  constructor-overridable) is rejected with `Content-Length exceeds maximum`
  *before* the payload buffer is allocated, and the header parser saturates
  instead of wrapping so a giant digit string can no longer overflow `size_t`
  into a small valid-looking length (`lib/LSP/JsonRpcIO.cpp`). Covered by new
  cases in `test/unit/LspJsonRpcFuzzTests.cpp` (oversized, overflowing, and
  at-cap-valid). **`copy_bits` asserts — premise withdrawn as overstated:** the
  release read/write paths are bounds-safe *by construction*, not via asserts.
  Every generated deserialize helper calls `saturate_fragment_bits` to clamp the
  read window to the buffer *before* `copy_bits` (`runtime/dsdl_runtime.h`
  get_bits/get_uxx), and every serialize helper checks buffer size and returns
  `-SERIALIZATION_BUFFER_TOO_SMALL` first; the `assert()`s in `copy_bits` are
  API-contract / loop-invariant checks (non-null, non-overlap, `size<=8`) that
  are *not* the memory-safety mechanism, so their absence under `NDEBUG` cannot
  cause an OOB access. They are correctly kept (zero release cost) and *do* fire
  in the ASan/UBSan+fuzz lane, which builds Debug precisely so they stay live.

### P1 — Required for a credible 1.0

- [ ] Semantics overflow/DoS hardening (`Rational`, `BitLengthSet`, capacity).
- [ ] Embedded-catalog integrity + freshness gating; runtime SHA-256 check.
- [ ] LSP concurrency correctness (DocumentStore mutex; analysis snapshot atomicity).
- [ ] Isolated cross-language runtime-primitive equivalence tests; populate/justify the wrapper allowlist.
- [ ] Determinism gates that actually perturb (`PYTHONHASHSEED`, locale/TZ, two toolchains) + audit `unordered_*` iteration in emitters.
- [ ] Object backend: escape/validate `targetTriple` and staged paths.

### P2 — Maturity / maintainability

- [ ] Reduce per-backend control-flow duplication (shared render template, or a verifier that the six emit orders match) — directly strengthens G1.
- [ ] Remove dead `dsdl.field`/`dsdl.constant` ops; add proactive verifiers in lowering.
- [ ] Split the largest emitters (Ts ~2.1k, Cpp ~2.0k LOC) into syntax/planning/naming modules.
- [ ] LLVM-version lock + multi-version EmitC testing; LSP logging for post-mortems; document the LSP "AI" surface's data flow.
- [ ] Security review of union-tag handling across backends; supply-chain/SBOM for release artifacts.
- [ ] **Float serialization: avoid the `float→double→float` round-trip.** Our C codegen normalizes a saturated `float32` field by promoting each element to `double`, applying a double-typed saturation helper, then narrowing back to `float` (`lib/CodeGen` / the `__llvmdsdl_plan_scalar_float__…__ser` helper). For finite values this is numerically exact, but it canonicalizes signaling-NaN mantissa payloads, diverging bit-for-bit from the reference compiler (which passes same-width floats through untouched). Saturating same-width floats in native `float` (no `double` hop) would preserve NaN bits, enable **byte-exact reference parity for all float-carrying types** (flip `register.Value` and any float case to byte-exact in the differential harness), and drop needless conversions. Cross-backend change — needs golden-file updates + re-parity across C/C++/Rust/Go/TS/Python.

**Bottom line for the maintainer:** the hard part — a real MLIR pipeline, a hardened frontend, a defensible runtime, and a genuine differential-testing harness — is already built and largely sound. What stands between this and "high-assurance public release" is mostly **(a) making the verification as strong as the documentation already claims it is**, and **(b) relabeling the few claims that are inherently marketing.** That is a focused, weeks-not-years effort, and most of it is additive testing rather than rearchitecting.
