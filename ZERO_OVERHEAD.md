# Zero-overhead DSDL — development plan

Opened 2026-09-17 against `49b8883`, from the G6 re-audit in
[docs/development/roadmap.md](docs/development/roadmap.md). That audit found
`dsdl-annotate-aliasability` stamping a verdict about the wire onto an API that needs a verdict
about host memory, wrong for 9 of the 63 sections it accepts. This plan replaces the feature.

Every number here was measured by generating and running code from the embedded `uavcan`
catalogue; the reproduction steps are at the end.

## Three properties, named

One word covered three different things, which is how the verdict and its consumer drifted apart.
The plan uses these names throughout and the generated surface should carry them too.

| | Property | Decided from | Target-dependent |
|---|---|---|---|
| **W** | wire-flat — the serialised form is a contiguous byte image with no bit-level packing | the schema | no |
| **H** | host-image — the generated natural struct is byte-identical to that wire image | the schema plus an alignment model | yes; confirmed by the target's own compiler |
| **V** | view-representable — a packed type can be emitted whose layout is the wire image | implied by W | no |

`H ⊂ W`, and `V ≡ W` once odd scalar widths are reachable through accessors.

Over the catalogue's 181 plan sections:

| Set | Sections |
|---|---:|
| today's `ZOH_ALIAS_ELIGIBLE` | 63, of which **9 are wrong** on arm64 |
| **H** — storage width plus natural alignment, recursive | **54** — matches the measured ground truth exactly, no false positives or negatives |
| **W** — fixed, sealed, non-union, byte-aligned, byte-multiple, recursive; wire padding allowed | **107** |
| — representable with direct scalar widths (8/16/32/64 int, 32/64 float) | 56 |
| — needing an odd-width or `float16` accessor | 51 |

## What ships

1. **`@aliasable`** asserts **W**. `dsdlc` diagnoses at analysis time, naming the field that broke
   it.
2. **Generated static assertions** confirm **H** on the actual target, so the author's performance
   claim fails the consumer's build rather than degrading silently.
3. **Bulk-copy bodies** for **H** types: serialise and deserialise collapse to one copy.
4. **A packed wire-view type** for **W** types: field access with no decode, at any nesting depth.
5. **`--aliasable-only`**: emit the view and neither the object type nor the serdes.

## Decisions taken

