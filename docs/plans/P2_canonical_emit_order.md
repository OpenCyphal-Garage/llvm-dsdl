# Canonical emit order (the oracle)

Phase 0 output for [P2_emit_order_dedup.md](P2_emit_order_dedup.md). This is the reference
**abstract** serialize/deserialize step order that every string backend
(Rust, Go, C++, TypeScript, Python) must follow. The emit-order verifier asserts this order; the shared render template
produces it by construction.

This prose is the human-readable projection of the machine-checked model in
[spec/dafny/CyphalSerdes.dfy](../../spec/dafny/CyphalSerdes.dfy), which *proves* (unbounded,
by induction) that serialize/deserialize round-trip and that the read path is bounds-safe, and
defines the accepted op orderings (`SerOrderOK`/`DeOrderOK`). The model is the source of truth;
this doc is the readable shadow.

Derived from the current implementations (`RustEmitter.cpp`, `GoEmitter.cpp`, `TsEmitter.cpp`)
which already agree on it. Where a backend differs, it is recorded under
[Known differences](#known-differences).

## Abstract order vs spelling — the load-bearing distinction

the emit-order verifier checks **abstract op order**, not surface text. Two backends agree if they perform the same
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
WRITE_TAG`** — identical abstract order, different spelling. the emit-order verifier must treat them as equal. This
case doubles as a emit-order-verifier insensitivity test (see [Known differences](#known-differences)).

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
(principle 1).

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
Float helper is width-matched (f32 for 16/32-bit, f64 for 64-bit) — see the P2 float item.

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

### Composite
```
Sealed:     COMPOSITE_INLINE           # inline nested serialize/deserialize at current offset
Delimited:  COMPOSITE_DELIM_HEADER(32) # 32-bit delimiter/extent header, then nested payload
```
Exact delimiter-header mechanics (length backpatch on serialize; bounded read on deserialize)
to be captured precisely during Phase 1b instrumentation.

### Padding / void
```
ALIGN(pad.bits) → PAD(write/skip zeroed bits) → ADVANCE(pad.bits)
```

## Abstract op vocabulary (for `EmitTrace.h`, Phase 1a)

`VALIDATE_TAG` · `MASK_TAG` · `WRITE_TAG` · `READ_TAG` · `STORE_TAG` · `SWITCH` · `CASE` ·
`DEFAULT_BAD_TAG` · `ALIGN` · `WRITE_SCALAR{bool|uint|sint|float}` ·
`READ_SCALAR{bool|uint|sint|float}` · `LEN_CHECK` · `LEN_VALIDATE` · `LEN_WRITE` · `LEN_READ` ·
`ELEM_LOOP` · `COMPOSITE_INLINE` · `COMPOSITE_DELIM_HEADER` · `PAD` · `ADVANCE`.

Each op carries a small payload where relevant (bit width, option index, scalar sub-kind). the emit-order verifier
normalizes away identifiers and indentation, keeping op + payload.

## Known differences

| # | Backend(s) | Kind | Canonical decision | Phase |
|---|---|---|---|---|
| D1 | TS masks union tag as its own statement (`TsEmitter.cpp:600`); Rust/Go fold it into the write arg | **Spelling only** — abstract order `VALIDATE→MASK→WRITE` is identical | Keep both; use as a emit-order-verifier insensitivity test (the emit-order verifier must report *equal*) | 1c |
| D2 | Fixed-array `LEN_CHECK`: **Rust emits it** (Vec/slice, runtime `len != capacity` guard); **Go/C++ do not** (fixed arrays are compile-time-sized `[N]T` / `std::array`, so the guard is subsumed by the type system — no emit site) | **Genuine structural difference, accepted** — the check is type-system-subsumed, not missing. the emit-order verifier treats `LEN_CHECK` as a backend-optional op (tolerated absent) | 1c comparator |
| D3 | C++ has a **bulk-copy fast path for fixed `bool` arrays** (`dsdl_runtime_copy_bits`/`get_bits`) that returns before the element loop, so it emits **no `ELEM_LOOP` / per-element scalar ops** for that case | **Genuine C++ optimization** — real divergence on fixed-bool-array fixtures only | Note as accepted; 1c comparator scopes or annotates fixed-bool-array cases |
| D4 | **Python union deserialize** emits `READ_TAG → ADVANCE → MASK_TAG → VALIDATE_TAG → STORE_TAG`; Rust/canonical is `READ → MASK → STORE → VALIDATE → ADVANCE` | **Genuine order difference, safe** — read-before-mask-before-validate all hold (Python even validates *before* storing); only `STORE`/`ADVANCE` bookkeeping positions differ | **Accept**, and *loosen the model's `DeOrderOK`* to the safety-critical core rather than pin `STORE`/`ADVANCE` to Rust's positions. Reveals the oracle was over-constrained. | model refinement + 1c |
| D5 | *(further genuine reorderings, if any, enumerated once the comparator runs over all five)* | — | Normalize (fix) or accept + document | 1d |

Only a **genuine abstract-order divergence** (e.g. a backend that masks before validating, or
advances before writing) is a Phase 1d fix. Spelling differences (D1) and type-system-subsumed
or optimization differences (D2, D3) are **accepted and handled in the comparator**, not
"fixed" — but they must be *explicitly modeled*, never silently ignored. This is exactly the
class of structural fact the old marker-regex "convergence" score could not see.
