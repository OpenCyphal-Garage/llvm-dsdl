# Clean Code

A backend is a translation of MLIR. That holds for the bodies: `build-dsdl-plan-bodies` produces
a serialise, a deserialise and an initialise function per plan, and each backend spells those
functions through a `BodySpelling`. [Backend Translation](docs/development/backend-translation.md)
is the record of that work.

It does not hold for the declarations. Section 4 of [DESIGN.md](DESIGN.md) puts type declarations,
module layout, manifests and runtime support outside the body contract, free to consult the
semantic model. Each emitter answers them by string concatenation, and the six answers were
written from one C-shaped starting point. The output is not idiomatic in any of the six languages.

This is the plan to bring the declaration half under a contract of its own.

## The output

Taking `uavcan.file.List.0.2` from the regulated corpus, at `0403f21`:

| | the section type | a synthesised helper |
|---|---|---|
| C | `struct uavcan__file__List__Request` | `int8_t llvmdsdl_plan_capacity_check__uavcan_file_List_0_2__request` |
| C++ | `struct uavcan::file::List_Request` | `inline std::int8_t uavcan::file::mlir_llvmdsdl_plan_capacity_check_uavcan_file_List_0_2_request` |
| Rust | `pub struct uavcan_file_List_Request` | `fn mlir_llvmdsdl_plan_capacity_check__uavcan_file_List_0_2__request` |
| Go | `type List_Request struct` | `func mlir_llvmdsdl_plan_capacity_check__uavcan_file_List_0_2__request` |
| Python | `class List_Request` | `def mlir_llvmdsdl_plan_capacity_check__uavcan_file_List_0_2__request` |
| TypeScript | `export interface List_Request` | `function mlir_llvmdsdl_plan_capacity_check__uavcan_file_List_0_2__request` |

The corpus yields 658 helper symbols, and the same 658 appear in all six languages under a spelling
that belongs to none of them — C drops the `mlir_` prefix and C++ collapses the doubled underscores
it reserves, which is the whole of the variation. In C++ they sit at namespace scope, reachable by
ADL; in Python they sit at module scope with no leading underscore, so `import *` exports them; in
C they carry external linkage, so a corpus contributes 658 `llvmdsdl_`-prefixed symbols to the
global namespace and two corpora linked together collide.

Each identifier restates the scope that already contains it. `uavcan_file_List_Request` is declared
in `crate::uavcan::file::list_0_2`; the path is spelled twice. `mlir_llvmdsdl_plan_capacity_check_uavcan_file_List_0_2_request`
is declared inside `namespace uavcan::file` beside `List_Request`, and names both again.

Beyond the shared mangle, each language carries its own:

**C** is the closest to right, having been rebuilt through the body translator in #40.
`uavcan__file__List__Request` uses `__` as the separator a language without scopes needs, and the
out-of-line `_ir_` entry points with inline wrappers are a deliberate shape. The helpers need
internal linkage.

**C++** flattens `List.Request` to `List_Request` where the language has both nested classes and
namespaces. The free `List_Request_serialize_` duplicates the `serialize` member, so one operation
has two public spellings.

**Rust** emits `#![allow(non_camel_case_types)]`, `#![allow(non_snake_case)]` and
`#![allow(non_upper_case_globals)]` into every module and the crate root, from
`lib/CodeGen/emitter/Rust.cpp` at two sites. rustc reports this class of defect by default; the
generator silences it. `Result<usize, i8>` answers with a bare integer where the language has a
trait for the purpose.

**Go** spells types `List_Request` and constants `LIST_REQUEST_FULL_NAME`, neither of which is Go,
and returns `(int8, int)` where the language returns `error`. The receiver is `obj`.

**Python** spells classes `List_Request` against PEP 8, and puts the type's facts in module-level
`DSDL_*` constants rather than on the class they describe.

**TypeScript** exposes `makeList_Request`, `serializeList_RequestInto` and
`deserializeList_RequestFrom` — free functions carrying the type name, which is how a language
without methods does it.

## A scope is a concept; the tree of scopes is not

[Identifier naming](docs/development/identifier-stropping.md) already has the two ideas this needs.
A **role** is what an identifier will be used as, and it is exhaustive and mandatory. A **scope** is
a region in which identifiers must not collide, and `NamingScope` allocates within one.

