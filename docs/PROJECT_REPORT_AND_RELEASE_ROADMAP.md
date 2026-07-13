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

> **Update 2026-07-12 — both halves of this critique are now addressed for the union
> prologue.** The named failure mode ("mask-before-validate in one backend") is caught two
> independent ways: the **emit-order verifier** (behavioral trace comparison per (type,
> direction) against the Dafny-proven ordering class, in ctest/CI, with an end-to-end
> mutation negative control) and the **shared render template**
> (`include/llvmdsdl/CodeGen/EmitStep.h`): all five string emitters now render the union
> serialize/deserialize prologue from one canonical step list, so the order is correct *by
> construction*, with per-backend spelling classes carrying only surface idiom. Scalar/
> array/composite field bodies remain per-backend (optional P2 step 2d). See
> [docs/plans/P2_emit_order_dedup.md](plans/P2_emit_order_dedup.md).

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
10. ✅ **Done (2026-07-11).** `semantic_wrapper_allowlist.json` is **justified-empty** (rationale documented in-file; the validator only tracks hand-written, non-generated above-primitive wrappers, of which there are none, and the release-blocking allowlist lane passes). Added the **isolated** cross-language primitive equivalence harness (`llvmdsdl-primitive-equivalence`) over **C/Rust/Go/Python/TS**, asserting each primitive direction independently (float16 pack/unpack, sign-extend, unsigned read, `copy_bits`) so paired bugs can't mask each other — which immediately **caught and fixed a real Go+Rust `float16_pack` bug**. See the P1 entry.

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
  non-float types are byte-exact, and **every float-carrying type is byte-exact
  too** — including `Real32` and the `register.Value` union — after the C float
  codegen fix (2026-07-10). The scalar-float normalization helper is now
  **width-matched** (f32 for 16/32-bit fields, f64 for 64-bit) instead of always
  f64, so the generated C keeps a float in its native width end-to-end rather
  than promoting to `double` and narrowing back; that round-trip was
  canonicalizing signaling-NaN mantissa payloads and was the *sole* source of
  divergence from the reference. Verified byte-exact clean to **1,000,000** random
  iterations across all 10 cases. *(C/EmitC fix:
  `lib/Transforms/Passes.cpp` helper type + `lib/Transforms/ConvertDSDLToEmitC.cpp`
  casts/decl. The Cpp/Rust/Go emitters were width-matched the same day
  (`lib/CodeGen/HelperBindingRender.cpp` helper + the serialize/deserialize callers
  in `CppEmitter.cpp`/`RustEmitter.cpp`/`GoEmitter.cpp`), so all four native
  backends now keep floats native end-to-end and no longer canonicalize NaN via a
  double round-trip. TS/Python are inherently double-typed and cannot preserve
  float32 NaN payloads.)* **(d) Coverage broadened 6 → 10
  cases (2026-07-10)** across new wire shapes, all byte-exact and verified clean
  to 300k iterations: `node.port.SubjectIDList.1.0` (a **byte-exact non-float
  tagged union** — sparse list / bool[8192] bitset / total, the union coverage
  the float-variant `register.Value` can't provide),
  `pnp.NodeIDAllocationData.2.0` (fixed `byte[16]` + variable-length optional),
  `diagnostic.Record.1.1` (nested composite timestamp + `uint8[<=255]`), and
  `time.SynchronizedTimestamp.1.0` (narrow non-byte-aligned `uint56`); all 10 are
  byte-exact. **Remaining:** extend into the `reg`/UDRAL namespace and
  `node.port.List.1.0` (~8.5 KB, needs the harness I/O buffers enlarged). *(The
  Cpp/Rust/Go float helpers were width-matched for cross-backend consistency on
  2026-07-10 — see P2.)*
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

- [x] Semantics overflow/DoS hardening (`Rational`, `BitLengthSet`, capacity).
  ✅ **Done (2026-07-11).** `BitLengthSet` was already overflow-hardened (`__builtin_*_overflow`
  with saturation, defects BLS-D1…D16). This pass closes the other two:
  **`Rational`** (`lib/Support/Rational.cpp`) now evaluates `+ - * /` in 128-bit intermediates and
  **poisons** (sets a sticky `overflowed()` flag, clamping to 0) when the reduced result leaves
  64-bit range instead of invoking signed-overflow UB; comparisons cross-multiply in 128-bit so
  they are always exact; and `normalize()`/`gcd()` no longer negate/`llabs` `INT64_MIN`. The
  evaluator (`Evaluator.cpp`) surfaces a diagnostic on any poisoned result and special-cases
  `intPow` for bases in {−1, 0, 1} so a huge exponent can't spin the loop (a DoS), bailing as soon
  as the running product overflows for other bases; `INT64_MIN % -1` is also guarded. **Array
  capacity**: the length-prefix width is now computed as the bit-width of `capacity` rather than
  `ceilLog2(capacity + 1)`, so an adversarial `INT64_MAX` inclusive-array bound no longer overflows
  the `+ 1` (the repeat math already saturated in the hardened `BitLengthSet`). Covered by new
  `EvaluatorTests`/`AnalyzerTests` cases (overflow → diagnostic, `1 ** huge` terminates, boundary
  comparisons, adversarial capacities analyze without crashing); verified UB-free under UBSan.
- [x] Embedded-catalog integrity + freshness gating; runtime SHA-256 check.
  ✅ **Done (2026-07-11).** Freshness gating already existed — the release-blocking
  `llvmdsdl-embedded-uavcan-catalog-guard` runs `generate_embedded_uavcan_mlir.py --check`,
  regenerating the MLIR from the submodule and failing the build if the committed
  `UavcanEmbeddedMlir.inc` is stale (plus a guard selftest). This pass adds the missing
  **runtime SHA-256 check**: the generator already baked a `kEmbeddedUavcanMlirSha256` into the
  `.inc`, but nothing verified it. `loadUavcanEmbeddedCatalog` now computes `llvm::SHA256` over the
  embedded MLIR text and refuses to parse (hard error + diagnostic) on mismatch — catching binary/
  memory corruption, tampering, or a text/hash drift. Factored into a pure, testable
  `verifyEmbeddedCatalogIntegrity` (unit-tested with the shipped blob, the known empty-string
  digest, and a rejected mismatch). **Found and fixed a latent bug** doing so: the generator hashed
  `mlir_text` but the raw-string literal embedded `"\n" + mlir_text` (a leading newline), so the
  recorded hash never matched the bytes actually shipped; the generator now hashes the exact
  embedded content. Verified: unit tests, the freshness guard (regenerates byte-identically), the
  guard selftest, and the generator's own drift/roundtrip tests all pass.
- [x] LSP concurrency correctness (DocumentStore mutex; analysis snapshot atomicity).
  ✅ **Done (2026-07-11).** **DocumentStore** was the real gap: it had no synchronization and its
  `lookup()` returned a raw `const DocumentSnapshot*` into the internal map — a dangling-pointer /
  use-after-free hazard the instant a reader holds it across a concurrent `close()`/rehash. It now
  carries a `std::mutex` guarding every operation, and `lookup()` returns a value copy
  (`std::optional<DocumentSnapshot>`) so no pointer into the map can escape. Proven under
  **ThreadSanitizer**: the 8-thread stress test (new in `LspDocumentStoreTests`) is race-free with
  the mutex and TSan flags `unordered_map` rehash/read races the moment the locks are removed.
  **Analysis-snapshot atomicity already holds** and needed no change: analysis state
  (`latestAnalysisResult_`/`analysisDirty_`) is owned solely by the main message-loop thread, and the
  async Index worker receives a **versioned immutable copy** (`scheduleRebuild(snapshotVersion,
  analysis_.buildIndexShards())`, shards taken by value) rather than a reference to shared state —
  verified. *(Note: there is no live race today — request handlers dispatch inline on the main thread
  and only a cancellable-sleep test method is offloaded — so this hardens the shared store for the
  offload-to-worker path the scheduler architecture is built toward. A dedicated TSan CI lane would
  be a worthwhile follow-up; today the concurrency test runs under the normal and ASan lanes.)*
- [x] Isolated cross-language runtime-primitive equivalence tests; populate/justify the wrapper allowlist.
  ✅ **Equivalence tests done (2026-07-11).** A shared golden-vector file
  (`test/integration/primitive_vectors.txt`, 47 vectors) is run against **all five**
  runtimes — C, Rust, Go, Python, and TypeScript — by thin per-language drivers
  (`PrimitiveEquivalenceDriver.{c,rs,go,py,ts}`, wired via `RunPrimitiveEquivalence.cmake`
  → `llvmdsdl-primitive-equivalence`; TS is optional, gated on tsc/node/dsdlc). Each
  primitive **direction** is asserted on its own — `float16_pack`, `float16_unpack`,
  signed read/sign-extension (`get_i8/16/32/64`), unsigned read, and `copy_bits` at
  arbitrary bit offsets — so a bug in one direction can't be masked by its inverse.
  **This immediately caught a real bug:** both the Go and Rust `float16_pack` mis-ported
  the C algorithm's load-bearing unsigned wraparound subtraction (Go zeroed via a guard,
  Rust used `saturating_sub`), so *every finite float16 value serialized to garbage*
  (`1.0 → 0x0000`). It was masked because the Go parity harness set `requireByteParity:
  false` on all 24 float-carrying cases. Fixed both runtimes (`wrapping_sub` / unconditional
  wrapping subtraction), and flipped all 24 Go float cases to `requireByteParity: true`
  (they now agree byte-for-byte with C at 128 random iterations). The double-typed drivers
  (Python, TS) run the same vectors and explicitly **report** the small, principled skips
  they cannot represent at the raw-primitive level (Python: float16 overflow, which its
  primitive raises on while saturation happens one layer up; TS: float16-unpack NaN results,
  which a JS number canonicalizes) — never silent, and `processed + skipped` must still
  cover every vector. ✅ **Wrapper allowlist justified (2026-07-11):**
  `runtime/semantic_wrapper_allowlist.json` carries a `justification` documenting why it is
  intentionally empty (all Rust semantic wrappers are template-generated and excluded by the
  validator; no backend ships a hand-written above-primitive wrapper), and the
  release-blocking `llvmdsdl-runtime-semantic-wrapper-allowlist` lane passes.
