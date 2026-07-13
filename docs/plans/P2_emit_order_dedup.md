# P2 — Reduce per-backend control-flow duplication (execution plan)

Executes the P2 roadmap line *"Reduce per-backend control-flow duplication (shared
render template, or a verifier that the six emit orders match)."* Sequenced **emit-order verifier → shared render template**:
first a behavioral verifier that the emit orders match (the net), then a shared render
template refactored under that net.

Companion: [P2_canonical_emit_order.md](P2_canonical_emit_order.md) — the canonical
step-order spec (Phase 0 output, the oracle both phases check against).

## Key architectural facts (grounded in the tree)

- **Scope = the 5 string emitters**: `RustEmitter`, `GoEmitter`, `CppEmitter`,
  `TsEmitter`, `PythonEmitter`. **C is out of scope**: it lowers through MLIR
  `convert-dsdl-to-emitc` (no string emitter) and is covered instead by the C↔{Go,Rust,Cpp}
  parity harnesses. State coverage honestly as "5 string emitters + C via parity", never "six".
- **Field order is already shared for native backends.** `traverseNativeSection`
  (`include/llvmdsdl/CodeGen/NativeEmitterTraversal.h`) drives field/padding/union order via
  callbacks (`onUnionDispatch`, `onFieldAlignment`, `onField`, `onPaddingAlignment`,
  `onPadding`); Rust/Go/Cpp each supply spelling callbacks. What is still hand-written per
  backend is (a) the **union prologue** (`onUnionDispatch` hands the whole union to each
  backend's `emitSerializeUnion`) and (b) **scalar/array/composite rendering**.
- **Two order-producers exist**: native (`PlannedFieldStep` walk via `traverseNativeSection`)
  and scripted (`buildScriptedSectionOperationPlan`, `ScriptedOperationPlan.cpp`, used by
  TS/Python). The end-state unifies them onto one step IR; the plan bridges incrementally.
- Because the native traversal already exists, **the shared render template is "reify a callback stream that
  already exists", not "six renderers from scratch."**

## Why the emit-order verifier comes before the shared render template

The emit-order verifier is the shared render template's safety net. Once the emit-order verifier's golden trace is green and stable, the shared render template becomes a
**behavior-preserving** refactor: the trace proves emitted order is byte-identical before and
after. Without the net, the shared render template is a large refactor betting against golden-output regressions with
nothing underneath.

---

## Phase 0 — Canonical order spec (~0.5–1 day) — *DONE as [P2_canonical_emit_order.md](P2_canonical_emit_order.md)*

The oracle. Enumerates the ordered abstract steps per direction for struct + union sections
and scalar/array/composite fields, and resolves the one known ambiguity (mask placement:
fold-into-write, matching Rust/Go; TS to be normalized in Phase 1d).

**DoD**: reviewed step list; every current divergence flagged normalize-or-accept. ✅

## Phase 1 — the emit-order verifier (~3–6 days) — *DONE (2026-07-12)*

| Step | Work | Files | Status |
|---|---|---|---|
| 1a | `enum class EmitTraceOp` + null-by-default `EmitTraceSink` (zero cost when off) | new `include/llvmdsdl/CodeGen/EmitTrace.h` | ✅ |
| 1b | `trace(op, …)` calls **at the text-emission site** in `emitSerialize{Union,Scalar,Array,Composite,Padding}` + align helpers | `{Rust,Go,Cpp,Ts,Python}Emitter.cpp` | ✅ |
| 1c | Comparator ctest: per fixture × direction, collect 5 traces, normalize, assert identical; + mutation/negative test | `test/integration/CMakeLists.txt`, `tools/convergence/emit_order_verifier.py` | ✅ |
| 1d | Normalize genuine **abstract-order** divergences the emit-order verifier surfaces (enumerated once instrumented). NB: TS mask-placement is spelling-only — a emit-order-verifier insensitivity check, not a fix | emitters | ✅ (none needed — see below) |
| 1e | Add to CI; relabel marker-regex "convergence 100" as an infra lint, point the real number at the emit-order verifier | `tools/convergence/*`, `docs/CONVERGENCE_SCORECARD.md` | ✅ |

**As landed (2026-07-12), beyond the original sketch:**

- **Per-(type, direction) segmentation.** Emitters record a `SECTION <canonical.name> <direction>`
  header at every serialize/deserialize entry (`EmitTraceSink::beginSection`; canonical =
  backend-independent DSDL name incl. `.Request`/`.Response`), so a divergence is reported
  against one concrete type + direction and misalignment cannot cancel across type boundaries.
- **Payload-aware skeletons.** The cross-backend comparison is over `(op, payload)` pairs —
  a wrong bit width, union option index, or prefix width fails even when op names agree.
- **Accepted differences are modeled, not ignored.** D2: `LEN_CHECK` dropped from the skeleton
  (type-system-subsumed in Go/C++). D3: the C++ fixed-bool-array fast path now traces an honest
  `BULK_COPY` op which the comparator expands to `ELEM_LOOP` + a 1-bit bool scalar — the
  declared equivalence, selftest-pinned. D4: `ADVANCE`/`STORE_TAG` positions are skeleton-free;
  membership still pins the safety order.
- **Fixture coverage**: union, variable/fixed arrays, floats (16/32/64), signed + `void`
  padding, fixed bool array (exercises D3), sealed composite (`COMPOSITE_INLINE`), delimited
  composite (`COMPOSITE_DELIM_HEADER`), array-of-composites, and a service type
  (`.Request`/`.Response` labels) — 26 segments, all green. **Plus the full embedded UAVCAN
  public-regulated corpus** (`--corpus`, wired into the ctest when the submodule is present):
  424 segments, all 5 backends agree, sub-second per backend. **1d outcome: zero unmodeled
  divergences** — every difference the comparator can see is D1 (invisible by design) or
  modeled D2/D3/D4.
- **Two negative controls as ctests**: `…-selftest` (checker logic: mask-before-validate
  rejected, payload divergence detected, D3 equivalence honored) and `…-mutation`
  (end-to-end: dsdlc re-run with `LLVMDSDL_EMIT_TRACE_MUTATE=swap-tag-validate` reorders the
  real recorded traces and the verifier must go red — proves the pipeline, not just the
  checker, has teeth). Residual honesty note: the trace is only as truthful as `trace()`
  adjacency to the `emitLine` it describes; that adjacency is a review invariant until
  Phase 2 makes order true by construction.

**Critical design point**: place `trace()` calls at the **spelling layer** (where text is
produced), not in the shared traversal. (1) tracing the traversal is tautological for order;
(2) spelling-site traces **survive Phase 2** — when the shared render template changes *who calls* the spelling, the
spelling still traces the same tokens in the same order, so the golden trace is unchanged and
genuinely proves the shared render template preserved behavior.

**DoD**: ctest green across all `test/integration` fixtures; mutation test red; CI runs it;
convergence claim relabeled (closes the G4 honesty gap). ✅ **Met 2026-07-12** — three ctests
(`llvmdsdl-emit-order-verifier`, `…-selftest`, `…-mutation`, labels
`integration;convergence;emit-order`) run in every ctest-driven CI lane; the convergence
scorecard preamble now names the emit-order verifier as the behavioral step-order check.

> **GATE:** the emit-order verifier green + stable on the branch before any Phase 2 work.
> **Status: OPEN — Phase 2 may begin.**

## Phase 2 — the shared render template, union prologue first (~1–2 wks prologue; deeper optional) — *union prologue DONE (2026-07-12)*

| Step | Work | Files | Status |
|---|---|---|---|
| 2a | `EmitStep` IR + `renderSteps(steps, BackendSpelling&)`; reify the `traverseNativeSection` callback sequence into `std::vector<EmitStep>` and **decompose `onUnionDispatch`** into ordered union sub-steps | new `include/llvmdsdl/CodeGen/EmitStep.h`, `lib/CodeGen/EmitStepRender.cpp` | ✅ (`UnionEmitStep` + `buildUnionSectionSteps` + `renderUnionSection`; the canonical prologue order lives in exactly one function) |
| 2b | `BackendSpelling` for Rust/Go/Cpp — **union prologue only** — delete hand-written prologue, route through `renderSteps`; the emit-order verifier proves order unchanged | `{Rust,Go,Cpp}Emitter.cpp` | ✅ (nested `UnionSpelling` classes; **full-UAVCAN-corpus generated output byte-identical** pre/post, proven by rebuild-and-diff) |
| 2c | Bring Ts/Python onto the same union-prologue steps (extend/converge `ScriptedSectionOperationPlan` → `EmitStep`); the emit-order verifier proves parity | `Ts/PythonEmitter.cpp` | ✅ (`TsUnionSpelling`/`PyUnionSpelling` + extracted case-body helpers; corpus diff is **exactly** the D4 normalization — the deserialize `ADVANCE`/`STORE` bookkeeping moved to canonical positions, 7 union types, nothing else) |
| 2d | Extend the step IR into scalar/array/composite so recursion is shared too; one lens at a time under the emit-order verifier | emitters | ✅ **native backends (2026-07-12)**: `FieldEmitStep` recursive tree + `buildFieldEmitSteps` + `renderFieldSteps` (EmitStep.h / EmitStepRender.cpp); Rust/Go/C++ field rendering fully routed, each proven **full-corpus byte-identical**. Scripted (TS/Python) = next tranche, design below |

**2d as landed (2026-07-12) — the recursion is now shared, not just the top level:**

- `buildFieldEmitSteps(type, facts, prefixOverride, direction)` builds a **step tree**:
  array nodes own their element's step subtree (`children`) — nesting/recursion is decided
  once, in shared code, from shared facts (semantic type, lowered facts, shared
  helper-symbol resolution, `buildArrayWirePlan`). `renderFieldSteps` owns **every
  cross-group ordering decision recursively**: scalar helper-before-write grouping,
  fixed-array guard→loop, variable-array length-group→loop, loop-contains-element-subtree,
  composite delimiter mechanics position. The per-backend `FieldSpelling` classes contain
  *zero sequencing* — only leaf statement idioms (temp names, casts, error channel,
  storage types, PMR flavor differences).
- **The accepted deviations became interface points**: D2 is
  `spellFixedArrayLenCheck` (Rust emits the runtime guard; Go/C++ implement it as a
  documented type-system-subsumed no-op), and D3 is `trySpellArrayBulkFastPath` (only C++
  implements it, for fixed bool arrays, emitting the honest `BULK_COPY` trace). A backend
  can no longer skip or reorder a step ad hoc — it can only exercise a declared right.
- **Byte-identity proof per backend**: Rust, Go, and C++ (all three profiles) each
  regenerate the full UAVCAN corpus byte-for-byte identically to the pre-refactor
  compiler. Byte-identity subtleties handled: `nextName` temp-name counters are shared
  and order-sensitive, so each spelling preserves its backend's original allocation
  order (they legitimately differ — e.g. C++ allocates `count_raw` before `count`,
  Rust/Go the reverse).
- **Remaining tranche — TS/Python onto the same tree.** Design: their per-kind if-chains
  map 1:1 onto the `FieldSpelling` methods (scalar kinds → `spellScalar*`, array blocks →
  the array group methods, composite helpers → `spellComposite*`); helper *names* resolve
  from the tree's helper *symbols* via the existing `helperBindingName{Ts,Py}` resolvers;
  TS's build-then-assign deserialize style stays inside its spelling (declare-vs-assign is
  a statement shape, not an order). Expected diff: identical or declared normalizations
  only, under the same double net.

