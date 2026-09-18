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
| **W** | wire-flat — the serialised form is a contiguous byte run at a fixed length | the schema | no |
| **H** | host-image — the generated structure is byte-identical to that run | the schema plus an alignment model | yes; confirmed by the target's own compiler |

`H ⊂ W`. W is what `@aliasable` asserts and what the accessors serve; H is what the bulk copy needs.

*(A third property, "a packed type can be emitted whose layout is the wire image", was dropped on
2026-09-18: the accessors do that work without the type. See **Decisions taken**.)*

Over the catalogue's 181 plan sections:

| Set | Sections |
|---|---:|
| the flag this replaced | 63, of which **9 were wrong** on arm64 |
| **H** — storage width plus natural alignment, recursive | **54** — matches the measured ground truth exactly, no false positives or negatives |
| **W** — sealed, non-union, fixed, byte-aligned, byte-multiple, recursive; wire padding allowed | **107** |

Phase 2.1 admits byte-multiple array totals into W, so that 107 rises; the census gate carries the
number and moves with it.

## What ships

1. **`@aliasable`** asserts **W**. `dsdlc` diagnoses at analysis time, naming the field that broke
   it.
2. **A candidate lint** says a delimited type *would* be aliasable once sealed, or names the field
   that would still block it — while the field is cheap to change rather than at lockdown.
3. **Generated static assertions** confirm **H** on the actual target, so the author's performance
   claim fails the consumer's build rather than degrading silently.
4. **Bulk-copy bodies** for **H** types, on the native backends: serialise and deserialise collapse
   to one copy.
5. **Field accessors** for **W** types, in all six languages: read a field off the wire with no
   decode, at any nesting depth.
6. **`--aliasable-only`**: emit the accessors and neither the object type nor the serdes.

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
- **`try_deserialize_view_` and `try_serialize_view_` are gone**, removed in phase 1. The first
  returned the pointer it was given and the second copied.
- **Decode-free reads are generated functions, not a second type.** *(Revised 2026-09-18.)* An
  accessor is one `dsdl.read_bits` at a constant offset: an op every backend already spells,
  including TypeScript's, which reads any width at any bit offset through the runtime. So the
  accessors are `func.func` bodies that `build-dsdl-plan-bodies` builds beside serialise and
  deserialise, and the existing translator spells them everywhere with no new ops. A packed struct
  would buy nothing the accessors do not and would cost a second declaration per section,
  `-Waddress-of-packed-member`, unaligned member access on strict targets, and a split around the
  widths it cannot hold natively.
- **The accessors are for every language, not only the native ones.** The lab side of this workflow
  is Python and TypeScript tooling reading frames a C node produced. A feature whose payoff stopped
  at the native backends would invert that.
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
- **Unions.** ✅ Decided: excluded from W, because a union's wire is a tag plus whichever option
  was selected. A union whose options are all wire-flat and of equal length is a fixed tag plus a
  fixed-offset option, which accessors can serve; that is phase 6, not a change to W.

## Phases

Phase 0 is independent. Phases 3 and 4 both depend on 1 and are independent of each other. 2.1 and
2.2 were added on 2026-09-18 from the implementation review; 2.2 lands before 4 so that authors can
design for the property rather than discover it at lockdown.

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

### Phase 2.1 — revisit what landed — done 2026-09-18

*(From the implementation review. Nothing committed was undone; each item is an addition or a
relaxation.)*

- **`HOST_IMAGE` is gone from TypeScript and Python.** An object there has no byte image, so whether
  a structure could be one said nothing. `WIRE_FLAT` stays, and phase 4's accessors make it mean
  something in both.
- **The verifier re-derives `host_image`**, not only its implication. It indexes the module's message
  plans so a nested composite's extent comes from its own steps, and reports a step the claim
  contradicts: wire padding, a width the host would widen, or alignment the structure would insert.
  A type the module does not carry is left to the analysis, which had the whole model.
- **A field blocker outranks sealing.** An unsealed type with a `uint2` field reported only
  `not-sealed`, so the field surfaced on a second round trip after sealing — and the field is the
  part that is hard to change. The `@aliasable` check names the field and mentions sealing alongside
  it. Over the catalogue this moved 23 sections off `not-sealed` and onto the field that also blocks
  them: 30 → 7.
- **A nested refusal chains.** The verdict carries the nested type's name, reason and field, so the
  check emits *"vendor.Narrow.1.0: field 'value' is not a whole number of bytes wide"* as a note
  rather than sending the author to another file.
- **A fixed array's run is what has to land on a byte boundary**, not each element, so `bool[8]` is
  wire-flat and `bool[4]` is not. H keeps refusing them, because a 1-bit element's storage width is
  eight. The catalogue's counts did not move — its whole-byte bool arrays are all in delimited types
  — so the case is covered by unit tests rather than by the census.
- **The orphaned comment is gone.** It described `dsdl-legalize-endianness`, which is not in the
  tree, and the view helpers, which phase 1 removed.
- **The verdict caches key on the section's address** rather than a tuple holding a string.