- [~] Determinism gates that actually perturb (`PYTHONHASHSEED`, locale/TZ, two toolchains) + audit `unordered_*` iteration in emitters.
  ✅ **Audit + env-perturbation done (2026-07-11).** **Emitter audit came up clean:** the CodeGen
  layer uses no `llvm::DenseMap`/`DenseSet`/`SmallPtrSet` (whose iteration is pointer/insertion
  ordered), every `std::unordered_map`/`unordered_set` is a keyed *lookup/membership* structure that
  is never iterated to produce output, there is no locale- or time-dependent formatting, and the
  emitters already `std::sort`/`llvm::sort` (8 sites) wherever emission order is derived from a set.
  So the generated text is already iteration-order-independent. **Gates now actually perturb:** the
  six `RunUavcan*Determinism.cmake` gates previously ran two concurrent generations in the *same*
  environment (catching only concurrency/address nondeterminism); they now run the two generations
  under deliberately different `LC_ALL`, `TZ`, and `PYTHONHASHSEED`, so byte-identical output proves
  environment-independence and would catch any future locale/TZ/hash-order dependence. All gates
  pass under perturbation. **Remaining:** the **two-toolchain** dimension (build `dsdlc` against a
  second C++ stdlib, e.g. libstdc++ vs libc++, whose `std::hash` differs) is the only way to catch
  C++ `unordered_*` *iteration* nondeterminism that env-perturbation cannot — it catches nothing
  today given the clean audit, but is worth a CI lane; deferred as it needs a second build config.