**As landed (2026-07-12):**

- The union prologue order is now produced **by construction** from
  `buildUnionSectionSteps` — a backend physically cannot reorder validate/mask/write or
  read/mask/store/validate on its own; the emit-order verifier independently pins the same
  order (belt and braces).
- The `UnionSectionSpelling` interface expresses the real divergence axes: dispatch
  construct (Rust `match` / Go `switch` / C++ if-else-if / TS `switch`+`break` / Python
  if-elif), error channel (`Result`/`(rc,0)`/negative-int/`throw`/`raise`), and mask
  folding (native fold into the write argument; scripted spell it as a statement — D1).
  Helper-conditional guards in the scripted backends are preserved verbatim.
- **D4 is closed by construction**: TS/Python union deserialize now emits the canonical
  `READ → MASK → STORE → VALIDATE → ADVANCE` order; the raw prologue traces of all five
  backends are identical (the comparator's bookkeeping tolerance is retained for
  robustness, but nothing needs it for unions anymore). The TS/Python lit snapshot goldens
  were updated accordingly; C↔TS parity and the Python runtime/malformed suites confirm
  the reorder is behavior-preserving.
- **LOC delta (honest)**: roughly neutral-to-slightly-up (~250 lines of shared
  template/interface; per-backend union code about flat — hand-written sequencing deleted,
  spelling-class boilerplate added). The win is structural, not volumetric: 10 hand-written
  prologue sequencings (5 backends × 2 directions) collapsed into 1, which is what G1's
  "planned once, rendered six times" critique actually asked for.