**Landed in** `lib/Semantics/AliasLayout.cpp`, `lib/Semantics/Analyzer.cpp`,
`lib/Transforms/Passes.cpp`, `lib/CodeGen/emitter/{Ts,Python}.cpp`, with cases in
`AliasLayoutTests.cpp` (19), `aliasable-directive.txt` and the snapshots.

**The verifier earned its place during this phase:** relaxing the array rule in the analyser without
relaxing it in the pass made every `bool[8]` type fail the build with *"wire_flat holds but this
field is not a whole number of bytes"*. That is the drift the pass exists to catch, caught on the
first run.

### Phase 2.2 — the aliasable-candidate lint — done 2026-09-18

An author chooses a `uint2` health code or a `void4` early and would learn that it blocks the fast
path only when the type is sealed, which is when it is most expensive to change. The verdict is
known before then: for a delimited type it is the layout walk with the sealing test skipped, which
is what `wire_flat` already answers.

- **`layout.aliasable_candidate`** in `dsdld`, at `Info` severity, so it reads as a hint in the
  editor and can be disabled like any rule. `LintDocument` now carries the analysed definition:
  analysis runs before the rules, and a rule that asks about layout needs the resolved model rather
  than the AST.
- **`dsdlc --warn-aliasable-candidates`** reports the same for a batch or a CI audit. Off by
  default, because it answers a question about a type's future rather than about the code being
  generated. It reads the local module rather than the merged one: advice about a definition is only
  worth giving to someone who can edit it.
- Two shapes are reported — *"would be @aliasable once sealed: its fields are already a contiguous
  byte run"*, and *"would be @aliasable once sealed, except that field 'health' is not a whole
  number of bytes wide"*.
- It stays quiet where the blocker is a design decision rather than an oversight: a variable-length
  array, a union, an empty type, and a nested type whose problem belongs to its own file. It says
  nothing about a sealed type, which has already made its choice.

**Noise, measured before building it.** Of the catalogue's 181 sections, 30 are delimited, and the
rule fires on 12 of them: 7 that sealing alone would qualify, and 5 held up by one narrow field. The
other 18 are variable-length arrays, unions and nested problems, and are silent.

**Landed in** `lib/LSP/Lint.cpp`, `include/llvmdsdl/LSP/Lint.h`, `lib/LSP/Analysis.cpp`,
`tools/dsdlc/main.cpp`, with cases in `LspLintTests.cpp` and `aliasable-candidate.txt`, and the rule
documented in `docs/reference/lsp/lint-rules.md`.

### Phase 3 — bulk-copy bodies for H — M

**Depends on** 1. Native backends only.

**The plan's first idea does not work, and the review caught it.** Reusing `dsdl.bit_write` with an
`8 × N` width is spellable in C, where it lowers to `dsdl_runtime_copy_bits` and becomes a `memmove`.
In Rust, Go and TypeScript that op is a per-bit loop over a *bool container*
(`Rust.cpp:943`, `Go.cpp:1101`, `Ts.cpp:1020`): it means "move a run of bools", and its source cannot
be an object. So the bulk copy needs its own op.

- **`dsdl.image_copy`**, with a spelling per backend: `memcpy` in C and C++,
  `core::slice::from_raw_parts` under `unsafe` in Rust, `unsafe.Slice` in Go. Rust's is sound only
  with `#[repr(C)]` and the H verdict, which makes the `repr(C)` item a prerequisite rather than a
  nicety. TypeScript and Python cannot spell it, and do not need to: their objects have no byte
  image, and phase 4's accessors are their fast path.
- **A target capability, not a backend branch.** `build-dsdl-plan-bodies` always emits the canonical
  field-wise body; a rewrite pass replaces an H section's bodies with `image_copy` when the pipeline
  was told the target's objects are byte images. The driver sets one boolean per language. That
  keeps one pipeline and keeps bodies-as-IR, which is the rule this feature must not bend.
- **The fold is its own stage.** `addLowerDSDLBodiesPipeline` runs the optimise stage only when
  `optimizeLoweredSerDes` is set (`Passes.cpp:1362`). Putting the fold there would make the fast path
  conditional on an unrelated flag, so it runs under the capability instead.
- **A `__BYTE_ORDER__` guard.** A whole-object copy on a big-endian host produces big-endian bytes,
  and turning those into the wire's little-endian form is a swap *per scalar*, which needs the
  layout — a transport cannot do it to an opaque buffer. So `image_copy` is correct where the host
  is little-endian, and the generated header says so and falls back to the field-wise body
  elsewhere. This qualifies the roadmap's claim that `serialize_` is host-endianness-agnostic: it
  stays true of the field-wise body, which is what a big-endian host keeps.
- **No saturation, and the reason is the argument that the copy is complete rather than merely
  fast:** an H field's storage width equals its wire width, so every value the structure can hold is
  representable on the wire. Nothing needs clamping. For the same reason the copy cannot leak
  padding, because "no padding" is the H condition.