- [x] Object backend: escape/validate `targetTriple` and staged paths.
  ✅ **Done (2026-07-12).** First, the reassuring part: the object backend already invokes the
  C/C++ compiler and archiver via **argv** (`llvm::sys::ExecuteAndWait`), never a shell, so there
  was no shell-injection vector (a hostile `--target-triple` can only ever be an inert `--target=`
  payload) — "escape" was moot; **validation** was the gap. Added: `--target-triple` is now checked
  against a conservative charset (hyphen-separated `[A-Za-z0-9._-]`, no leading dash, ≤128 chars) so
  whitespace/path/shell metacharacters are rejected up front with a clear diagnostic; `--obj-archive-name`
  must be a single safe filename component (no path separators or `..`), closing an archive-path
  traversal where `../../evil` would escape `--outdir`; and every derived object/archive path is
  containment-checked (`isPathWithinRoot`) to refuse writes outside the output root as
  defense-in-depth behind the input validation. New negative cases in `test/lit/cli.txt` prove the
  malicious triple and traversal archive name are rejected while a valid triple still passes.

### P2 — Maturity / maintainability

- [~] Reduce per-backend control-flow duplication (shared render template, or a verifier that the six emit orders match) — directly strengthens G1. **Execution plan: [docs/plans/P2_emit_order_dedup.md](plans/P2_emit_order_dedup.md)** (sequenced emit-order verifier → shared render template; canonical-order oracle in [P2_canonical_emit_order.md](plans/P2_canonical_emit_order.md)). ✅ **Phases 0–1 done (2026-07-12, branch `p2-emit-order-dedup`): the emit-order verifier is live.** All 5 string emitters (honest scope: C has no string emitter — it is covered by MLIR/EmitC + the C↔{Go,Rust,Cpp} parity harnesses) trace their abstract serialize/deserialize op stream per (type, direction) through a zero-cost-when-off side channel (`LLVMDSDL_EMIT_TRACE`); the comparator (`tools/convergence/emit_order_verifier.py`, three ctests incl. a checker selftest and an end-to-end trace-mutation negative control) asserts per-backend membership in the Dafny-proven safe ordering class (`spec/dafny/CyphalSerdes.dfy`, re-verified in CI) **and** cross-backend payload-aware wire-skeleton equality over union/array/float/padding/composite/service fixtures (26 segments) **plus the full UAVCAN public-regulated corpus (424 segments)** — all green, zero unmodeled divergences (accepted D2/D3/D4 differences are explicitly modeled in the comparator, D3 via an honest `BULK_COPY` op). The convergence scorecard preamble now points behavioral step-order claims at this verifier. ✅ **Phase 2 union prologue done (2026-07-12): the shared render template is live.** All five string backends render the union serialize/deserialize prologue through one shared step template (`include/llvmdsdl/CodeGen/EmitStep.h` — `buildUnionSectionSteps` is the single in-code statement of the canonical order) with per-backend `UnionSectionSpelling` classes expressing only the real divergence axes (match/switch/if-chain dispatch, Result/(rc,0)/negative-int/throw/raise error channels, mask folding). Proven behavior-preserving: Rust/Go/C++ full-corpus generated output is **byte-identical** pre/post (rebuild-and-diff), and the TS/Python diff is exactly the deliberate D4 bookkeeping normalization (now all five backends emit the canonical `READ→MASK→STORE→VALIDATE→ADVANCE` order by construction — D4 closed; lit snapshots updated; C↔TS parity + Python runtime suites green). **Remaining: optional 2d (extend the step IR into scalar/array/composite recursion) — decide after alpha feedback.**
- [ ] Remove dead `dsdl.field`/`dsdl.constant` ops; add proactive verifiers in lowering.
- [ ] Split the largest emitters (Ts ~2.1k, Cpp ~2.0k LOC) into syntax/planning/naming modules.
- [ ] LLVM-version lock + multi-version EmitC testing; LSP logging for post-mortems; document the LSP "AI" surface's data flow.
- [ ] Security review of union-tag handling across backends; supply-chain/SBOM for release artifacts.
- [x] **Float serialization: avoid the `float→double→float` round-trip.** ✅ **C/EmitC done (2026-07-10)** — the scalar-float helper is width-matched (f32 for 16/32-bit, f64 for 64-bit), so C keeps floats native end-to-end and preserves signaling-NaN payloads, giving byte-exact reference parity for all float-carrying types (`register.Value`, `Real32`) at 1M iterations. ✅ **Cpp/Rust/Go done (2026-07-10)** — the same width-match is now applied to the shared float helper (`lib/CodeGen/HelperBindingRender.cpp`: `const float`/`f32`/`float32` for 16/32-bit, `double`/`f64`/`float64` for 64-bit) and every serialize/deserialize caller (`CppEmitter.cpp`, `RustEmitter.cpp`, `GoEmitter.cpp`), so all four native backends keep floats in native width end-to-end and no longer canonicalize signaling-NaN payloads via a double round-trip. Verified against the uavcan-cpp-c-parity, uavcan-c-go-parity, uavcan-c-rust-parity, and generation suites. **Locked in by directed signaling-NaN payload regression cases** added to the cpp-c, c-rust, and c-go parity harnesses (feed the sNaN wire bytes `01 00 80 7F`, deserialize→reserialize, assert the quiet bit stays clear); a mutation reintroducing the round-trip makes them fail. **TS/Python are out of scope**: both are inherently double-typed and cannot preserve float32 NaN payloads, so full cross-language NaN byte-parity is not achievable regardless. *(Open spec question about what "the original value will be preserved" means for a NaN at `float16` width is captured in `NAN_PRESERVATION_QUESTION_FOR_MAINTAINERS.md`.)*