- **`@aliasable` asserts W, not H.** A directive is a contract on the schema, and H is not
  decidable from a schema — it is a fact about the consumer's ABI. Splitting the claim across the
  directive (W, at analysis time) and the static assertion (H, at the consumer's compile time)
  covers the whole guarantee without `dsdlc` modelling anyone's ABI.
- **A failed `@aliasable` is an error.** `@assert` failing is an error and this is the same kind of
  statement. A warning that has to be escalated with `-Werror` invites the silent slowdown the
  directive exists to prevent. The relief valve, if one is wanted, is `--allow-non-aliasable`.
- **`@aliasable` requires `@sealed` rather than implying it.** Implicit sealing would change the
  wire format of a type from a directive that reads as an assertion.
- **`@aliasable` scopes to the section.** For a service, the directive before `---` constrains the
  request, matching `@sealed` and `@extent`.
- **No `--no-serdes-when-aliasable`.** Aliasable does not mean serdes-free: something still has to
  get from buffer to object, and under phase 3 that is already one copy. Omitting the entry point
  drops implicit zero extension onto callers who will not know to implement it, and makes
  `T__deserialize_` conditionally absent for every generic consumer — the transports, the parity
  harnesses and the five decoder-fuzz lanes. `--aliasable-only` (phase 5) delivers the intent as a
  whole-invocation mode instead.
- **`try_deserialize_view_` and `try_serialize_view_` retire** in phase 4. The first returns the
  pointer it was given, the second copies; the view type replaces both.
- **A view requires the whole payload; `deserialize_` does not.** DSDL's implicit zero extension
  makes a short buffer a valid encoding, and `deserialize_` honours it. A view cannot: the missing
  bytes would have to be fabricated somewhere, and anywhere is a copy. So the view path requires
  the full payload and says so, `deserialize_` keeps the spec's tolerance, and a caller that needs
  the tolerance uses `deserialize_`. Phase 4's accessors inherit this.

## Decisions needed

These block the phases named against them.

- **Rename the generated surface.** ✅ Decided: `WIRE_FLAT` and `HOST_IMAGE`, with their reasons,
  in each language's local spelling. `ZOH_ALIAS_ELIGIBLE` named neither property.
- **pydsdl divergence.** ✅ Decided: ship as a documented llvm-dsdl extension, upstream later.
  Stated in `docs/reference/commands/dsdlc.md`; the differential corpus is the public regulated
  submodule, so it cannot contain the directive.
- **Unions (affects phase 4 scope).** A union's wire is a tag plus the selected option, so it is
  not flat and is excluded here. Whether a tagged view type is worth a later phase is open.

## Phases

Phase 0 is independent. Phases 3 and 4 both depend on 1 and are independent of each other.

### Phase 0 — make the existing verdict legible — done 2026-09-17

The reason strings become user-facing in phase 2, so they had to be right first. The verdict set is
unchanged — 63 sections before and after — because this corrects the diagnosis, not the predicate.

- The annotator tests composites and variable arrays before the width tests, and answers for a union
  before walking its steps. A composite carries `bit_length = 0`, so the old `bitLength <= 0` test
  ran first and reported `invalid-bit-length` for every nested composite: 74 of the 118 ineligible
  verdicts, under a name that reads as a compiler defect for a well-formed type. The
  `composite-field` and `union-type` branches were unreachable. Reason counts over the catalogue:

  | Reason | Before | After |
  |---|---:|---:|
  | `invalid-bit-length` | 74 | — |
  | `composite-field` | — | 69 |
  | `union-type` | — | 6 |
  | `variable-array` | 20 | 21 |
  | `sub-byte-field` | 17 | 15 |
  | `empty-layout` | 4 | 4 |
  | `not-sealed` | 3 | 3 |
  | eligible | 63 | 63 |

- The cursor advances by a fixed array's whole length rather than by one element. The residual was
  always a multiple of eight for a field that had passed the byte-multiple check, so no verdict
  moves today and no test can fail on the old code through the verdict; phase 1 makes the absolute
  offset load-bearing, which is when it would have.
- A message emits one `DSDL_ZOH_ALIAS_*` pair; a service emits a request pair and a response pair.
  The service's base-name alias no longer states a verdict at all — C, C++ and Go gave it the
  request's, and the two payloads have independent layouts. Both payloads already state their own
  under their own names.

**Landed in** `lib/Transforms/Passes.cpp`, `lib/CodeGen/emitter/{Ts,Python,Cpp,Go,CHeaderRender}.cpp`,
`test/lit/dsdl-annotate-aliasability.mlir` (one schema per reachable verdict),
`test/lit/codegen-all-languages.txt`, `test/lit/golden/ts_python_snapshots/`,
`test/unit/CHeaderRenderTests.cpp`.

### Phase 1 — one predicate, two consumers — done 2026-09-17

W is decided once, in the analyser, because phase 2 needs source locations and a transform pass has
none. The MLIR attributes carry it to every backend and the pass verifies rather than recomputes.

- `lib/Semantics/AliasLayout.cpp` decides both verdicts on the semantic model, recursing into
  composite fields, and records each on `SemanticSection` with the field the reason is about.
- H reads `scalarStorageBits()`, which moved to `lib/Support/ScalarStorage.cpp`: how wide a
  generated type holds a scalar is needed below codegen. No target triple is consulted.
- `dsdl-verify-alias-layout` re-derives from the steps what they can decide. A plan stating no
  verdict is unverified rather than an error, so hand-written IR stays a valid `dsdl-opt` input;
  what `dsdlc` emits always states one.
- The generated surface is `WIRE_FLAT`/`HOST_IMAGE` and their reasons, in each language's local
  spelling. `try_deserialize_view_` and `try_serialize_view_` are gone.

**Measured** W = 107 and H = 54 of 181, matching what sized this plan.

**H has no false positives.** `llvmdsdl-alias-layout-reality` deserialises a deterministic buffer
into all 181 sections and compares each structure's bytes with the wire's: 177 agree, and the four
it disputes are delimited types, refused on purpose because a delimited payload may be longer or
shorter than this version's layout while the check only ever presents its own length. A refusal for
any other reason fails the gate, as does any claim the compiler contradicts.

**A reason names a field.** `not-fixed-size` no longer appears in the catalogue: a varying length is
a consequence, and the field walk names the field it comes from. Padding moves the cursor rather
than refusing, because padding has no name and a refusal that can name nothing is the defect this
work exists to remove — the field it displaces is named instead.

**Gates** `llvmdsdl-alias-layout-census` (the counts), `llvmdsdl-alias-layout-reality` (the
compiler's answer), `AliasLayoutTests.cpp` (one case per reason, including the recursion and the
two byte-image failures), `dsdl-verify-alias-layout-disagreement.mlir` (a plan claiming a verdict
its steps contradict). Both integration gates were confirmed to fail when their premise is broken.

### Phase 2 — `@aliasable` — done 2026-09-17

`@aliasable` asserts **W**, so a schema change that costs a performance-critical type its layout
fails at the schema rather than quietly costing its reader a decode.

- `DirectiveKind::Aliasable` parses as a flag, like `@union`; an expression or a repeat is an error.
- The check runs after the verdicts are decided, so it reads one answer rather than a second
  implementation, and reports through the analyser's diagnostics — which is why `dsdld` shows it
  without knowing the directive exists.
- A refusal names the field: *"field 'tail' is a variable-length array, so the fields after it
  move"*, not *"not aliasable"*.
- It scopes to a section, so a service asserts for each of its two payloads and a refusal says
  which. It requires `@sealed` rather than implying it, and the `not-sealed` reason says to add it.

**Decided:** ship as a documented llvm-dsdl extension, upstream later. Unknown directives are a hard
error in both implementations, so a namespace using `@aliasable` does not parse under pydsdl or
generate under Nunavut. The differential corpus is the public regulated submodule and cannot contain
the directive, so the exclusion is structural rather than a filter to maintain.
`docs/reference/commands/dsdlc.md` states the extension and says a portable namespace should not use
it.

**Gates** `aliasable-directive.txt`: the accepted case is a record of two byte-clean records, which
only recursion into composites makes possible; a case per refusal reason asserting the field named;
the two malformed spellings; a service whose response alone fails; and the verdict reaching the
generated constants.

### Phase 3 — bulk-copy bodies for H — M

**Depends on** 1.

- In `lib/Transforms/BuildDSDLPlanBodies.cpp`, an H section's serialise and deserialise bodies
  become one bulk copy. It goes in the plan bodies, not an emitter, so all six backends inherit it.
  `dsdl_runtime_copy_bits` already degenerates to `memmove` for a byte-aligned byte-multiple run
  (`runtime/dsdl_runtime.h:154`), so C needs no new runtime; whether to add a `dsdl.bytes_copy` op or
  reuse `dsdl.bit_write` with a `8 × N` width is an implementation choice inside this phase.
- Generated headers carry `_Static_assert(sizeof(T) == WIRE)` and an `offsetof` per field as the ABI
  backstop. It should never fire.
- Rust needs `#[repr(C)]` on the struct before it has a layout to assert. C++ adds
  `static_assert(std::is_standard_layout_v<T>)`.
- The copy cannot leak padding on the serialise side, because "no padding" is the H condition.

**Files** `lib/Transforms/BuildDSDLPlanBodies.cpp`, `lib/CodeGen/emitter/{C,Cpp,Rust,Go}.cpp`,
`include/llvmdsdl/IR/DSDLOps.td`.

**Acceptance** `llvmdsdl-serdes-instruction-comparison` shows an H type's `deserialize_` at a bulk
copy's instruction count; the backend-contract lane still fails a backend that does not follow a
perturbed body; every existing parity, determinism and decoder-fuzz lane stays green, including
implicit zero extension on a short buffer, which the copy path must preserve.

### Phase 4 — packed wire-view type for W — L

**Depends on** 1. Splits in two; 4a is the useful half.

**4a — direct widths (56 sections).** Per W section emit a second type whose layout is the wire
image: packed to alignment 1, wire padding as reserved members, nested composites as nested view
types, fixed arrays as arrays of those. Reads go through per-field accessors that copy from
`buffer + offset` into a local of the right width — one load each, defined in every language, and no
cast.

The cast is what this avoids, and the reason is worth keeping in the code: `(const T*)buffer` is not
portably expressible. The buffer is a `uint8_t*` of unknown alignment, so the cast is undefined and
faults on strict-alignment targets; and no object of type `T` exists in those bytes, which needs
`std::start_lifetime_as` (C++23) in C++ and violates strict aliasing in C.

**4b — odd widths and `float16` (the remaining 51).** A `uint56` member becomes seven bytes with an
accessor that widens; `float16` becomes two bytes with a converting accessor.

`try_deserialize_view_` and `try_serialize_view_` are removed in 4a. New generated names register in
the reserved list in `lib/Support/NamingPolicy.cpp:357`.

**Files** `lib/CodeGen/emitter/*.cpp`, `lib/Support/NamingPolicy.cpp`,
`lib/CodeGen/{TypeMetadata,SchemaLookup}.cpp`, `docs/reference/codegen/`.

**Acceptance** For every W section, the view type's size equals the wire size and each accessor
returns what `deserialize_` puts in the corresponding field, checked across C, C++, Rust and Go on
the real corpus; an accessor costs one load in the instruction lane; the removed functions are gone
from every backend's output.

### Phase 5 — `--aliasable-only` — S

**Depends on** 4.

Emit the view type and its accessors, and neither the object type nor the serdes. The mode requires
every targeted section to be `@aliasable` and fails naming the first that is not. This is the
line-count and dead-path reduction, delivered as a build mode rather than as a per-type hole in the
API.

**Acceptance** The mode's output compiles standalone in each language; a targeted non-`@aliasable`
type fails with a diagnostic naming it; `--list-outputs` reports the reduced set.

## Gates this adds

| Gate | Phase | What fails it |
|---|---|---|
| catalogue W/H census | 1 | either count moving without the test moving with it |
| predicate-vs-reality | 1 | an H section whose compiled struct is not a byte image |
| `@aliasable` diagnostics | 2 | a failure reason without a field name and location |
| bulk-copy instruction count | 3 | an H `deserialize_` above a bulk copy's count |
| zero-extension preservation | 3 | the copy path rejecting a short buffer |
| view accessor equivalence | 4 | an accessor disagreeing with `deserialize_` |

## Risks

- **The census numbers are corpus-specific.** 54 and 107 describe the regulated namespace, which was
  designed for CAN 2.0 wire economy and is therefore a pessimistic sample — `uint2` health codes and
  `uint56` timestamps are exactly the shapes that defeat W. A namespace written against `@aliasable`
  picks byte-clean widths deliberately. Treat the counts as regression anchors, not as the feature's
  value.
- **DSDL's alignment rule is weaker than C's**, so byte-clean does not imply host-clean under
  composition: a 7-byte composite followed by a `float32` is 11 bytes of wire and 12 of struct. H
  therefore shrinks as schemas nest, and phase 3 alone would be a feature that degrades with use.
  Phase 4 is what makes the work compose.
- **Phase 4 is the largest change to the generated API so far.** It lands before the alpha → beta-1
  boundary or it waits for it.

## Reproducing the measurements

```bash
dsdlc -l c -O out +uavcan && grep -rh '_ZOH_ALIAS_REASON_ "' out | sed 's/.*REASON_ //' | sort | uniq -c
```

The census, the H-versus-reality comparison and the W/V split were computed from `dsdlc -l mlir
+uavcan` and from compiling every eligible type's struct against its
`_SERIALIZATION_BUFFER_SIZE_BYTES_`. Phase 1 turns both into tests, at which point this section
becomes the tests' description rather than a manual procedure.