What is missing sits between them. Nothing builds the tree of scopes, and nothing decides which
scope a declaration belongs to. `makeSectionFieldScope` is handed a flat list of fields; the
emitter decides by concatenation where the resulting names are written and what encloses them.
`DefinitionNamePolicy` carries a single capability — `namespaceJoin`, empty where the language has
namespaces of its own — and that is the whole of what the compiler knows about the shape of a
target language.

So the declaration half has a policy for *how a name is spelled* and none for *where a declaration
lives*. Since the second question is answered six times in six emitters, it is answered the same
way six times.

The helper name shows the cost twice over. `lib/Transforms/Passes.cpp` mints
`llvmdsdl_plan_capacity_check__<schema>__<section>` as the MLIR symbol. `renderHelperBindingIdentifier`
re-mangles that string at render time into `mlir_llvmdsdl_…`, collapsing `__` for C++ alone. The C
backend uses the first, the other five use the second. One symbol, two spellings, computed in two
places — which is what happens when a name is decided away from the references to it.

## Language classification

The classification is the input the surface layer reads. One row per language, stating what the
language can express, indexed by the questions a declaration's shape depends on.

| | scopes below the file | nested type declarations | methods | internal linkage | error convention | type's own constants |
|---|---|---|---|---|---|---|
| C | none | no | no | `static` | status code | macros in the enclosing scope |
| C++ | namespace, class | yes | yes | private member | status code | `static constexpr` in the class |
| Rust | module | no | yes, in `impl` | private by default | `Result<T, E>` | associated `const` |
| Go | none below the package | no | yes, by receiver | lower-case initial | `(T, error)` | package scope, typed |
| Python | class | yes | yes | `_` prefix | exception | class attribute |
| TypeScript | class, namespace | yes | yes | not exported | exception | `static readonly` |

A row is a claim about the language, not a preference, which is what makes it testable and what
keeps it out of the emitters. Two consequences follow directly and are worth stating because they
are the ones a shared implementation gets wrong: Go has no scope between the package and the type,
so a Go package is flat and its names must be distinct across every file in the directory; C has no
scope at all, so C alone composes a path into each identifier, and C is therefore the shape the
other five must stop imitating.

The table grows as the languages demand: generic parameters, trait or interface conformance,
visibility levels finer than two, and operator overloading each become a column when a backend
needs one. It does not grow to hold preferences. `pmr` versus `std` containers is a profile, not a
capability, and stays where profiles are.

## The surface tree

A pass — `project-dsdl-surface` — runs at the end of `lower-dsdl-bodies`, after every body
transformation, parameterised by the target language. It reads the classification and produces:

- a tree of scopes, each a `SymbolTable`, mirroring what the target language will actually open —
  namespaces and classes for C++, modules for Rust, one package for Go, one module for C, and for
  the type's own scope wherever the language has one;
- every declaration placed in a scope, carrying its kind (type, field, constant, method, free
  function, helper) and its visibility;
- every identifier allocated within its scope, through the existing role and `NamingScope`
  machinery, so that nothing restates its own address;
- every symbol reference in every body rewritten to match, through `SymbolTable::replaceAllSymbolUses`.

The last point is why this belongs in the IR rather than in a codegen-side structure beside
`SectionMetadata`. `SectionMetadata` holds facts, nothing references it, and a struct is the right
home for it. A declaration's name is referenced — by `func.call` in the bodies, by `dsdl.call_serdes`
across definitions, by the naming manifest, and by the collision check in `Discovery`. A name
decided anywhere other than where the references live acquires a second spelling, which is the
defect already in the tree. MLIR's nested symbol tables and `SymbolRefAttr` exist for this, and the
verifier will then reject a scope whose declarations collide rather than leaving it to a consumer's
compiler.

Placing the pass at the end of the pipeline contains the cost. Every pass upstream keeps walking a
flat module of `func.func`; only the emitters and the manifest see the tree.

Two consumers move onto the tree in the same change, or they drift from it:

- `renderNamingManifest` reports the identifiers a backend writes. It must read the tree rather
  than recompute the projection, which is what makes it a report rather than a second opinion.