**Bottom line for the maintainer:** the hard part — a real MLIR pipeline, a hardened frontend, a defensible runtime, and a genuine differential-testing harness — is already built and largely sound. What stands between this and "high-assurance public release" is mostly **(a) making the verification as strong as the documentation already claims it is**, and **(b) relabeling the few claims that are inherently marketing.** That is a focused, weeks-not-years effort, and most of it is additive testing rather than rearchitecting.

---

## 6. Release Phasing (added 2026-07-12)

Everything above — P0 → P1, with P2 as maturity work — **is the alpha.** Language-target expansion is deliberately deferred past alpha so that a coherent first version reaches testers before the backend set grows, with the alpha → beta-1 boundary reserved as the sanctioned place to absorb breaking changes.

- [ ] **Alpha — limited release for initial feedback.** Scope = the current roadmap (P0 release-blockers first, then P1 for a credible cut; P2 as capacity allows). The target-language set is **frozen at the current six** — C, C++, Rust, Go, TypeScript, Python — plus their existing profiles (C++ `std`/`pmr`/`autosar`). Goal: get a good first version into alpha testers' hands and gather real usage feedback. **No new language backends in alpha.**

- [ ] **Beta 1 — add one new target from the deferred set.** After alpha, explore and prioritise the four candidates below and **(probably) pick one** to add first; the choice is driven by alpha feedback and demand, not decided now:
  - **Ada / SPARK** — safety-critical forcing function and the only shipping DO-178C-Level-A answer; intentionally breaks the C-syntax, exception, and integer-storage assumptions, so it doubles as the acceptance test for the shared-render abstraction. Cost: paid qualification path (GNAT Pro / SPARK Pro), Pascal-family emitter (largest template departure).
  - **Ferrocene** — really a `core`-only / `no_std` **profile of the existing Rust backend** compiled by a *certified* toolchain (ISO 26262 ASIL D, IEC 61508 SIL 3; DO-178C **not** yet achieved). Lowest syntax risk, highest ROI, and the one candidate with real first-party demand (no-std Rust).
  - **MicroPython** — a bignum / GC / `uctypes` **profile of the existing Python backend**; convenience and education nodes, non-safety-critical.
  - **WASM** — deployment target for edge / app-processor Cyphal nodes; reachable through the Rust backend (and, later, a TinyGo profile of Go); `i32`/`i64`-only, so it needs explicit narrowing codegen.
  - *Tenet to revisit when scoping:* pin the **execution model** (imperative, mutable in-place buffer, monotonic bit cursor) rather than "C-informed syntax," and let "could Ada slot in as a backend?" be the test of whether the shared-render / emit-order abstraction (P2) is genuinely language-neutral.

- [ ] **Beta 1 roadmap — authored after alpha feedback lands.** Not written now, by design. **Breaking changes are expected and acceptable across the alpha → beta-1 boundary** — this is the window to rework interfaces, land the shared-render / emit-order-verifier refactor (P2), and revise whatever alpha exposes, before API stability starts to matter.