- The marker-lint (`convergence_report.py`) was taught the spelling-class member form of
  the union-helper marker (`helperNames_.unionTagValidate`) — same shared machinery, new
  spelling site.

**Spelling interface must express the real divergence axes** (this preserves idiomatic
output): `match` vs `switch` vs `if/elif`; the error channel (`Result`/`Err` vs multi-return
`(rc,0)` vs negative-int vs `throw`/`raise`); and overflow ops as a step attribute
(`WRITE_SCALAR{wrap|saturate}` → `wrapping_*`/`@truncate`/explicit mask) so build-mode-dependent
arithmetic is pinned by the same verifier.

**Scope discipline**: union prologue for all 5 first — ship, measure LOC delta — *then* decide
on 2d. Leave C/EmitC on the MLIR path (step-IR-drives-EmitC is a separate epic; note it, do
not scope it).

**DoD**: union prologue emitted via one shared step-list + per-backend spelling across all 5;
emit-order-verifier trace unchanged (behavior preserved); LOC reduction measured; G1 grade rationale updated.
✅ **Met 2026-07-12** (traces unchanged for Rust/Go/Cpp; TS/Python changed only in the
D4-accepted bookkeeping positions, deliberately, closing D4; LOC measured ≈ neutral —
the deliverable is single-source sequencing, see above; G1 note updated in the roadmap).

## Risks & mitigations

| Risk | Mitigation |
|---|---|
| Trace placed wrong → tautological or breaks under the shared render template | Place at spelling site (1b); mutation test (1e) proves it detects reorders |
| The shared render template regresses idiomatic output | Spelling interface expresses match/switch/if + error model; the emit-order verifier is order-only, so pair with existing golden-output/parity tests for text quality |
| Two order-producers drift during 2c | Converge onto `EmitStep`; the emit-order verifier spans both, so drift fails the build |
| "Six" overclaim (C excluded) | Log the C/EmitC boundary explicitly; coverage = "5 string emitters + C via parity" |

## Roadmap fit

- **Phase 0 + 1 belong in alpha**: cheap, fix the G4 "convergence measures markers not
  behavior" honesty gap, and 1d catches real divergences. Output-affecting only where it
  *fixes* a bug.
- **Phase 2 is behavior-preserving under the emit-order verifier**, so it is safe even during alpha, and is the
  natural home for the alpha→beta-1 breaking-change window if we choose to also normalize
  output there.

**Effort**: Phase 0–1 ≈ 1 week; Phase 2 union-prologue ≈ 1–2 weeks; optional deep recursion
(2d) multi-week.