- `Discovery` rejects two definitions whose generated type names or file stems collide, today by
  calling `renderSectionTypeName` itself. Renaming under the tree without moving the check makes
  it reject collisions that no longer exist and miss ones that do.

## One renderer, one declaration spelling per language

`translateFunction` holds the structure of a body and asks a `BodySpelling` for the language.
The declaration half takes the same shape: a shared renderer walks the surface tree — opening and
closing scopes, emitting declarations in dependency order, placing definitions — and asks a
`DeclarationSpelling` for the syntax.

The renderer holds: scope nesting and ordering, forward declarations where a language needs them,
which declarations are public, and where a body is attached. The spelling holds: how this language
opens a namespace, writes a field, declares a constant, spells a signature, marks a declaration
internal, and returns an error.

This is where the string emission falls. The six emitters hold 1,077 emission sites between them,
782 of which are outside the `BodySpelling` subclass — the declaration half — and 358 of those are
in `Ts.cpp` alone. A signature is assembled by concatenation at every entry point, for every
profile, in every language:

```cpp
w.line("inline std::int8_t " + plan.typeName + (serialize ? "_serialize_(const " : "_deserialize_(") +
       plan.declaredName + "* const " + object + ", " + (serialize ? "" : "const ") +
       "std::uint8_t* const buffer, std::size_t* const inout_buffer_size_bytes" + resource + ")");
```

A signature in the tree is a declaration with typed parameters and a return; the spelling writes
one parameter list. Those 782 sites are the measure the mechanism phases are gated on.

This is not licence to add a template engine. A template is a second statement of the shape, in a
language the compiler cannot check, and it is what every code generator reaches for — nnvg included,
which is why the pull towards one is worth naming. Where nnvg's shape lives in a per-language
template tree that a human keeps in agreement with five others, ours lives in a classification a
pass reads: adding a language is a row in the table and a spelling, and the shape follows from what
the language can express. Giving up that property to save string concatenation trades the reason
this compiler exists for a convenience the renderer already provides.

## Where each language lands

Named for `uavcan.file.List.0.2` under the versioned scheme, which is what the type names below assume.

**C.** `struct uavcan__file__List__Request` and `uavcan__file__List__Request__serialize_` stay: the
path in the identifier is what a language without scopes requires. The 658 helpers become `static`,
and drop to names unique within their translation unit.

**C++.** `uavcan::file::List_0_2::Request`, nested, with `serialize` and `deserialize` as members
and the type's facts as `static constexpr` data members. A helper becomes a private static member
function of the section type it operates on, losing the prefix that type now supplies:
`Request::capacity_check`. The type is the scope, so nothing sits beside it — no free entry points
and no `detail` namespace. A service's outer name becomes the struct that encloses the two sections
and holds no members of its own, which is what removes the `using List = List_Request` alias: the
alias existed because a flat scope had no way to say that `Request` belongs to `List`.

**Rust.** `pub struct Request` in module `uavcan::file::list_0_2`, with `impl Request` holding
`serialize`, `deserialize` and the type's facts as associated consts. Rust has no type nested in a
type, so its module is what carries the enclosure C++ gets from nesting. A helper is a private `fn`
of the module.
`Result<usize, Error>` replaces `Result<usize, i8>`, over an enum deriving `Debug` and implementing
`Display` and `core::error::Error` — stable in `core` since 1.81, so `no_std` keeps it. The variants
are values, so returning one allocates nothing.

**Go.** `type ListRequest struct` in package `file`. Go's own wire interfaces come first:
`MarshalBinary() ([]byte, error)` and `UnmarshalBinary([]byte) error`, so a generated type satisfies
`encoding.BinaryMarshaler` and `encoding.BinaryUnmarshaler` and drops into anything that already
speaks them. The buffer-oriented pair stays beside them for the path that allocates nothing:
`func (r *ListRequest) Serialize(buffer []byte) (int, error)`. Errors are package-level sentinels
built once with `errors.New`, which is how a returned `error` costs no allocation per call.
Constants take a `const` block in Go's own case, not `LIST_REQUEST_EXTENT_BYTES`. Helpers stay in
the package scope Go gives them and take unexported camelCase names, which is Go's visibility
mechanism rather than a convention over one.