- Generated headers carry `_Static_assert(sizeof(T) == WIRE)` and an `offsetof` per field as the ABI
  backstop. It should never fire. C++ adds `static_assert(std::is_standard_layout_v<T>)`.

**Files** `lib/Transforms/BuildDSDLPlanBodies.cpp`, `lib/Transforms/Passes.cpp`,
`include/llvmdsdl/IR/DSDLOps.td`, `lib/CodeGen/emitter/{C,Cpp,Rust,Go}.cpp`.

**Acceptance** `llvmdsdl-serdes-instruction-comparison` shows an H type's `deserialize_` at a bulk
copy's instruction count; the backend-contract lane still fails a backend that does not follow a
perturbed body; every parity, determinism and decoder-fuzz lane stays green, including implicit zero
extension on a short buffer, which the copy path must preserve.

### Phase 4 — field accessors for W — M

**Depends on** 1. *(Was L, and a packed type. Revised 2026-09-18 — see **Decisions taken**.)*

Per W section, `build-dsdl-plan-bodies` builds an accessor beside the serialise and deserialise
bodies: one `dsdl.read_bits` at a constant offset, which the existing translator spells in every
language with no new ops and no per-backend logic beyond the function's name.

- Nested composites return a `dsdl.buffer_at` slice, so `Vec3.x(Pose.position(buffer))` composes.
- Fixed arrays take an index, checked by `dsdl.index_holds`.
- Setters are `dsdl.write_bits` the same way.
- No width split: `read_bits` already reads a `uint56` and the float16 helper already widens, so the
  odd widths cost nothing extra.
- No endianness condition: `read_bits` reads little-endian bytes into host integers on every host,
  which makes this the path that holds where phase 3's does not.
- New generated names register in the reserved list at `lib/Support/NamingPolicy.cpp:357`.

**Acceptance** Every accessor returns what `deserialize_` puts in the corresponding field, across all
six languages, on the catalogue and on the `@aliasable` fixtures; an accessor costs one load in the
instruction lane.

### Phase 5 — `--aliasable-only` — S

**Depends on** 4.

Emit the accessors and neither the object type nor the serdes — a filter on which functions are
emitted, once phase 4 has made the accessors functions. The mode requires every targeted section to
be `@aliasable` and fails naming the first that is not.

**Acceptance** The mode's output compiles standalone in each language; a targeted non-`@aliasable`
type fails with a diagnostic naming it; `--list-outputs` reports the reduced set.

### Phase 6 — container views — later

*(Added 2026-09-18.)*

The production shape is a delimited container that keeps evolving around records that no longer do:
a system that has locked its hot inner records but still wants to change the message around them
writes exactly that. Phase 4 serves it as far as reading goes — decode the container, take the inner
record's bytes through `buffer_at`, read them with the inner type's accessors.

What is missing is the container's own structure *holding a view* of an aliasable field rather than a
decoded copy, so the container's deserialise skips the leaf entirely. That is what makes composition
zero-copy rather than the leaves alone, and it is the phase that makes the feature worth having on
the shape users will actually build.

Equal-length unions belong here too: a union whose options are all wire-flat and of the same length
has a flat wire form of a tag plus one option, and an accessor set can serve it by reading the tag
and then the option at a fixed offset.

## Gates this adds

| Gate | Phase | What fails it | State |
|---|---|---|---|
| catalogue W/H census | 1 | either count moving without the test moving with it | ✅ landed |
| predicate-vs-reality | 1 | an H section whose compiled struct is not a byte image | ✅ landed |
| `@aliasable` diagnostics | 2 | a failure reason without a field name and location | ✅ landed |
| verifier re-derives H | 2.1 | a `host_image` the steps contradict | ✅ landed |
| candidate lint | 2.2 | a delimited type that would qualify, reported without its field | ✅ landed |
| bulk-copy instruction count | 3 | an H `deserialize_` above a bulk copy's count | to build |
| zero-extension preservation | 3 | the copy path rejecting a short buffer | to build |
| accessor equivalence | 4 | an accessor disagreeing with `deserialize_`, in any of the six | to build |

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
- **The bulk copy is a little-endian-host path.** A whole-object copy on a big-endian host produces
  big-endian bytes, and the swap back is per scalar, so it needs the layout and a transport cannot
  do it to an opaque buffer. Phase 3 guards on `__BYTE_ORDER__` and falls back to the field-wise
  body; phase 4's accessors have no such condition. A type that asserts `@aliasable` has frozen its
  layout — that is the intent, and a version bump is how it changes.
- **Phase 4 adds a function per field to the generated API.** It lands before the alpha → beta-1
  boundary or it waits for it.

## Reproducing the measurements

The census and the comparison against the compiler are the two `alias-layout` tests, which is where
the numbers in this document come from:

```bash
ctest --test-dir <build> -L alias-layout -V
```

Both print their counts, and the reality lane names every section whose claim the compiler disputes.
To read the reasons directly instead:

```bash
dsdlc -l c -O out +uavcan && grep -rh '_WIRE_FLAT_REASON_ "' out | sed 's/.*REASON_ //' | sort | uniq -c
```
