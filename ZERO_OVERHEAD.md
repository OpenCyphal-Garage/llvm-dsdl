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
- **A whole-object view requires the whole payload; `deserialize_` does not.** *(Narrowed
  2026-09-19; it was written of any view. See phase 6.)* DSDL's implicit zero extension makes a
  short buffer a valid encoding, and `deserialize_` honours it. A view of the whole object cannot:
  the missing bytes would have to be fabricated somewhere, and anywhere is a copy. So that path
  requires the full payload and says so, `deserialize_` keeps the spec's tolerance, and a caller
  that needs the tolerance uses `deserialize_`. A field view is the case this does not cover: the
  nested type's accessors read one, and they zero-extend, so a container holds what the buffer has
  and serialises it zero-filled to the field's width.

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

### Phase 3 — bulk-copy bodies for H — done 2026-09-18

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

**Progress, 2026-09-18.** The ops, the capability and the fold pass are in; nothing is wired to a
backend yet, so no generated code has changed. Every host-image shape tried now folds in both
directions with no field work left beside the move: a scalar record, a record holding a fixed
array, and a record holding another byte-image record. A non-host-image body is left as it was.

Three things were learned building the pass, and each is worth more than the code.

*A release build hides a use-after-free until it is somewhere else.* The write fold's first attempt
erased a chain step while the guard after it still read that step's result. `Operation::erase()`
asserts the op has no uses — in a Debug build. This tree is RelWithDebInfo, where the assertion is
compiled out, so the corruption surfaced as a stack overflow in the verifier, far from the cause.
The fix was to redirect every step's result to the capacity check's, which is also the right
answer: once the fields are gone, every code in the chain *is* that one. Run a Debug `dsdl-opt`
against IR surgery before trusting a release one; it names the op on the spot.

*A count of moves passes a fold that did not fold.* The read side's first handling of a nested
record produced the right count of `image_read` ops and a body that still called the nested
deserialise beside it — correct output at twice the cost. The backward slice from the consumed-store
guard had walked through the nested call's error code into the call itself and kept it. The slice
now stops at `dsdl.call_serdes`: that code is what the fold replaces, not something the survivors
are built from. The lit test asserts the field work is *gone*, not that a move appeared.

*A decline can be a right answer.* The nested fixture first used — a byte-image record followed by
a `uint8` — is not a host image: nine bytes of wire, twelve of structure. The pass refused it at
the first gate, and an earlier report here listed that refusal as a shape gap. It was the verdict
doing its job.

**The C target folds, source and object, 2026-09-18.** The capability is set for `c` and `obj`
where the target triple — the host's when none is given — is little-endian. Measured on the
catalogue's objects, deserialise and serialise instruction counts before and after:

| Section | Read | Write |
|---|---:|---:|
| `uavcan.si.unit.angle.Quaternion` (16 bytes) | 84 → **35** | 22 → **16** |
| `uavcan.primitive.scalar.Natural64` (8 bytes) | 40 → 35 | 16 → 16 |
| `uavcan.node.Version` (2 bytes) | 33 → 35 | 18 → 16 |
| `uavcan.node.ExecuteCommand` (not a host image; the control) | 70 → 70 | 54 → 54 |

The floor of about 35 is the entry point's own work — null checks, the consumed count — which the
fold does not touch; the field work went from a call and a mask per field to a copy the backend
folds to loads and stores. A two-byte type is a wash, because the zero-extension branch costs what
two byte loads did.

Two more things were learned, and one decision taken.

*The plan named the wrong primitive.* It said the bulk copy needs no new runtime because
`dsdl_runtime_copy_bits` degenerates to a memmove for a byte-aligned run. It does — at run time,
through branches, and inlined those branches stay. Measured that way, Quaternion's write went from
22 instructions to 46 and Version's read from 33 to 49: worse than the field reads replaced. What
folds to loads and stores is a *constant-length* `memcpy`. So the object path emits `llvm.memcpy`
and `llvm.memset` intrinsics behind a branch on the buffer's length, and the source path calls two
`static inline` helpers, `dsdl_runtime_image_read` and `dsdl_runtime_image_write`, that do the same
in C. Measure before believing an argument about what a routine "degenerates to".

