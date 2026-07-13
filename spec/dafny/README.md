# CyphalSerdes — formal control-flow / round-trip model (Dafny)

A Dafny model of Cyphal DSDL serialize/deserialize as functions over an abstract wire,
with machine-checked proofs. It is the normative **oracle** for the emit-order verifier
(B1): the op-orderings established here are what each generated backend
(C/C++/Rust/Go/TS/Python) is checked against. The prose
[P2_canonical_emit_order.md](../../docs/plans/P2_canonical_emit_order.md) is the
human-readable projection of this module.

## Scope (deliberate)

Models the **sequence of wire operations** and the **structural invertibility** of
serialize/deserialize. The wire is an ordered stream of typed *tokens*; a scalar's value is
an opaque tag that must survive the round trip.

**Token granularity:** tokens are atomic — the model never splits a field. This is exact for
schema-evolution skew (appended fields always start on a token boundary) and deliberately
coarse for arbitrary corruption: a buffer cut mid-scalar is below the abstraction floor.
Byte-level truncation is exercised empirically by the sanitizer/fuzz harnesses instead.

Variable arrays carry their capacity: conformance bounds the element count
(`|elems| <= cap`) and `De` validates the wire's length prefix against it — the functional
counterpart of the `LEN_VALIDATE` op, the same way `VALIDATE_TAG` corresponds to the union
tag-range guard.

**Out of scope — verified empirically instead:** bit-exact byte layout, integer widths,
saturation/truncation, endianness, NaN payloads. Those are covered by the differential-parity
and cross-language round-trip harnesses.

## What is proven

| Property | How | Strength |
|---|---|---|
| **Round-trip identity** — `De(t, SerWire(v) + suffix) == Some((v, suffix))` for every value `v` conforming to type `t` | `lemma RoundTrip` (+ `RoundTripSeq`/`RoundTripCount`), by structural induction | **Unbounded** — holds for *all* conforming values, not a bounded sample |
| **Canonical acceptance** — the converse: every wire `De` accepts decodes to a conforming value whose re-serialization is exactly the consumed prefix, so `De` accepts *nothing* `SerWire` cannot produce | `lemma DeCanonical` (+ `DeSeqCanonical`/`DeCountCanonical`), mirror induction of `RoundTrip`; `DeAcceptanceCharacterization` states the iff | **Unbounded** — with `RoundTrip` this pins `De` exactly: added leniency breaks `DeCanonical`, added strictness breaks `RoundTrip` |
| **Tolerant decoding** — DSDL implicit truncation + zero extension, at token granularity: exhausted wire reads as zeros, delimited sections consume exactly their declared length, present-but-wrong data still errors | `DeCompat`; `DeCompatConservative` — agrees with `De` on every wire `De` accepts; `DeCompatRoundTrip`; `ZeroExtension` + `ZeroValueConforms`; `DeCompatConforms`; concrete `CompatTolerates`/`CompatStillRejects` bounds | Conservative extension is **unbounded**; the tolerance shape is pinned by CI-enforced concrete checks |
| **Version-skew compatibility** — the extensibility contract: for `Evolves(tNew, tOld)` (field append at *delimited* boundaries only), a new reader decodes an old wire to `Upgrade(v)` (appended fields read as zeros) and an old reader decodes a new wire to `Downgrade(v)` (appended fields skipped via the delimiter) | `lemma OldWireNewReader` / `lemma NewWireOldReader` (+ seq/elems/section-body lemmas and zero-footprint support); `UpgradeConforms`/`DowngradeConforms`; `VersionSkewExample` is the concrete CI-enforced bookend | **Unbounded**, both directions; sealed layouts admit **no** append rule — adding one breaks the proofs (mutation-tested) |
| **Bounds safety** — `De` never reads past the buffer on any wire (truncated, empty, adversarial) | `De` is a **total** `function` with no precondition; Dafny rejects any unguarded token access | By construction |
| **Emit-order oracle** — the accepted serialize/deserialize op orderings | `SerOrderOK` / `DeOrderOK` predicates; `SerOps` / `DeOps` are the canonical traces | Predicates are the definition B1 applies to real backend traces; `CanonicalTracesOrderOK` proves the canonical traces satisfy them — **unbounded**, for *all* values (concat-closure lemmas + structural induction) |