**Python.** `class ListRequest` with the type's facts as class attributes. Helpers become
`@staticmethod` on the section class where they belong to one, and module-private otherwise.

**TypeScript.** `export interface ListRequest` for the data, and a `const` of the same name holding
`create`, `serialize` and `deserialize`. An interface and a value of one name coexist — the first
declares into the type namespace, the second into the value namespace — so `ListRequest` is both
the type a consumer annotates with and the object the operations hang off, and
`ListRequest.deserialize(bytes)` completes in an editor where a free function named after its type
does not. Helpers stay module-private and take camelCase names.

## Phases

Each phase is one change, with a gate that fails against the tree before the phase is written.

Rust landed first, in #41, ahead of the mechanism phases rather than after them. That was not the
order below and it earned something: phase 3's gate is that the surface tree reproduces today's
output byte for byte, and today's output is now idiomatic Rust rather than the flat names. A tree
that reproduces `list_0_2::Request` is tested against the shape it exists to produce; one that
reproduced `uavcan_file_List_Request` would only have been tested against the shape it replaces.

**1 — The judges.** *Landed for Go, Python, TypeScript and Rust: each judge is in the image,
asserted, and holding its lane to a baseline. The C and C++ lane is still to be built.*

The compilers already run over the regulated corpus: `RunUavcanRustCargoCheck`
runs `cargo check`, `RunUavcanGoBuild` runs `go test ./...`, `RunUavcanTsTypecheck` runs `tsc`, and
the C and C++ generation lanes compile under `-Werror`. What none of them judges is idiom, because
a compiler accepts an un-idiomatic name by design. Python has no lane of either kind.

Rust is the exception, and not in the way it first appears. `RunContainerViews`, `RunAliasableOnly`,
`RunUnionAccessors` and `RunDeprecationAttributeCompileGate` already compile generated Rust under
`RUSTFLAGS=-D warnings`, and all four are release-blocking. rustc's naming lints are on by default,
so the judge was installed and the gate was hard. `#![allow(non_camel_case_types)]`,
`#![allow(non_snake_case)]` and `#![allow(non_upper_case_globals)]` were what kept it green: removing
those three lines from the emitter turns all four lanes red at once, 43 errors on the union-accessors
fixture alone and 802 warnings over the regulated corpus — 658 helper names and 144 type names, with
nothing at all under `non_upper_case_globals`, which was suppressing nothing. The suppression was not
hiding advice. It was defeating a gate the project already had.

So the Rust naming cannot be deferred behind a baseline the way the others can: these lanes are
pass-or-fail over a whole crate, and the allows are in-source debt that the no-suppression rule
already forbids.

Each remaining language gains the style judge its compiler is not: `cargo clippy -- -D warnings`
beyond what rustc denies, `staticcheck` for Go's `ST1003`, `ruff` with the `N` rules for Python,
`eslint` with `@typescript-eslint/naming-convention`, and `clang-tidy`'s
`readability-identifier-naming` over the generated C and C++. `ts26.4.5` carries all of them --
`staticcheck` 2025.1.1, `ruff` 0.16.8, `eslint` 10.11.0, `clippy` 0.1.93 and `typescript-eslint`
8.70.1 -- beside the `clang-tidy` the lint lane already runs. `tools/assert_style_judges.py` is what
says so: every lane that asserts its toolchains asserts the judges too, and reports the versions it
found.

The parser is required rather than merely reported, because eslint cannot read a `.ts` file without
it: 10.11.0 answers `interface` with `Parsing error: Unexpected token interface`, and a
configuration whose `files` misses `.ts` lints nothing and exits zero. An image carrying eslint
alone can host no TypeScript judge, and would say so by passing.

`eslint` is also the one judge the image must not merely carry but resolve to. `ts26.4.4` had
eslint 10 as an unreachable dependency of `typescript-eslint` while `/usr/bin/eslint` was Debian's
6.4.0, which predates flat config and sits outside the parser's peer range. The assertion reads the
version of whatever `eslint` resolves to, so that arrangement fails rather than judges.

