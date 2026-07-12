# CyphalSerdes — formal control-flow / round-trip model

A TLA⁺ model of Cyphal DSDL serialize/deserialize as a **transition system over an
abstract wire**. It is the normative **oracle** for the emit-order verifier (B1): the
op-orderings proven here are what each generated backend (C/C++/Rust/Go/TS/Python) is
checked against. The prose [P2_canonical_emit_order.md](../../docs/plans/P2_canonical_emit_order.md)
is the human-readable projection of this module.

## Scope (deliberate)

Models the **sequence of wire operations** and the **structural invertibility** of
serialize/deserialize. The wire is an ordered stream of typed *tokens*; a scalar's value is
an opaque tag that must survive the round trip.

**Out of scope — verified empirically instead:** bit-exact byte layout, integer widths,
saturation/truncation, endianness, NaN payloads. Those are covered by the differential-parity
and cross-language round-trip harnesses. Modeling them here would add nothing the empirical
tests don't already give, at large cost.

## What is proven (over a bounded domain)

TLC exhaustively checks `Inv` over every value of every schema in `TestTypes` (bounds in
`CyphalSerdes.cfg`):

| Property | Statement |
|---|---|
| `AllRoundTrip` | `De(T, Ser(v).wire)` reconstructs `v` exactly and consumes every token. |
| `AllBoundsSafe` | `De` evaluates without reading past the buffer on **any** truncation, and accepts **iff** the buffer is complete (no false-accept of a truncated/adversarial stream). |
| `AllSerOrder` | serialize trace obeys: validate & mask **before** write-tag; len-validate **before** len-write; advance **immediately after** every write. |
| `AllDeOrder` | deserialize trace obeys: tag **read → masked → stored → validated**; length **read → advanced → validated**. |

`Ser(v).ops` and `DeOps(v)` **are** the emit-order oracle — the accepted op orderings B1
checks the backends against.

### Negative control (the model has teeth)

Swapping `VALIDATE_TAG` and `MASK_TAG` in the serialize union trace (i.e. mask-before-validate —
the exact bug class this effort targets) makes TLC report `Invariant Inv is violated`. The
check is not vacuous.

## Assurance boundary (read this before citing it)

This proves the **abstract transition system**, *not* the emitted C++/Rust/Go/TS/Python code.
The link from model to code is **B1** — testing each backend's recorded op-trace against the
orderings derived here — not a refinement proof. Cite it precisely: *"the wire-format
transition system is model-checked; the generators are checked against it by B1."* Do not call
it "proven serialization." (This discipline is the whole point: the project's stated top risk
is claiming proofs the code doesn't deliver.)

The model itself is validated against the DSDL wire rules and the existing differential-parity
results — a wrong model would be a wrong oracle.

## Run

```sh
./check.sh                       # uses vendored vendor/tla2tools.jar, runs TLC
TLA2TOOLS_JAR=/path/to/jar ./check.sh   # optional override
```

Expected tail: `Model checking completed. No error has been found.`

Needs only Java — `tla2tools.jar` is vendored in [`vendor/`](vendor/) (see its README for
version/provenance), so the check runs fully offline; the CI `formal-model` lane uses the
same in-tree jar. `TLA2TOOLS_JAR` overrides it if you prefer your own.

## Extend

- Widen coverage: raise `ScalarVals` / `MaxElems` in `CyphalSerdes.cfg`, add schemas to
  `TestTypes` in the module. It stays exhaustive over whatever bounded domain you set.
- Keep it honest: any op-order the model *accepts* is a member of the equivalence class B1
  tolerates; any it *rejects* is a genuine bug. Accepted-but-different cases (e.g. Rust's
  optional fixed-array `LEN_CHECK`, absent in Go/C++ because their fixed arrays are
  compile-time-sized) are the D2/D3 entries in the prose spec — model them as optional ops,
  never silently ignore them.

## Files

- `CyphalSerdes.tla` — the model (Ser/De/DeOps, ordering predicates, bounded value generation, properties).
- `CyphalSerdes.cfg` — TLC constants + `INVARIANT Inv`.
- `check.sh` — portable runner.
