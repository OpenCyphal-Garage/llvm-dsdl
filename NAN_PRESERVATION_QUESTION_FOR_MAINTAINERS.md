# Question for maintainers: what does "the original value will be preserved" mean for NaN?

**Status:** open question about DSDL specification interpretation. Not a known bug — a
request for clarification that affects float serialization conformance across all backends.

## TL;DR

For the **saturated** cast mode (the default), the DSDL specification says that when the
value being cast is infinity or not-a-number, "the original value will be preserved." We
need to know whether "preserved" means:

- **(a)** the value merely **remains a NaN** (payload/mantissa bits unspecified), or
- **(b)** the exact **bit pattern (including the NaN payload) is preserved** wherever the
  field width makes that possible.

The two readings diverge for `float16`, and they have implications for whether signaling
NaNs may be quieted on `float32`/`float64`.

## The specification text

From `specification/dsdl/serializable_types.tex` in the OpenCyphal specification repo,
describing the **saturated** cast mode:

> If the original value is finite, the nearest finite value will be used. Otherwise, in the
> case of infinity or not-a-number, the original value will be preserved.

(The **truncated** mode says, correspondingly: "Infinity with the same sign, unless the
original value is not-a-number, in which case it will be preserved.")

Source: <https://github.com/OpenCyphal/specification/blob/master/specification/dsdl/serializable_types.tex>
· <http://specification.opencyphal.org/Cyphal_Specification.pdf>

## What we observe today (all four native backends: C, C++, Rust, Go)

These backends share one runtime float16 conversion routine
(`dsdl_runtime_float16_pack` / `dsdl_runtime_float16_unpack` in `runtime/dsdl_runtime.h`,
the well-known "magic-float" algorithm also used by libcanard/Nunavut).

### float32 / float64 — exact bit pattern preserved

A signaling NaN survives a deserialize→serialize round-trip byte-for-byte, identically
across all four backends:

| input (wire, little-endian) | output |
| --- | --- |
| `01 00 80 7F` (float32 sNaN, quiet bit clear) | `01 00 80 7F` |

Note we do **not** set the quiet bit — the signaling NaN is preserved as-is. This is now
covered by a regression test in the C↔C++, C↔Rust, and C↔Go parity suites.

### float16 — NaN preserved, but payload canonicalized

The runtime conversion flattens any half-precision NaN payload to a single canonical value:

| input (wire, little-endian) | output |
| --- | --- |
| `01 7C` (half sNaN `0x7C01`) | `00 7E` (`0x7E00`, quiet NaN, payload dropped) |
| `01 7E` (half qNaN `0x7E01`) | `00 7E` (`0x7E00`, payload dropped) |

So under reading (b), float16 would be non-conforming; under reading (a) it is fine.

## Why reading (a) seems forced for float16 (but we want confirmation)

The native value being cast into a `float16` field is a `float32`/`float64`, whose NaN
payload is **23 bits** wide (binary32). A `float16` NaN payload is only **10 bits** wide.
A 23-bit payload cannot in general be represented in 10 bits, so a normative requirement to
"preserve the exact NaN payload" through a `float32 → float16` cast would be unsatisfiable.
That pushes us toward reading (a) ("remains a NaN") as the only coherent meaning at `float16`
width. IEEE 754 is consistent with this: it treats NaN payload propagation as a
*recommendation*, not a requirement, and explicitly permits quieting a signaling NaN on
format conversion.

We currently implement reading (a): NaN in → NaN out, payload canonicalized at `float16`,
exact payload preserved at `float32`/`float64` where the width allows it.

## Concrete questions

1. **Semantics of "preserved" for NaN.** Is the requirement (a) "the value remains a NaN,"
   or (b) "the exact bit pattern is preserved where representable"?

2. **float16 payload canonicalization.** If the answer is (a): is canonicalizing every
   `float16` NaN to a single pattern (`0x7E00`) acceptable, or should an implementation at
   least reproduce the low payload bits when the source NaN's payload already fits in the
   10 available bits (e.g. round-tripping a wire `0x7C01` back to `0x7C01`)?

3. **Signaling NaNs at float32/float64.** We currently preserve signaling NaNs exactly and
   do **not** set the quiet bit, reading "preserve the original value" literally. IEEE 754
   *recommends* quieting signaling NaNs on conversion. For a same-width `float32 → float32`
   store (which is arguably not a "conversion" at all), which behavior does the spec intend —
   preserve the signaling NaN as-is (what we do), or quiet it?

## Why this matters

Cross-implementation byte-exactness of the serialized form is a protocol-level concern.
If reading (b) is intended, or if maintainers want a specific float16 payload policy, then
the shared runtime float16 routine (and any conformance/differential test that tolerates NaN
payload differences) would need to change accordingly, and this would need to be coordinated
with the reference implementations (libcanard/Nunavut) to keep the ecosystem consistent.

## References

- Spec cast-mode text: `specification/dsdl/serializable_types.tex`
  (<https://github.com/OpenCyphal/specification/blob/master/specification/dsdl/serializable_types.tex>)
- Runtime float16 conversion: `runtime/dsdl_runtime.h`
  (`dsdl_runtime_float16_pack` / `dsdl_runtime_float16_unpack`)
- float32 signaling-NaN regression tests: `test/integration/CppCParityMain.cpp`,
  `test/integration/CRustParityMain.rs`, `test/integration/CGoParityMain.go`
