# Canonical emit order

The reference **abstract** serialize/deserialize step order that every string backend
(Rust, Go, C++, TypeScript, Python) follows. This is a live contract: the shared render
template produces this order by construction, and the emit-order verifier
(`tools/convergence/emit_order_verifier.py`, ctest `llvmdsdl-emit-order-verifier`)
independently pins it on every build.

This prose is the human-readable projection of the machine-checked model in
[spec/dafny/CyphalSerdes.dfy](https://github.com/OpenCyphal-Garage/llvm-dsdl/blob/main/spec/dafny/CyphalSerdes.dfy), which *proves* (unbounded,
by induction) that serialize/deserialize round-trip and that the read path is bounds-safe, and
defines the accepted op orderings (`SerOrderOK`/`DeOrderOK`). The model is the source of truth;
this doc is the readable shadow.

**Where the order lives in code.** `buildUnionSectionSteps` and `buildFieldEmitSteps`
([`include/llvmdsdl/CodeGen/EmitStep.h`](https://github.com/OpenCyphal-Garage/llvm-dsdl/blob/main/include/llvmdsdl/CodeGen/EmitStep.h)) build the step list/tree;
`renderUnionSection` and `renderFieldSteps`
([`lib/CodeGen/EmitStepRender.cpp`](https://github.com/OpenCyphal-Garage/llvm-dsdl/blob/main/lib/CodeGen/EmitStepRender.cpp)) own every
cross-statement ordering decision, recursively. Per-backend `UnionSectionSpelling` /
`FieldStepSpelling` classes contain *zero sequencing* — only leaf statement idioms. A
backend cannot reorder these steps; it can only exercise a declared right
(see [Accepted differences](#accepted-differences)).

## Abstract order vs spelling

The emit-order verifier checks **abstract op order**, not surface text. Two backends agree if they perform the same
ordered sequence of abstract ops, regardless of how each op is spelled.

- **Abstract op** (the emit-order verifier asserts these, cross-backend identical): `VALIDATE_TAG`, `MASK_TAG`,
  `WRITE_TAG`, `ADVANCE`, `SWITCH`, `CASE`, `ALIGN`, `WRITE_SCALAR`, … (full list below).
- **Spelling** (the shared-render-template visitor varies these; the emit-order verifier is deliberately blind to them): whether a
  mask is a separate statement or an inline sub-expression; `match` vs `switch` vs `if/elif`;
  `Result`/`Err` vs `(rc,0)` vs negative-int return vs `throw`/`raise`; identifier names;
  indentation.

Example: in `serialize` of a union tag, Rust/Go write `set_uxx(buf, off, mask(tag), bits)`
(mask folded into the write argument) while TS emits `tag = mask(tag);` then
`writeUnsigned(...tag...)` (mask as its own statement). **Both are `VALIDATE_TAG → MASK_TAG →
WRITE_TAG`** — identical abstract order, different spelling. The emit-order verifier treats them as equal. This
case doubles as a verifier insensitivity check (D1 below).

## Two invariant principles

1. **Write path validates before it emits; read path reads before it validates.** On
   serialize, a value is range/tag/length-validated *before* the corresponding `WRITE_*`. On
   deserialize, the raw bits are `READ_*` first, then masked/validated. This asymmetry is
   intentional and appears identically for the union tag and the array length prefix.
2. **`ADVANCE` (`offset_bits += N`) happens immediately after the matching `WRITE_*`/`READ_*`,
   never before.** Alignment padding (`ALIGN`) is emitted *before* a field's value ops.

## Section level

### Struct (non-union) — serialize and deserialize
For each lowered field step, in lowered order:
```
(padding step)  → ALIGN(pad.bits) → PAD(write/skip zero bits) → ADVANCE
(value step)    → ALIGN(field.alignmentBits) → <field ops, see Field level>
end of section  → ALIGN(8)          # trailing pad to byte boundary
```

### Union — serialize
```
VALIDATE_TAG          # unionTagValidate(tag); error-branch on failure
MASK_TAG              # unionTagMask(tag)
WRITE_TAG(tagBits)    # set_uxx / writeUnsigned
ADVANCE(tagBits)
SWITCH(tag) {
  CASE(optionIndex):
    ALIGN(option.alignmentBits)
    <option field ops, see Field level>
  ...
  DEFAULT: DEFAULT_BAD_TAG          # -REPRESENTATION_BAD_UNION_TAG
}
```

### Union — deserialize
```
READ_TAG(tagBits)     # get_u64 / readUnsigned
MASK_TAG              # unionTagMask(raw)
STORE_TAG             # obj.tag = masked
VALIDATE_TAG          # unionTagValidate(tag); error-branch
ADVANCE(tagBits)
SWITCH(tag) { CASE(optionIndex): ALIGN(...) <field ops> ... DEFAULT: DEFAULT_BAD_TAG }
```
Note `READ → MASK → STORE → VALIDATE` on deserialize vs `VALIDATE → MASK → WRITE` on serialize
(principle 1). All five backends emit this by construction via `renderUnionSection`.

## Field level

`emitSerializeAny` / `emitDeserializeAny` dispatch on cardinality: array → [Array], else
scalar/composite → [Scalar]/[Composite].

### Scalar — serialize (by `SemanticScalarCategory`)
```
Bool:                       WRITE_SCALAR(set_bit, 1)             → ADVANCE(1)
Unsigned/Byte/Utf8:  CAST(u64) → HELPER(mask|saturate) → WRITE_SCALAR(set_uxx, bits) → ADVANCE(bits)
Signed:              CAST(i64) → HELPER(mask|saturate) → WRITE_SCALAR(set_ixx, bits) → ADVANCE(bits)
Float:               CAST(fN)  → HELPER(width-matched)  → WRITE_SCALAR(set_f16|f32|f64) → ADVANCE(bits)
Void:                → [Padding]
Composite:           → [Composite]
```
The saturating/masking `HELPER` is applied to the value **before** `WRITE_SCALAR` (principle 1).
Float helpers are width-matched (f32 for 16/32-bit, f64 for 64-bit) so no native backend
canonicalizes signaling-NaN payloads through a `float→double→float` round-trip.

### Scalar — deserialize
```
Bool:                       READ_SCALAR(get_bit)                → ADVANCE(1)
Unsigned/Byte/Utf8:  READ_SCALAR(get_uN) → HELPER(mask) → CAST(store) → ADVANCE(bits)
Signed:              READ_SCALAR(get_u64) → HELPER → CAST(signed store) → ADVANCE(bits)
Float:               READ_SCALAR(get_fN) → HELPER → CAST(store) → ADVANCE(bits)
```

### Array — serialize
```
Fixed:     LEN_CHECK(len == capacity)          # exact-length guard
Variable:  LEN_VALIDATE(len) → MASK(prefix) → LEN_WRITE(prefixBits) → ADVANCE(prefixBits)
ELEM_LOOP(0..count) { <element scalar/composite ops> }     # count = len (var) or capacity (fixed)
```

### Array — deserialize
```
Variable:  LEN_READ(prefixBits) → ADVANCE(prefixBits) → MASK(prefix) → STORE(count) → LEN_VALIDATE(count)
Fixed:     count = capacity
CLEAR → RESERVE(count) → ELEM_LOOP(0..count) { DEFAULT_ELEM → <element deserialize ops> → PUSH }
```
Same validate asymmetry: serialize `LEN_VALIDATE` before `LEN_WRITE`; deserialize `LEN_READ`
before `LEN_VALIDATE`.

Array steps own their element's step subtree, so nesting is decided once in shared code and
`renderFieldSteps` recurses into the element for both directions.

### Composite
```
Sealed:     COMPOSITE_INLINE           # inline nested serialize/deserialize at current offset
Delimited:  COMPOSITE_DELIM_HEADER(32) # 32-bit delimiter/extent header, then nested payload
```

**Sealed** — no header. The nested call runs against the buffer from the current byte offset;
the cursor advances by the bytes the nested call reports consuming (`ADVANCE(consumed * 8)`).

**Delimited — serialize** (length backpatch):
```
ADVANCE(32)                             # reserve the header slot; write it last
size = ceil(bitLengthSet.max() / 8)     # worst-case bound for the sub-buffer
VALIDATE(size <= remaining capacity)    # delimiterValidateSymbol helper; error-branch
nested serialize into buffer[start .. start+size]   # returns the ACTUAL byte count
size = <actual returned count>
WRITE header(32) at (offset_bits - 32) = size       # the backpatch
ADVANCE(size * 8)
```

**Delimited — deserialize** (bounded read):
```
READ header(32) → size
ADVANCE(32)
VALIDATE(size <= remaining capacity)    # same helper; error-branch
nested deserialize from buffer[start .. start+size]  # bounded to exactly `size` bytes
ADVANCE(size * 8)                       # by the HEADER size, never by nested-consumed
```
That last line is the forward-compatibility rule and is load-bearing: advancing by the
nested call's consumed count instead of the header value silently corrupts version-skew
decoding, because a newer sender's extra trailing bytes must be *skipped*, not re-read as
the next field. The trace records one `COMPOSITE_DELIM_HEADER` op; the mechanics above are
spelled per backend inside `spellCompositeSerialize` / `spellCompositeDeserialize`.

### Padding / void
```
ALIGN(pad.bits) → PAD(write/skip zeroed bits) → ADVANCE(pad.bits)
```

## Abstract op vocabulary

Mirrored by `enum class EmitTraceOp` in
[`include/llvmdsdl/CodeGen/EmitTrace.h`](https://github.com/OpenCyphal-Garage/llvm-dsdl/blob/main/include/llvmdsdl/CodeGen/EmitTrace.h) — this doc and
that enum are kept in step.

`VALIDATE_TAG` · `MASK_TAG` · `WRITE_TAG` · `READ_TAG` · `STORE_TAG` · `SWITCH` · `CASE` ·
`DEFAULT_BAD_TAG` · `ALIGN` · `WRITE_SCALAR{bool|uint|sint|float}` ·
`READ_SCALAR{bool|uint|sint|float}` · `LEN_CHECK` · `LEN_VALIDATE` · `LEN_WRITE` · `LEN_READ` ·
`ELEM_LOOP` · `BULK_COPY` · `COMPOSITE_INLINE` · `COMPOSITE_DELIM_HEADER` · `PAD` · `ADVANCE`.

Each op carries a small payload where relevant (bit width, option index, scalar sub-kind). The emit-order verifier
normalizes away identifiers and indentation, keeping op + payload — payloads are compared,
so a wrong bit width or option index fails even when op names agree.

Two structural elements beyond the wire ops: `BULK_COPY` (payload = total bits) is the honest trace
of the C++ fixed-bool-array fast path — the D3 declared equivalence
`BULK_COPY ≡ ELEM_LOOP + 1-bit bool scalar` is applied by the comparator.
`SECTION <canonical.name> <serialize|deserialize>` header events segment the trace per
(type, direction) so divergences localize and cannot cancel across type boundaries.

**Trace calls live at the spelling sites, not in the shared render template.** This is
deliberate: tracing the shared sequencer would be tautological for order. Because each
backend traces the tokens it actually emits, the verifier stays an independent check on the
shared template rather than a restatement of it.

## Accepted differences

Cross-backend differences fall into three classes: invisible by design (spelling), a declared
right exercised through a named interface point, or genuine abstract-order divergence. Only
the third is a defect. All three are explicitly modeled. The marker-regex "convergence"
score cannot see structural facts of this kind.

| # | Backend(s) | Kind | Resolution |
|---|---|---|---|
| D1 | TS/Python mask the union tag as its own statement; Rust/Go fold it into the write argument | **Spelling only** — abstract order `VALIDATE→MASK→WRITE` is identical | Both kept. Serves as the verifier's insensitivity case: it must report *equal* |
| D2 | Fixed-array `LEN_CHECK`: **Rust emits it** (Vec/slice, runtime `len != capacity` guard); **Go/C++ do not** (fixed arrays are compile-time-sized `[N]T` / `std::array`, so the guard is subsumed by the type system — no emit site) | **Structural, accepted** — type-system-subsumed, not missing | Declared interface point `FieldStepSpelling::spellFixedArrayLenCheck` (a documented no-op in Go/C++); `LEN_CHECK` is backend-optional in the comparator skeleton |
| D3 | C++ has a **bulk-copy fast path for fixed `bool` arrays** (`dsdl_runtime_copy_bits`/`get_bits`) that returns before the element loop, so it emits no `ELEM_LOOP` / per-element scalar ops for that case | **Genuine C++ optimization** | Declared interface point `FieldStepSpelling::trySpellArrayBulkFastPath`; traces an honest `BULK_COPY`, and the comparator applies the declared equivalence `BULK_COPY ≡ ELEM_LOOP + 1-bit bool scalar` (selftest-pinned, exercised by the `BoolArray` fixture) |
| D4 | *(none)* | — | All five backends render the union prologue through `renderUnionSection`, so their raw prologue traces are identical by construction. The comparator carries a tolerance for `STORE`/`ADVANCE` bookkeeping positions anyway: it costs nothing, and it is the axis a hand-written prologue would drift along first |
| D5 | *(further genuine reorderings)* | **None.** All 5 backends verified over unions, variable/fixed arrays, floats, signed+void padding, fixed bool arrays, sealed + delimited composites, arrays of composites, and a service type (26 fixture segments), plus the full UAVCAN public-regulated corpus (424 segments) | Zero unmodeled divergences |

## Enforcement

Three ctests (labels `integration;convergence;emit-order`) run in every ctest-driven CI lane:

| Test | What it proves |
|---|---|
| `llvmdsdl-emit-order-verifier` | Every backend's trace is a member of the Dafny-proven safe ordering class, and the payload-aware wire skeletons agree cross-backend — over the fixtures and, when the submodule is present, the full UAVCAN corpus |
| `llvmdsdl-emit-order-verifier-selftest` | The checker itself has teeth: mask-before-validate rejected, payload divergence detected, D3 equivalence honored, `BULK_COPY` without `ADVANCE` rejected |
| `llvmdsdl-emit-order-verifier-mutation` | The whole pipeline has teeth: dsdlc re-runs with `LLVMDSDL_EMIT_TRACE_MUTATE=swap-tag-validate` and the verifier must go red |

**Scope**: the 5 string emitters. **C is out of scope** — it lowers through MLIR
`convert-dsdl-to-emitc` with no string emitter, and is covered instead by the
C↔{Go,Rust,Cpp} parity harnesses. State coverage as "5 string emitters + C via parity",
never "six". Driving EmitC from the step IR is a separate, unscoped epic.

## History

This document is the standing specification of the emit order. It began as the Phase 0
oracle for the P2 emit-order deduplication work, which sequenced the emit-order verifier
before the shared render template and completed on 2026-07-12. That effort's own record —
per-phase status, the byte-identity proofs, the LOC-delta accounting — is kept in the
G1 section of [the project report](../../development/roadmap.md) and in git
history (`docs/plans/P2_emit_order_dedup.md`, removed 2026-07-31).