*There are two C lowerings, and the plan knew of one.* `-l c` emits C source through an EmitC
pipeline; `obj` lowers through hand-built LLVM IR. Every C-source-generating lane — 61 tests —
failed the moment the fold ran, because the EmitC lowering had no pattern for the two ops and their
operands could not be materialised. A spelling for C is two spellings.

*Refuse, rather than fall back, on a big-endian build of the source.* The plan's `__BYTE_ORDER__`
guard falling back to the field-wise body needs both bodies emitted, which the fold does not do.
Instead a folded type's header carries a guard that fails the build with the reason and the fix
(`--target-triple` naming the target). The object path needs none: the triple decided the fold.
Confirmed by compiling a folded header with the byte-order macro flipped. A type that is not a
host image but holds one refuses too, because its own body calls the folded one — the refusal
follows composition, which is what it must do.

**C++ folds too, 2026-09-18.** Its spelling calls the same two runtime helpers C does — generated
C++ already includes the C runtime and `<cstring>` — so the body is one line in each direction and
the byte-order guard is the same `#error`. The translator dispatches the two ops as statements
beside the bit moves, and `BodySpelling` gains them as pure virtuals: a backend the fold never
reaches — Rust and Go until their slices land, TypeScript and Python for good — spells them as a
fatal error naming the fact, so a fold running where it must not is loud rather than wrong. Every
C++ parity lane, including the pmr and autosar profiles, passes against C output that folds.

**Rust folds, 2026-09-18.** A host-image struct gets `#[repr(C)]` — and only such a struct: the
default representation may reorder a non-image type's fields to pack it, and that is worth keeping
there. `repr(C)` is the layout the verdict was decided under, fields in order at natural alignment.
The moves view the object as bytes through `core::slice::from_raw_parts` under `unsafe`, which is
sound because every field of a host image is an integer, a float, a fixed array of those or a
nested host image, so any bytes are a valid value. A folded type carries
`#[cfg(target_endian = "big")] compile_error!(…)`, proven to fire by flipping the condition in a
copy of the crate. Every Rust lane passes, the `no_std` profiles included.

Rust's compiler found the fold's one loose end. A folded body no longer calls the per-field helpers
built beside it, and Rust refuses an unused private function under warnings-as-errors, which the
deprecation compile gate builds with. Erasing them is not an option: the plan's steps still name
them and the lowered contract requires a named helper to exist — the C text path checks it — so
erasing broke every C-source lane instead. The fold marks an unreferenced helper, Rust skips a
marked one, and C and C++ carry theirs as `static inline`, which the compiler drops. Clearing the
steps' helper names would be the tidier IR, and it means changing a versioned contract; that is
noted for phase 4, which rebuilds the bodies anyway.

**Go folds, 2026-09-18.** The moves view the object as bytes through
`unsafe.Slice((*byte)(unsafe.Pointer(obj)), N)`, sound for the reason Rust's are, and a short
buffer is `clear` then `copy`. Go decides the architecture when the package is built, so the
endianness refusal is a build constraint rather than an `#error`: a second file beside the type's,
`<stem>_host_image.go`, under `//go:build !(386 || amd64 || …)`, whose body is a reference to an
undefined name spelling the reason. The list is the standard library's own, from
`encoding/binary`'s native order, and it is the little-endian list rather than its complement, so
an architecture on neither is refused rather than trusted. Proven by cross-building the fixtures:
`ppc64` and `s390x` fail naming the type, `386` and the host pass. A build constraint was chosen
over a check at `init`: the other three targets refuse at compile time, and a panic at process
start is a runtime failure with a compile-time cause. The Go generation lane counts one file per
type, and now counts past the guards.

**The static assertions land, 2026-09-18.** Every host-image structure now asserts, on the target
it is compiled for, that its size is the wire's and that each member sits at the offset natural
alignment gives it: `DSDL_RUNTIME_STATIC_ASSERT` in C — the header is included from C++
translation units, where `_Static_assert` is an extension — `static_assert` with
`std::is_standard_layout` in C++, `const _: () = assert!` over `size_of` and `offset_of!` in Rust,
and the `[1]struct{}{}[unsafe.Sizeof(T{})-N]` idiom in Go, where a mismatch is an index out of
bounds. The offsets come from the verdict's own walk, recorded on the section as it is decided,
so the assertion and the verdict cannot drift apart. They are emitted for every host image, folded
or not: the `HOST_IMAGE` constant makes the claim, and the assertion is what backs it. The empty
type gets none; its image is zero bytes, its structure is one in three of the four languages, and
the move copies nothing.