The version a judge is pinned at is part of what it reports, so the counts are taken with the
judges `ts26.4.5` carries and nothing else: `ruff` 0.16.8, `staticcheck` 2025.1.1, `eslint` 10.11.0
beside `typescript-eslint` 8.70.1 and TypeScript 5.2.2, and `clippy` 0.1.93. Each is recorded per
rule in `test/integration/judge-baselines/`, written by the lane rather than added up by hand.

Two of those differ from what a local run may have. `clippy` 0.1.95 reports `nonminimal_bool` six
times where 0.1.93 reports `collapsible_else_if` thirty-four, so a host reading 607 and a lane
reading 641 are two judges rather than a regression. TypeScript 5.2.2 against 6.0.3 changes none of
these counts, which is why the pairing the image ships is enough. `typescript-eslint` holds
TypeScript below 6.1 in any case. `no-useless-assignment` arrived in `eslint` 10 and reports a defect the
older release did not, so a lane pinned behind it would have ratcheted in a shape two other judges
already name. The lint lane holds `eslint` at 10 or newer for that reason, and holds the other two
to nothing: the versions tried report identically, rule for rule.

The lanes are `llvmdsdl-uavcan-<language>-style-judge`, one per judge, each generating the regulated
corpus and holding its judge to `test/integration/judge-baselines/<language>.json`. The comparison
is per rule rather than on the total, because a total alone lets one rule grow behind another
shrinking. A rule absent from a baseline is a regression at any count: a judge reporting something
it has never reported is exactly what the lane is for.

A baseline records the judge that produced it and is not compared against another version of it.
Off CI that mismatch is a skip, since a developer's judge differing from the image's is not a
verdict on the generated code; on CI it is a failure, because the image pins every judge and a
mismatch there means the baseline is stale. Each lane is registered only for a judge the build can
find, which is how every language lane here behaves, and the toolchain assertion is what stops a
judge going missing in CI without anyone noticing.

The judges gate the phases after this one. Rust's phase converged because rustc names the defect:
eighteen findings came out of #41, and the six of them that earlier fixes in that same branch created
were each caught by a judge rather than by inspection. A language's phase run ahead of its judge
discovers its defects in review, which is the cost this phase exists to avoid.

A presence or byte-parity gate ratchets in the shape it finds, so no existing gate can report this
class of defect; that is why the phases below come after this one rather than before it. The
findings this produces are the backlog, recorded as a baseline outside the generated source that
later phases may only shrink.

The naming was the half that was expected. With the Rust names fixed, rustc reports nothing over the
regulated corpus, and `cargo clippy -- -D warnings` reported 2,134 findings, none of them about a
name. Around 1,700 shared one cause: a plan opens each body by testing its pointer arguments against
null, and a Rust `&self` or `&mut [u8]` cannot be null, so the guard survived as `false || false`, a
dead `if`, and an error path nothing can reach.

That was a defect of the lowering rather than of any backend, and the fix went where every backend
inherits it. `dsdl-fold-null-guards` runs after `build-dsdl-plan-bodies` under a `TargetNullability`
the driver derives from the target: Rust keeps neither test, Go, TypeScript and Python keep the one
on the object a caller may omit, and C and C++ keep both. It canonicalises the bodies it changed
rather than waiting for `--optimize-lowered-serdes`, which is off unless a caller asks for it, so a
guard folded to a constant is never left where a reader would find it. The C and C++ output is
unchanged byte for byte.

Two findings survived the fold as spelling rather than lowering, and both were bodies whose plan
cannot fail. Rust wrapped the plan's error code in `if err == 0i8 { Ok(..) } else { Err(err) }`
against an error the fold had made the constant zero, and bound the slice's length to a size local
that the body overwrote before reading. Neither shape is in the IR, so both are answered in the
emitter: a function whose every return is a constant zero spells the success arm alone, and a body
that never reads the size it is handed leaves the local's declaration to its own write.

With the other three judges installed beside it, the backlog this phase exists to produce came to
10,106 findings over the regulated corpus. Fixing what the four of them agreed on took it to 5,499,
and every one of those fixes was in a single place:

