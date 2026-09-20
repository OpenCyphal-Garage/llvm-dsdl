# Layout Guarantees

A DSDL type carries two layout properties. They answer different questions, they are decided in
different places, and the generated code reports both so a consumer can check either.

| | Property | Decided from | Target-dependent |
|---|---|---|---|
| **W** | wire-flat — the serialised form is a contiguous byte run at a fixed length | the schema | no |
| **H** | host-image — the generated structure is byte-identical to that run | the schema plus an alignment model | yes; confirmed by the target's own compiler |

`H ⊂ W`. W is what `@aliasable` asserts and what the field accessors serve; H is what the bulk copy
needs.

The two differ wherever the host holds a field wider than the wire carries it, or wherever a
structure aligns a field the wire does not. `uavcan.time.SynchronizedTimestamp` is seven bytes of
wire in an eight-byte structure, because a `uint56` is held in a `uint64_t`;
`uavcan.primitive.scalar.Real16` is two against four, because a `float16` is held in a `float`.

## What the generated code reports

Every type reports `WIRE_FLAT` and a `WIRE_FLAT_REASON` naming what blocked it, whether or not the
type asserts the property. C, C++, Rust and Go report `HOST_IMAGE` and `HOST_IMAGE_REASON` beside
them. A TypeScript or Python object has no byte image, so those two report the wire verdict alone;
the accessors are what W buys them.

A reason names the field it is about — `field 'tail' is a variable-length array, so the fields after
it move` — and a nested refusal carries the nested type's own name, reason and field, so an author
is not sent to another file to find out why.

## What each property buys

**H — the bodies become one move.** A host-image type's `serialize_` and `deserialize_` collapse to
a single move of the object's bytes on C, the object target, C++, Rust and Go. Over the embedded
catalogue this took `uavcan.si.unit.angle.Quaternion`'s read from 84 instructions to 35. A type that
is not a host image keeps the field-wise body.

**W — the fields are readable without a decode.** A wire-flat type's scalar fields, and the elements
of its fixed arrays, have a getter and a setter beside the serialisation functions in all six
languages: one read or one write at the field's fixed offset. A nested composite has a getter
answering its bytes, so accessors compose to any depth. A getter answers what `deserialize_` puts in
the field, on a short buffer too, where both zero-extend.

`--aliasable-only` emits the accessors and neither the object type nor the serialisation.
`--aliasable-views` holds a composite field of an `@aliasable` type as a view of the buffer the
holder was deserialised from, so the holder's deserialise skips the leaf.

`dsdlc --help` and [dsdlc](../commands/dsdlc.md) carry the directive and the two modes in full.

## The census

Over the embedded `uavcan` catalogue's 181 sections:

| Verdict | Sections |
|---|---:|
| `flat` — W holds | 107 |
| `variable-array` | 26 |
| `sub-byte-field` | 16 |
| `nested-not-flat` | 15 |
| `union-type` | 6 |
| `empty-layout` | 6 |
| `not-sealed` | 5 |

H holds for 54 of the 107.

These counts describe the regulated namespace, which was designed for CAN 2.0 wire economy: `uint2`
health codes and `uint56` timestamps are the shapes that defeat W. A namespace written against
`@aliasable` picks byte-clean widths deliberately. Read the counts as regression anchors rather than
as a measure of how often the properties hold.

## What the guarantees rest on

**H is confirmed by the consumer's compiler.** `dsdlc` models no ABI. Every host-image structure
carries a static assertion of its size and of each member's offset, emitted in the target's own
spelling, so a structure that is not the image fails the consumer's build rather than serialising
wrongly. The offsets come from the verdict's own walk, so the assertion and the verdict cannot
drift apart.

**The moved bodies are a little-endian path.** Moving an object's bytes on a big-endian host
produces big-endian bytes, and the swap back is per scalar, which needs the layout. A folded type's
generated header therefore refuses a build it cannot place as little-endian: it refuses a host it
reads as big-endian, and refuses one whose compiler states no byte order. Regenerating with
`--target-triple` naming that target emits the field-wise body instead. Go states the same refusal
as a build constraint. The accessors carry no such condition, and neither does the field-wise body.

**Overlap is permitted.** A view points into the buffer its holder was deserialised from, and a host
image is the wire's bytes, so an object and a wire buffer may share storage. The runtime moves such
bytes rather than copying them; `runtime/dsdl_runtime.h` states the contract and its one limit.

**Composition is what makes W worth asserting.** DSDL aligns a composite field to a byte boundary
but reserves no padding beyond it, so byte-clean does not imply host-clean under nesting: a 7-byte
composite followed by a `float32` is 11 bytes of wire and 12 of structure. H therefore shrinks as
schemas nest, and the accessors, which need only W, are what keeps the property useful there.

## The gates

| Gate | Fails on |
|---|---|
| `llvmdsdl-alias-layout-census` | either count moving without the test moving with it |
| `llvmdsdl-alias-layout-reality` | a host-image section whose compiled structure is not a byte image |
| `dsdl-verify-alias-layout` | a verdict the lowered steps contradict |
| `llvmdsdl-accessor-equivalence` | an accessor disagreeing with `deserialize_`, in any of the six languages |
| `llvmdsdl-aliasable-only` | the mode's output failing to compile alone on any target |
| `llvmdsdl-container-views` | a view not pointing into the buffer, or a short or empty view mishandled |
| `llvmdsdl-union-accessors` | a tag or option read disagreeing with the decode |
| `llvmdsdl-host-image-instruction-counts` | an entry point's or an accessor's instruction count moving without its baseline |

## Reproducing the numbers

The census and the comparison against the compiler both print what they measured:

```bash
ctest --test-dir <build> -L alias-layout -V
```

The instruction counts are read off the emitted objects for two pinned triples:

```bash
ctest --test-dir <build> -R host-image-instruction-counts -V
```

To read the verdicts of a namespace directly:

```bash
dsdlc -l c -O out +uavcan && grep -rh '_WIRE_FLAT_REASON_ "' out | sed 's/.*REASON_ //' | sort | uniq -c
```