The assertions found a defect on their first run. Under the PMR profile every C++ structure
carried a `_memory_resource` pointer, so a host image was wider than the wire — and a host image
holding another had the nested pointer inside it, moving every field after it. The fold copies
from the structure's first byte, so on such a type it would have written the nested pointer's
bytes to the wire and never the field after it. Every parity lane had passed: the catalogue has no
nested host image, so no lane could reach the case. A host image allocates nothing, and under the
PMR profile it now carries no resource; the resource-taking constructor and `set_memory_resource`
stay as no-ops so a parent treats every member alike, and the free function takes the resource it
is handed rather than reading one from the object.

**The instruction-count gate, 2026-09-18.** `llvmdsdl-host-image-instruction-counts` generates
the fold fixtures as objects for two pinned triples, `aarch64-unknown-linux-gnu` and
`x86_64-unknown-linux-gnu`, disassembles each entry point with `llvm-objdump`, and holds its
instruction count to a baseline keyed by triple and LLVM major. A static count is a property of
the emitted code — exact, the same on every host that can target the triple, and readable
without a simulator — so this gates where the cachegrind lanes skip, Apple Silicon included; what
it does not see is how often an instruction runs. The non-image fixture is in the baseline as the
control, so a fold reaching a body it must not would move a number too. The baseline holds the
table above: 35 and 16 for the eight-byte record on AArch64, 56 and 29 for the field-wise
control. A key with no entry skips and prints the block to paste; a change to the lowering
re-baselines in the commit that makes it, and a wrong baseline was confirmed to fail the lane.

The acceptance named the cachegrind comparison, which cannot run on the machine this was built on;
the static count asks the same question of the object and runs everywhere. Short buffers are the
last acceptance item: the C/Go parity lane's directed truncated inputs on `node.Version`,
`scalar.Natural8` and `scalar.Integer64` already ran through folded bodies on both sides, and the
C/Rust and C++/C lanes now have the same on `scalar.Real32` and `scalar.Integer8`, checking that the
bytes past the input come back as zero. Phase 3 is complete.

### Phase 4 — field accessors for W — done 2026-09-18

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

**Decisions taken, 2026-09-18.** The shape of an accessor, settled before the first language:

- *A getter answers the value alone.* It takes the buffer and its size and returns the field's
  value: no error channel, because a read cannot fail — a short buffer zero-extends, which is what
  `deserialize_` does with it, so the two agree on every input `deserialize_` accepts. The one
  input it rejects, a null pointer with a size, is a C question; the C wrapper reads a null
  buffer as an empty one, and the other languages take a slice. A setter answers the runtime's
  error code, as `serialize_` does, and refuses a null buffer the same way.
- *One read, through the plan's own helper.* The getter's body is `dsdl.read_bits` at the field's
  offset followed by the deserialise helper the field-wise body uses, so a getter saturates,
  sign-extends and widens exactly as the body does; a setter is the serialise helper and one
  `dsdl.write_bits`. The offsets come from walking the plan's steps as the bodies do, once, in the
  pass.
- *Bodies with a member.* `llvmdsdl.plan_body` is `get` or `set` and `llvmdsdl.member` names the
  field; the symbol is `<stem>__get_<field>_ir_`. A backend that reads the member's plan step from
  those two attributes spells the accessor in the member's own type: `u16` in Rust, `uint16_t`
  in C, a `bool` for a bool. The plan holds every integer in an `i64`, so the spelling casts at
  the boundary and nowhere else.
- *Named `get_<field>` and `set_<field>`* in every language, so a field and its accessors never
  share a name where a language puts members and functions in one scope.