| where | what it was | what it is |
|-------|-------------|------------|
| `lower-dsdl-exec` | `cmpi eq %held, false` | `xori %held, true`, which every language spells with its own negation |
| `lower-dsdl-exec` | a tag chain seeded with `false` | a chain beginning at the first option |
| `dsdl-fold-null-guards` | reads the fold orphaned, which a canonicaliser keeps | swept to a fixed point, so no arm arrives empty |
| `BodyTranslator` | an `scf.if` of values spelled as a branch | the select it is |
| `BodyTranslator` | a null test negated by wrapping its text | `isNotNull`, which each language spells as its own test |
| `EmitCommon` | `isRead`, in four copies, three of them stale | `plansReadOfSize` |
| `HelperBindingNaming` | one lowered symbol per helper, in four languages | a name the scope holding it reaches it by |
| `Ts.cpp` | `interface X {}`, which any non-nullish value satisfies | `Record<string, never>` |
| `dsdl-fold-unobserved-accessor-sizes` | a composite getter's written-back length, in a local each of four languages suppressed | erased where the getter returns a view that carries its length |

The composite getter was the one defect three judges named alike, 44 times each. It answers a
pointer to the nested type's bytes and writes their length through another, which C and C++ read.
Go, Python, TypeScript and Rust answer a slice, a `memoryview` or a `Uint8Array` instead, which
carries the length, so the write landed in a local nothing read -- and each language had been
taught to hide it: `_ = outSize`, `void outSize`, a leading underscore. The write is erased in the
lowering for those four, where it is a question about the getter's signature and not about nulls,
and the canonicaliser takes what fed it, including the subtraction a getter at a non-zero offset
used. An emitter declares the size only where a plan still reads it.

The helper naming was the largest of them. A definition's 658 lowered helper symbols reached the
output verbatim -- `mlir_llvmdsdl_plan_capacity_check__uavcan_diagnostic_Record_1_1` -- which was
every one of Python's `N802` findings and three fifths of its over-long lines. Rust had already
answered it: a helper is private to the scope the definition is generated into, so the schema
component of the symbol names what that scope already says. `renderSchemaHelperNames` is that answer
for all four. Rust, TypeScript and Python each give a definition a module of its own, so the scope
covers one definition and the name is what distinguishes one helper from its siblings:
`capacity_check`, `capacityCheck`, `_capacity_check`. A Go package holds a whole DSDL namespace, so
the scope is the package's and the name carries the definition: `recordCapacityCheck`. C and C++
reach a helper by a symbol that carries the whole definition and are unchanged.

Three generation gates asserted the old symbol by name. A presence gate ratchets in the shape it
finds, which is what this phase exists to notice.

Go's names then went the way Rust's had. `SEVERITY_TRACE` is not a Go constant; `SeverityTrace`
is. Three roles moved to a casing of Go's own -- a constant, a field and an exported function to
`GoExported`, a local and a helper to `GoUnexported` -- and both recase each word of a source that
has no lower case, leave the capitals of one that does, and upper-case the words `ST1003` calls
initialisms. `FULL_NAME` means `FullName`; `VSLAMPoseUpdate` is the author's and stays; `unique_id`
is `UniqueID`. A Go name carries no underscore, so a scope's ordinal joins with nothing there:
`FooBar`, `FooBar2`.

A Go constant carries the type it belongs to, so the parts are projected apart and joined -- the
type verbatim, since it is already the type's name and folding its separators would put 1.23 and
12.3 of one definition on one constant. The scope keys on the DSDL parts rather than the composed
name, because `barBaz` and `bar_baz` are two constants and `CBarBaz` is one name, and the generated
tokens are keyed apart from the DSDL ones, because a definition may declare a constant called
`FULL_NAME` and the claim exists for exactly that. The naming manifest builds the same scope, so
what it reports is what is written.

A service section joins with nothing in Go, TypeScript and Python: `GetInfoRequest`. An underscore
inside a PascalCase name is what `ST1003` and `N801` report and what `naming-convention` rejects.

| language | judge | before the sweep | now |
|----------|-------|-----------------:|----:|
| Rust | clippy | 867 | 641 |
| Go | staticcheck | 3,241 | 318 |
| Python | ruff | 4,188 | 1,718 |
| TypeScript | eslint | 1,810 | 548 |
| | | **10,106** | **3,225** |

