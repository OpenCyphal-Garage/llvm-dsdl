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

Weak where it matters most: **(a)** there is **no ASan/UBSan/MSan anywhere** (`grep fsanitize` across presets/CI/cmake = 0 hits); **(b)** only the *Python* runtime (memory-safe by construction) is fuzzed — the **C/C++/Go/Rust decoders, where OOB is actually possible, get hand-written single-case truncation tests, not fuzzing**; **(c)** ~~the "release-blocking malformed gate" is the name-presence metric above~~ **(fixed 2026-07-03: the malformed/parity/determinism gates now consume executed ctest pass/fail — behavioral, not name-presence)**; **(d)** the release `copy_bits` path uses `assert()` guards that vanish under `NDEBUG`. For an avionics-adjacent decoder of untrusted bytes, this is the single most important gap.

### G6 — Zero-overhead proof / verifier-first / fallback-free → **Overstated**
All three were read directly:

- **`dsdl-annotate-aliasability`** (renamed 2026-07-03 from `dsdl-prove-zero-overhead`; `lib/Transforms/Passes.cpp`) now matches its honest description — "annotate plans with *conservative* aliasability facts." It stamps `zoh_alias_eligible` on fixed-size/sealed/byte-aligned layouts; that flag flows out only as a generated **boolean constant** (`ZOH_ALIAS_ELIGIBLE = true/false`) and **does not switch the serializer to a zero-copy path**. It proves nothing about emitted-code overhead. (It *is* well unit-tested as an annotator, incl. a negative case.) *(The emitted `ZOH_ALIAS_ELIGIBLE`/`zoh_alias_*` surface was deliberately left unrenamed — it is a generated-API and doesn't itself claim a proof.)*
- **"verifier-first"** is **post-hoc**: invariants are validated on already-lowered IR, not enforced during lowering (`lib/Lowering/LowerToMLIR.cpp` adds no MLIR verifiers/constraints).
- **"fallback-free"** is scored by grepping cmake gate files for regexes.

### G7 — DSDL v1.0 conformance → **Partial**
Strong: the lexer/parser went through **6 documented rounds of grammar conformance** and reserved-identifier enforcement; all six directives (`@union/@sealed/@extent/@assert/@print/@deprecated`) are parsed and semantically checked; **delimited (non-sealed) bit-length sets are modeled correctly** (32-bit delimiter header + 0..extent), matching pydsdl/Nunavut.

Real conformance bug found and verified live: **primitive bit-width constraints are not enforced.** `uint100` and `float8` pass parse + semantic analysis and emit structurally-valid MLIR (`lib/Frontend/Parser.cpp:725` only checks uint32 fit; no range check in `lib/Semantics/Analyzer.cpp` or the scalar IR verifier). The reference compiler rejects these with diagnostics. And the **differential parity vs Nunavut covers exactly 5 files** (`test/integration/RunDifferentialParity.cmake:99`), **disables byte comparison for the union and float cases**, and **is skipped in CI** (the workflow never provisions `nunavut`/`pydsdl`).

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
| LSP (`dsdld`) | **6** | Reuses compiler core (good); **unbounded `Content-Length` allocation (OOM DoS)**, DocumentStore thread-safety. |
| Tools / CLI | **6** | Good arg/exit discipline; embedded-catalog freshness & integrity ungated. |
| Build / CI / test infra | **6** | Strong test culture; **gates are name-presence metrics**; silent toolchain skips; CI uncommitted. |
| **Headline-claim integrity** | **5** | The dominant production risk: docs promise proofs the code doesn't deliver. |

---

## 4. Top Recommendations (by leverage)

**Highest leverage — make the claims true or relabel them:**

1. **Re-found the three scorecards on behavior, not markers.** ✅ **Done (2026-07-03) for parity/malformed/determinism** — they consume `ctest` **pass/fail** (JUnit) via `tools/convergence/ctest_results.py`; a cell is covered only if a matching test ran and passed. Convergence relabeled as an **infrastructure-consistency lint** (`docs/CONVERGENCE_SCORECARD.md`) rather than made behavioral (it is inherently a marker check). Deriving convergence from generated-output/AST equivalence remains a worthwhile P1 deepening.
2. ✅ **Done (2026-07-03).** Renamed `dsdl-prove-zero-overhead` → `dsdl-annotate-aliasability` and dropped "proof" language; documented `dsdl-legalize-endianness` as validation-only (no byte reordering). ~~mark `--target-endianness big` EXPERIMENTAL/unsupported until byte-swap logic exists~~ — **withdrawn as overstated:** DSDL wire is always little-endian, so there is no byte-swap to implement; `serialize_`/`deserialize_` are host-endianness-agnostic and byte-parity-tested against little-endian in the `-l obj` smoke test. Only the zero-copy *view* fast-path is disabled on BE (returns an error), which is correct, not missing.
3. **Build the verification the docs already promise:** run the **Nunavut differential parity in CI** (provision `nunavut`/`pydsdl`), enable byte comparison for union/float cases, and expand coverage from 5 types to a representative cross-section (and the `reg`/UDRAL namespace).

**Safety / high-assurance:**

4. **Add an ASan+UBSan CI lane** that compiles and runs the generated **native** decoders under sanitizers, plus a **libFuzzer lane over the generated C/C++ deserializers** on the real UAVCAN corpus (nested delimited, unions-of-composites, fixed-array-of-variable-array).
5. **Enforce primitive bit-length-set constraints** (int/uint 1..64, float ∈ {16,32,64}, void 1..64) in the frontend/semantics *and* the IR scalar verifier, with pydsdl-grade diagnostics.
6. **Bound the LSP:** cap `Content-Length` before allocation (`lib/LSP/JsonRpcIO.cpp:88`); make `DocumentStore` access thread-safe.
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
- [ ] **Sanitizers + native decoder fuzzing in CI.** ASan/UBSan over generated C/C++/Go; libFuzzer over native deserializers on the real corpus.
- [ ] **Reference-parity in CI**, byte-exact incl. unions/floats, broad type coverage.
- [ ] **Spec-conformance fix:** reject out-of-range primitive widths at frontend + IR verifier.
- [ ] **Memory-safety hardening:** LSP allocation cap; remove `assert()`-only guards from the release `copy_bits` path.

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

**Bottom line for the maintainer:** the hard part — a real MLIR pipeline, a hardened frontend, a defensible runtime, and a genuine differential-testing harness — is already built and largely sound. What stands between this and "high-assurance public release" is mostly **(a) making the verification as strong as the documentation already claims it is**, and **(b) relabeling the few claims that are inherently marketing.** That is a focused, weeks-not-years effort, and most of it is additive testing rather than rearchitecting.