**Progress, 2026-09-18.** The bodies are built for every scalar field of a wire-flat section, with
a lit test pinning the offsets after a fixed array and after a nested record and the absence of
accessors on a section that is not wire-flat. C declares the lowered entry points and wraps each
in a `static inline` function speaking the member's type; the object target exports them. Rust
spells them as associated functions, `Type::get_b(&buffer)` and `Type::set_b(&mut buffer, v)`,
which the deprecation gate forced ahead of the other languages: the accessors call the per-field
helpers a folded body had left unreferenced, so the helpers stay emitted and something must call
them. Both were checked by hand against `deserialize_`, on full and on short buffers, before the
lane exists.

**All six languages spell them, 2026-09-18.** C++ gets a static member defined inside the
struct, `Padded::get_b(buffer, size)`, since an accessor reads the wire and not an object; Go a
package-level function named after the type, `PaddedGetB(buffer)`; TypeScript an exported function
in the bodies' own style, `getPaddedB(buffer)`; Python a static method, `Padded.get_b(buffer)`.
Each speaks the member's own type — a `number` where TypeScript holds the plan's integer in a
`bigint`, a `bool` where the plan holds a bit in an integer — with the conversion at the boundary
and nowhere else. Where a language puts a struct's members and its functions in one scope, C++
and Python, the accessor's name is claimed through the struct's own naming scope after every
field, so a field named `get_b` and the getter of `b` cannot collide; that is the reserved-name
registration the plan asked for, done where the names are made rather than in a static list.

The naming corpus refused the first C++ shape. It compiles under `-Wreserved-identifier`, and a
free function named `Type_get_<field>_` puts a double underscore into the identifier whenever the
field starts or ends with one — `break_`, `_memory_resource` — which C++ reserves. Collapsing the
run, as the helper bindings do, would merge `break` with `break_`. So the free function went, the
accessor is defined where it is named, and a field that already starts with an underscore joins
its `get` without one; `foo` and `_foo` then meet at `get_foo`, and the struct's scope keeps them
apart. The other languages join plainly: none reserves the form, and Rust has no scope to
uniquify in.

Python found the setter's gap. Its runtime raises on a write past the buffer rather than answering
a code — the serialise body never reaches one, because its capacity check refuses the buffer
first — so a setter that relied on `dsdl.write_bits` for its error raised where C returned a
code. The setter now checks the buffer holds the field before it writes, as the body checks it
holds the whole, and answers `SERIALIZATION_BUFFER_TOO_SMALL` the way the body would. One check
in the IR, and six languages agree on a short buffer. A probe per language — full buffer, short
buffer, setter round trip, null or empty buffer, and the nested record's trailing field — was run
by hand against each language's `deserialize`; the lane that holds them there is next.

**The equivalence lane, 2026-09-18.** `llvmdsdl-accessor-equivalence` holds every language to
the claim on seven regulated types — `node.Version`, `scalar.Integer16`, `scalar.Natural64`,
`scalar.Real16`, `scalar.Real64`, `temperature.Scalar`, `file.Error`: unsigned, signed and
64-bit integers, float16 widening, float32 and float64. Each driver fills a buffer with
pseudo-random bytes, so floats meet every pattern including the NaNs, and compares values as
bits: the getter against `deserialize_`'s field on the full buffer and on a half-length one,
where both zero-extend; the setter's bytes read back through `deserialize_` and the getter, and
exactly for an integer; and a setter refused an empty buffer. Six legs, forty-two verdicts.

**Fixed arrays, 2026-09-18.** An element accessor takes an index after the buffer — `get_head(buf,
i)`, `Arr::get_head(buf, size, i)`, `ArrGetHead(buf, i)` — and the body is the scalar's with the
offset computed: base plus index times width, checked against the capacity. The decision taken
for an index at or past the capacity: a getter reads it as zero, as it reads bytes past the
buffer, since it has no error channel and a defined answer is safer in C than a read past the
field; a setter refuses it as an invalid argument, as it refuses a null buffer. The lane gained
`si.unit.angle.Quaternion`'s `float32[4]`, checked element by element and one past the end, in
every language. A bool array is the same path at a width of one bit.