This is the key upgrade over a bounded model checker: `RoundTrip`, `DeCanonical`, and
`CanonicalTracesOrderOK` are **proofs for all inputs**, and bounds-safety is not tested but
*guaranteed by the type system*.

### Negative control (the model has teeth)

Reordering the serialize union trace to mask-before-validate (the exact bug class this effort
targets) makes `dafny verify` fail inside `SerOpsOrderOK` — at the union head-block assert if
the lemma's block literal is reordered to match, or on the lemma's postcondition if only
`SerOps` is changed. Both variants are checked; the proofs are not vacuous.

The predicates are *ordering* constraints only: they say nothing about an op being present
(the empty trace satisfies both), and `dafny verify` cannot detect a predicate being
*weakened* — that direction is guarded by review of the predicate bodies, not by the proofs.

## Assurance boundary (read before citing it)

This proves the **abstract model**, *not* the emitted C++/Rust/Go/TS/Python code. The link
from model to code is **B1** — testing each backend's recorded op-trace against the orderings
here — not a refinement proof. Cite it precisely: *"the wire-format model is machine-checked;
the generators are checked against it by B1."* Do not call it "proven serialization." (The
project's stated top risk is claiming proofs the code doesn't deliver.)

`DeCanonical` is a statement about the **abstract token wire**: the model's wire grammar is
unambiguous — uniquely decodable and uniquely encodable. Its job is to pin the model's strict
`De` against leniency drift. Do **not** cite it as "decoders reject non-canonical input": real
DSDL decode is deliberately *more tolerant*, and that tolerance is modeled separately by
`DeCompat` (implicit truncation + zero extension). `DeCompat` deliberately has **no**
canonicity theorem — its acceptance is a strict superset of the canonical wires — so its
envelope is `DeCompatConservative`, `DeCompatConforms`, and the concrete `CompatTolerates` /
`CompatStillRejects` checks. Cross-*version* compatibility **is** proven at the model level
(`OldWireNewReader`/`NewWireOldReader`), but note its scope: `Evolves` covers **field append
at delimited boundaries** only — no union-option additions, no capacity or width changes.
Cite it as *"the model's evolution rules are machine-checked"*; the generated decoders are
still linked to the model only by B1 and the empirical harnesses.

## Run

```sh
dafny verify spec/dafny/CyphalSerdes.dfy
```

Expected: `Dafny program verifier finished with N verified, 0 errors`. Needs Dafny 4.x
(`brew install dafny`, or https://github.com/dafny-lang/dafny/releases). CI verifies it in the
`formal-model` lane on every change.

## Extend

- Add constructors/edge cases to the datatypes and extend the matches in `RoundTrip`,
  `DeCanonical`, `WFTyp`/`ZeroValue`/`DeCompat` (+ its lemma family),
  `Evolves`/`Upgrade`/`Downgrade` (+ the skew lemmas), and `SerOpsOrderOK`/`DeOpsOrderOK` —
  the proofs are by induction, so each new case is local: show the new head block satisfies
  the predicate, then stitch with the concat lemma.
- Ordering differences that are **accepted** (e.g. Rust's optional fixed-array `LEN_CHECK`,
  absent in Go/C++ because their fixed arrays are compile-time-sized — the D2/D3 entries in the
  prose spec) are modeled by making the op optional in the predicate, never silently ignored.

## Files

- `CyphalSerdes.dfy` — datatypes, `SerWire`/`De`/`DeCompat`, `SerOps`/`DeOps`, ordering
  predicates, and the round-trip, canonical-acceptance, tolerant-decode
  (truncation/zero-extension), version-skew (`Evolves`/`Upgrade`/`Downgrade`), and
  trace-ordering proofs.