What is left is per-language.

| count | language | lint | cause |
|------:|----------|------|-------|
| 997 | Python | `E501` | long lines |
| 545 | TypeScript | `naming-convention` | `_bound0_` and `_result1_` locals |
| 381 | Rust | `needless_late_init` | an `scf.if` with statements in an arm; only Rust has a block expression to take it |
| 290 | Go | `ST1000`/`ST1021`/`ST1022` | a package comment, and doc comments that open with the identifier |
| 198 | Python | `F401` | unused imports |
| 181 | Python | `UP037` | quoted annotations |
| 176 | Python | `SIM300` | `2112 > p0` rather than `p0 < 2112` |
| 164 | Python | `SIM108` | the remaining branch-not-expression sites |
| 158 | Rust | `unnecessary_cast` | a load casts to the storage type where the field already spells it |
| 59 | Rust | `derivable_impls` | a written-out `Default` that `#[derive(Default)]` covers |
| 34 | Rust | `collapsible_else_if` | an `else` holding one `if`, which the branch shapes leave behind |
| 28 | Go | `ST1003` | a package name with an underscore, and the runtime scaffold's own constants |

**2 — The classification.** The capability table, and `LanguageProfile` reading it. Consumed by
nothing yet. Gate: unit tests pin every row, and the emitters are shown to agree with the row that
describes them.

**3 — The surface tree.** `project-dsdl-surface`, the scope ops, symbol allocation and reference
rewriting. The first profile reproduces today's output exactly, so the whole phase changes no
generated byte. Gate: the byte-diff oracle over the showroom and the regulated corpus for all seven
targets, before and against after — zero diff — plus lit tests on the scope structure and a
verifier that rejects a colliding scope.

**4 — The declaration renderer.** `DeclarationSpelling` and the shared renderer; the hand-assembled
signatures and scope prefixes are deleted from all six emitters. Still no generated byte changes.
Gate: the same oracle, plus the emission-site count in the declaration half.

**5 to 10 — One phase per language**, each flipping its row from *as today* to the target above and
turning its judge from phase 1 green. Rust's and Go's names landed ahead of the mechanism, in #41
and #42, and what remains of each is its error type. C is cheapest and can go anywhere. C++ goes
last of the six: nesting is the largest change to the surface tree any language asks for, and
taking it after five languages have exercised the tree tests it on the shape that stresses it most.

Nothing in phases 2 to 10 touches a plan body. The wire is fixed by the round-trip, parity and
cross-language equivalence lanes throughout, and a phase that moves a wire byte has failed.

## The adversarial corpus

`Discovery` rejects a corpus in which two DSDL names reach one generated identifier. The class that
went ungated is the other one: a DSDL name reaching an identifier a backend emits for every type --
a trait its bodies name unqualified, a module its crate root declares, a global the standard headers
put beside a namespace, an accessor composed from another field, a union's synthetic tag. Reaching
it needs a name pair the regulated corpus has no instance of, so those defects arrived one at a time
through review.

`test/integration/generate_naming_adversarial_corpus.py` writes that corpus rather than checking it
in, so an axis is a name in a list, and `RunNamingAdversarialGate.cmake` hands each backend's output
to that language's own compiler. It found three defects on the days it was written, two of them in
backends the Rust work never touched: `namespace index` against POSIX's `index()` in C++, `lib`
against the crate root's own file in Rust, and TypeScript's missing import scope.

Its boundary is worth stating, because it is not obvious from the outside. The gate compiles, so it
finds what a compiler diagnoses and nothing else. `namespace std` compiles: [namespace.std] makes a
declaration added to it undefined behaviour rather than a diagnostic, so the corpus carried a `std`
root namespace and reported nothing. A rule the language states and no compiler enforces needs an
assertion on the name emitted, which is what `naming-stropping.txt` carries for that one. Compile
gates and text assertions are two halves, not alternatives.

## Acceptance