**Nested composites, 2026-09-18.** A nested field's getter answers the buffer from the field's
byte offset — `Nested::get_inner(&buf)` is a slice, and in C and C++ a pointer with what remains
through a size pointer — so the nested type's own accessors compose on it:
`Flat::get_b(Nested::get_inner(&buf))`. The offset is clamped to the size, so a short buffer yields
an empty one and every nested read zero-extends as `deserialize_` does. There is no setter: a
nested field is set through its own fields' setters on the buffer the getter answers. The one
lowering change was in the C text path, which had never lowered a function that returns a DSDL
pointer; the object path already did. The lane gained `si.sample.temperature.Scalar`, a
`SynchronizedTimestamp` followed by a `float32`, read through the composite getter on the full
buffer and on one cut inside the nested field.

**The instruction-count row, 2026-09-18.** The accessors are baselined in
`llvmdsdl-host-image-instruction-counts` beside the bodies, on both pinned triples: a scalar
getter, its setter, an element getter and a composite getter. The numbers say what the plan's
acceptance asked and did not get: on AArch64 a getter is 17 instructions, the composite getter 6,
and the element getter 15. A getter is not one load. It is the runtime's read primitive — a call,
with the clamps of a read that may straddle the buffer's end — followed by the field's
normalising helper, because `dsdl.read_bits` lowers the same way whatever its offset and width.
The one-load form is a fast path in the lowering for a read at a constant byte-aligned offset of
a standard width on a little-endian target: a bounds check and a plain load, the primitive kept
for the short-buffer case. It belongs in the lowering, where every body with a byte-aligned field
inherits it, and it needs the target's endianness there as the fold needed it.

**The fast path, 2026-09-18.** `convert-dsdl-to-llvm` takes the target's endianness beside its
`size_t` width, and on a little-endian target lowers a `read_bits` or `write_bits` at a constant
byte-aligned offset of a register's width to a bounds check and one unaligned load or store, the
primitive kept for a buffer short of the field. The object target sets it from the triple, as the
fold is. `Padded.get_b`'s hot path is now `cmp; b.lo; ldurh; ret`; the whole function is 16
instructions on AArch64, down from 25, the rest being the null-buffer substitution and the short
case, which the compiler inlined from the primitive. The bodies inherited it as the plan said:
`Padded`'s field-wise deserialise fell from 56 to 39 on AArch64 and from 61 to 44 on x86-64. A
getter of a folded type moved the other way by two instructions, the short case having become
inline; a whole-function count includes its cold path, which is the honest unit the lane has.
The C text path keeps calling the `static inline` primitive, which the C compiler inlines on its
own; that path's counts are the cachegrind comparison's to keep. Phase 4 is complete.

### Phase 5 — `--aliasable-only` — done 2026-09-18

**Depends on** 4.

Emit the accessors and neither the object type nor the serdes — a filter on which functions are
emitted, once phase 4 has made the accessors functions. The mode requires every targeted section to
be `@aliasable` and fails naming each that is not.

**Acceptance** The mode's output compiles standalone in each language; a targeted non-`@aliasable`
type fails with a diagnostic naming it; `--list-outputs` reports the reduced set.

**Decisions taken, 2026-09-18.**

- *A nested type needs no directive.* An `@aliasable` type's composite getter answers the nested
  record's bytes for the nested type's accessors to read, so the nested type is in the run by the
  outer type's assertion, and it is wire-flat by the directive's own definition. A type that is
  neither asserted nor nested is refused; every such type is named in one run, and a type that is
  not wire-flat is named with the field that blocked it.
- *A filter in the pipeline, not in the emitters.* `dsdl-keep-accessors` erases the serialise,
  deserialise and initialise bodies once they are built and marks the helpers nothing calls; the
  module carries `llvmdsdl.accessors_only`, on which the EmitC lowering accepts a schema with no
  bodies. Each emitter then leaves out the object type, the initialiser, the layout assertions and
  the endianness guard, and keeps the constants: `FULL_NAME`, `EXTENT_BYTES`,
  `SERIALIZATION_BUFFER_SIZE_BYTES` and the verdicts are about the wire, which is what the mode is
  for.
- *A file names no other type, and includes none.* A composite's getter answers bytes, so every
  language drops its import or include of the nested type; a reader that wants the nested type's
  accessors includes that type itself. An include a file does not use is a lint diagnostic in the
  consumer's build, and the generated code owes its consumers none. A TypeScript file whose
  accessors all answer bytes refers to nothing in the runtime and imports nothing, which
  `noUnusedLocals` would otherwise refuse. *(Corrected 2026-09-19: C and C++ had kept the include.)*

**Progress, 2026-09-18.** Landed on all seven targets. `llvmdsdl-aliasable-only` compiles a probe
per language against the generated output and nothing else — under `-Werror`, `-D warnings`,
`go vet` and `noUnusedLocals` — and reads a known buffer through the outer type's composite getter
and the inner type's field getter, a setter round trip and a short read. The unreferenced-helper
marking the fold introduced for Rust now reaches every translating backend, so an accessors-only
file carries the scalar helpers its accessors call and nothing else. The file set keeps its names,
less Go's `_host_image.go`, which the lit test holds `--list-outputs` to.

### Phase 6 — container views — done 2026-09-19

*(Added 2026-09-18; designed 2026-09-19.)*

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

**Decisions taken, 2026-09-19.**

- *A whole-invocation mode, `--aliasable-views`.* A view member ties the object to the buffer it
  was read from, which is a different contract from the owning object every consumer builds
  against today; so it is not the default, and it is not switched by a directive in another
  file. Under the mode a composite field whose type carries `@aliasable` is held as a view, and
  a field whose type is merely wire-flat is decoded as before: the directive is the author's
  statement that the type is meant to be read in place, the mode is the consumer's, and a view
  needs both. An array of an `@aliasable` type is one view per element, a fixed array's held in
  place and a variable-length one's beside its count. The catalogue holds fifteen scalar members
  of a wire-flat type inside thirteen non-flat containers, no fixed arrays of one and three
  variable arrays.
- *A view is the field's bytes, as the composite getter answers them.* Phase 4's `get_<field>`
  on a wire-flat container answers the buffer from the field's offset with what remains, and a
  view member holds that same pair. C and C++ hold the runtime's `dsdl_runtime_view_t`, a
  pointer and a size; Rust a `&'a [u8]`, which puts a lifetime on the container and on every
  type that holds one; Go a `[]byte`; TypeScript a `Uint8Array`; Python a `memoryview`. The
  nested type's own accessors read it, so no view carries methods and no language grows a second
  API.
- *A short view is not an error.* A buffer that ends inside the field is a valid encoding, and
  the accessors zero-extend a short view on read, so the container's deserialise holds what is
  there and advances by the field's fixed width as it does now. Serialising a view writes its
  bytes and zero-fills to the field's width, so an empty view, which is what the initialiser
  leaves, serialises as the nested type's default. This narrows the decision that a view
  requires the whole payload to the whole-object view it was taken about: a field view has a
  reader that zero-extends, and a whole-object view had none.
- *Decided at lowering, carried on the step.* `lower-dsdl` stamps `aliasable` on a plan whose
  section asserts it and, under the mode, `held_as_view` on a composite step whose nested type
  does. `build-dsdl-plan-bodies` builds such a step as a view: on deserialise the member takes
  the buffer from the field's offset and what remains, bounded by the field's width
  (`dsdl.store_view`); on serialise the member's bytes are copied to the field's offset and
  zero-filled to its width (`dsdl.load_view`, `dsdl.copy_bytes`). No nested entry point is
  called in either direction. `SectionMetadata` reads the same attribute for the struct, so the
  member and the body cannot disagree, and `dsdl-verify-alias-layout` refuses a view step whose
  nested plan is not asserted, sealed and fixed.
- *Equal-length unions are not in the catalogue.* Its two fixed equal-length unions,
  `ArbitrationID` and `port.ID`, hold sub-byte options, so accessors for such a union serve no
  regulated type; they are 6.3, built on the fixture that has one.
- *A union's accessors are a wire-flat section's, at one offset.* *(6.3, 2026-09-19.)* The pass
  that builds the bodies decides which unions qualify — sealed, of one length, every option whole
  bytes wide and a composite option wire-flat — and builds each option's accessors at the offset
  after the tag through the builders phase 4 has. The tag is reached as a member named `_tag_`,
  which no DSDL field can be called: a getter and a setter in the tag's width, normalised through
  the plan's own tag helpers. Every spelling names it after its own tag member, through the naming
  seams it already has, so an option named `tag` collides with nothing. An option's setter writes
  the value and not the tag: one write per accessor, as phase 4 decided, and selecting is the tag
  setter's with the option's tag constant. No verdict was added: the accessors are functions, and
  a backend follows the functions the pass built, as the contract says.