| gate | phases | holds |
|---|---|---|
| each language's own compiler and linter, at maximum strictness, over the regulated corpus | 1, then 5–10 | the output is accepted by the tools that judge that language |
| the adversarial corpus, compiled in every backend | 1 onwards | a generated name does not meet another generated name |
| the name emitted, asserted in lit | 1 onwards | a rule no compiler enforces still holds |
| byte-diff of all seven targets before and after | 3, 4 | a mechanism change changes no output |
| emission sites in the declaration half | 4 | the shape is stated once, the syntax six times |
| round-trip, the C↔language parity lanes, cross-language equivalence | all | the wire is unchanged |
| naming manifest against the surface tree | 3 onwards | the manifest reports what the backend writes |
| no in-source diagnostic suppression in generated output | 5–10 | a warning is answered by changing what is emitted |

The last row is the existing rule, applied where it was not. `#pragma GCC diagnostic ignored` was
removed from generated C and C++ in #24 for the same reason the three Rust `#![allow]` lines were
removed in #41: a suppression moves a build-policy decision into source the consumer compiles, and
hides whatever else lands inside it.

## Known uncovered

**TypeScript does not survive a definition whose own name is its dependency's.**
`adv.shadow.outer.Owner` holding an `adv.shadow.inner.Owner` imports `Owner`, `makeOwner` and the
two body functions beside the ones it declares, and tsc answers `TS2440`. `projectCompositeImports`
allocates no local name for an import, so TypeScript has no import scope at all -- the thing Rust
was given in this branch. The axis is in the adversarial corpus behind `--include-self-shadow`,
which is the reproduction; the gate leaves it off, so the gap is stated rather than gated.

Giving TypeScript an import scope is the first piece of its own phase.

## Decisions

The standard is that the output is unsurprising to an engineer who knows the language. Where a
language's own convention and consistency across the six disagree, the convention wins.

**C++ section types are nested.** `uavcan::file::List_0_2::Request`. Source compatibility with
nnvg's C++ output is not a goal of this compiler, and nothing in DESIGN.md or the docs ever claimed
it as one.

**C++ emits no free entry points.** An operation on a type is a member of that type, and a static
operating on a type is a static member of it. This is what removes `List_Request_serialize_` and
what places the 658 helpers.

**TypeScript emits an interface.** Generated wire types are data that crosses serialisation
boundaries — `JSON.parse`, `structuredClone`, a worker `postMessage` — and each of those returns a
plain object. A class survives none of them: the prototype is dropped, the methods with it, and
`instanceof` answers false. An interface is erased at compile time and describes exactly what comes
back. This is the direction [Protobuf-ES took in 2.0](https://buf.build/blog/protobuf-es-v2),
dropping the classes it generated in 1.0 for plain objects and a schema value, having found that
classes do not survive the frameworks and serialisation paths TypeScript runs in. The companion
`const` recovers the discoverability the class would have given.

**Rust and Go take their languages' conventions**, including the error types, in the same release
as the naming. Both change every consumer call site once; splitting them across releases changes it
twice. The names landed in #41 and #42 ahead of the error types, and v0.3.0 predates both, so the
error types land before the next release.

**`renderSectionTypeSuffix` leaves the installed headers.** `renderSectionTypeName` replaced it at
every call site, and the removed function answers `_Request` where Rust now names the section alone.
Keeping it exported would preserve a call that returns a name no backend emits, so a caller whose
assumption no longer holds gets a compile error rather than a wrong answer. The break is within what
the roadmap sanctions before beta-1.

**A file stem is projected from the versioned name, in every language.** `Break.1.0` strops against
Rust's `break` and gained a trailing `_`, and the separator before the version made the `__` that
`non_snake_case` reports in a module name. Composing first means the keyword check never sees the
keyword, so `break_1_0` needs no escape. The rule is language-neutral rather than Rust-only: one
composition rule beats six, and the seam was as ugly in the other module-scoped languages. It
renames one file in the regulated corpus, `uavcan/primitive/string__1_0.ts` to `string_1_0.ts`,
which is a breaking import path for TypeScript consumers; C and C++ name a header after the raw
short name and do not move. `Discovery` composes the same name, so the collision check keys on the
stem that is written.

C++ keeps its status code. The AUTOSAR profile is C++14 and the embedded profiles run without
exceptions, so neither an exception nor `std::expected` is available across the targets the backend
serves.