**Sub-phases.** 6.1 the mode, the ops, the C and object spellings and the lane on C; 6.2 the
other five languages; 6.3 union accessors; 6.4 arrays of views.

**Progress, 2026-09-19: 6.1 landed.** `--aliasable-views` reaches the analyser, which marks each
scalar composite field of an asserted type as held by view and refuses the holder's host-image
verdict on it; the lowering stamps `aliasable` on an asserting plan and `held_as_view` on the
step; `build-dsdl-plan-bodies` builds the step as `dsdl.store_view` on deserialise, `dsdl.load_view`
and `dsdl.copy_bytes` on serialise and `dsdl.clear_view` on initialise; both C lowerings spell the
four, C over a `dsdl_runtime_view_t` member and the object target over a pointer-and-size struct;
`dsdl-verify-alias-layout` refuses a view of a type that does not assert, of a delimited type, on
a union's option, and in a plan that claims to be a host image. `llvmdsdl-container-views` holds
the C and object targets to the contract on a delimited holder of a Pose and a Vec3: the view
points into the buffer, the Pose's accessors read it through the view, serialising reproduces the
wire, a buffer ending inside the Pose leaves a short view read as zeros and serialised zero-filled,
and an initialised holder serialises an empty view as zeros.

**Progress, 2026-09-19: 6.2 landed.** The translator carries the four view operations to the five
spellings through five hooks, and each holds the view in its own slice: C++ in the runtime's
`dsdl_runtime_view_t`, as C does; Rust in a `&'a [u8]`, which puts a lifetime on the holder, on
its impls, on its deserialise and on every type that holds one, decided by a walk of what each
type holds; Go in a `[]byte`; TypeScript in a `Uint8Array`; Python in a `memoryview`. The
initialiser renderer reads `dsdl.clear_view` as a member holding nothing, which each language
renders as its empty slice. A file names no type it holds only as a view, so the dependency
collectors leave such a type out of the imports and includes every language writes, while the
manifests keep it. The C and C++ files then had their include lists read off their own text: a
standard header is included where a type of its is spelled, the runtime where a primitive or a
runtime type is named, a nested type's header where the type is named or its entry points are
called, so a header of two scalars includes three headers and a folded type's implementation
includes no nested header at all. `llvmdsdl-container-views` now runs the probe on all
seven targets, Rust under `-D warnings`, Go under `go vet` and TypeScript under `noUnusedLocals`;
the Python probe also writes through the buffer and reads the change through the view.

**Progress, 2026-09-19: 6.3 landed.** A union's tag is a detached `dsdl.io` step the spellings hold
beside the plan's, so their accessor paths read its shape as they read a field's. The tag step
carries the plan's own tag helpers, which mask to the tag's width. `llvmdsdl-union-accessors`
holds all seven targets to the decode on a four-option union of a word, a float, a byte array and
a wire-flat record: the tag and the selected option read what the decode holds, a composite option
answers its bytes, a short buffer reads the tag and zeros, and the tag setter with an option setter
selects an option the decode then sees. The lit test pins the tag at offset nought and every
option after it, and that a ragged, a sub-byte and a delimited union get none. The probe also
found that a union's TypeScript file imported every option's factory while its own factory made
the selected option alone; a file now imports what its text uses.

**Progress, 2026-09-19: 6.4 landed.** An array of an asserted type is held as a view per element.
The analyser marks the array's field as the scalar's; `dsdl.store_view` and `dsdl.load_view` take
an optional element index, and the element loop builds each element as the view step builds a
scalar, at the index the loop holds. A fixed array is cleared whole by one `dsdl.clear_view`, and
a variable-length array keeps its length machinery, so an initialised holder empties it and a
decode sizes it before storing into it. C holds a fixed array of `dsdl_runtime_view_t` in place and
a variable-length one as its elements beside their count, cleared through
`dsdl_runtime_clear_views`; the object target holds an array of the pointer-and-size struct and
clears it with one memset of the array's size; C++ a `std::array` or the profile's vector of the
runtime type, which takes no memory resource; Rust `[&'a [u8]; N]` or `DsdlVec<&'a [u8]>`; Go
`[N][]byte` or `[][]byte`; TypeScript `Uint8Array[]`; Python `list[memoryview]`. The lane's
`Track` holder, a fixed pair of `Pose` and a bounded trail of them around a byte on each side,
holds all seven targets to the contract on every element: each points into the buffer at its own
offset, the `Pose` accessors read an element, serialising reproduces the wire, a buffer ending
inside the pair's second element leaves that element short and the trail empty, and an initialised
holder serialises every element and the trail as zeros.

What the instruction lane shows needs saying carefully. A static count is per function, and the
plain path's nested decode is a call, so the holder's own count is the same with the view as
without: `Frame`'s deserialise is 75 instructions either way on AArch64 and 88 against 86 on
x86-64. What the view removes is the call's target, `Pose`'s deserialise at 54 and 62, which the
plain path runs on every decode and the view path never does; the lane baselines it beside the
holder under `plain:` so the difference is on the page. The holder's serialise grows, 49 to 61 and
58 to 73, since a bounded copy with a zero-fill branch is longer than a call; the callee it drops
is 19 and 23. For a record that is a host image, as `Pose` is, the view saves its one folded copy
and the call; for a record that is wire-flat and not a host image, it saves the field-wise decode.

**Acceptance** A container deserialised under the mode reads every view field, through the
nested type's accessors, to the values the plain decode gives, on a full buffer and a short one;
serialising it back reproduces the plain serialise's bytes; its deserialise skips the nested
decode, which the instruction lane shows; the output compiles standalone in each language.

## Gates this adds

| Gate | Phase | What fails it | State |
|---|---|---|---|
| catalogue W/H census | 1 | either count moving without the test moving with it | ✅ landed |
| predicate-vs-reality | 1 | an H section whose compiled struct is not a byte image | ✅ landed |
| layout assertions in every host-image type | 3 | a structure that is not the image on the consumer's target | ✅ landed; found the PMR profile, and on 2026-09-19 the compiled-in catalogue, whose model was parsed and not analysed, so its 108 host-image headers carried none |
| `@aliasable` diagnostics | 2 | a failure reason without a field name and location | ✅ landed |
| verifier re-derives H | 2.1 | a `host_image` the steps contradict | ✅ landed |
| candidate lint | 2.2 | a delimited type that would qualify, reported without its field | ✅ landed |
| fold leaves no field work | 3 | field work surviving beside the move, or a non-host-image body folded | ✅ landed |
| bulk-copy instruction count | 3 | an entry point's instruction count moving without its baseline | ✅ landed |
| zero-extension preservation | 3 | a folded body's short-buffer read differing from the field-wise one's | ✅ landed: c-go parity's truncated `Version`, `Natural8`, `Integer64`; c-rust and cpp-c parity's truncated-image `Real32`, `Integer8` |
| accessor equivalence | 4 | an accessor disagreeing with `deserialize_`, in any of the six | ✅ landed: scalars, fixed arrays, nested composites |
| accessor instruction count | 4 | an accessor's count moving without its baseline | ✅ landed, on both pinned triples |
| accessors-only standalone build | 5 | the mode's output failing to compile alone on any target, or a targeted type without the directive going through | ✅ landed: seven targets, a probe each |
| container view contract | 6 | a view not pointing into the buffer, a read through it disagreeing with the decode, a serialise not reproducing the wire, or a short or empty view mishandled, of a scalar member or of any element of a fixed or variable-length array | ✅ landed on all seven targets |
| view holder instruction count | 6 | the holder's count, or the removed callee's, moving without its baseline | ✅ landed, both pinned triples |
| union accessors against the decode | 6.3 | a tag or option read disagreeing with the decode, or a union that does not qualify getting accessors | ✅ landed on all seven targets |

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
The instruction counts of the folded bodies are the `host-image-instruction-counts` lane, which
prints every count it takes:

```bash
ctest --test-dir <build> -R host-image-instruction-counts -V
```

To read the reasons directly instead:

```bash
dsdlc -l c -O out +uavcan && grep -rh '_WIRE_FLAT_REASON_ "' out | sed 's/.*REASON_ //' | sort | uniq -c
```
