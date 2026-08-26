# Identifier naming defects

Twenty-four defects in the identifier-naming subsystem. Seventeen were found by review of the unmerged range
`f8c2fad..docs/roadmap-g8-scorecard`; [D18](#d18) was found while preparing the fix for root cause
[A](#a-three-emitters-have-no-field-scope), in a file no reviewer had been given; [D19](#d19) by a
fixture added while fixing [A](#a-three-emitters-have-no-field-scope); [D20](#d20) while
reproducing [D5](#d5); and [D21](#d21) while consolidating the file-stem producers under root cause
[D](#d-composed-names-are-spliced-at-the-call-site); and [D22](#d22) by the lane added to run the
naming corpus under both versioning schemes, which also led to [D23](#d23); [D24](#d24) while fixing
[D5](#d5). Each is reproduced against a built `dsdlc`; the
transcripts are the generated output quoted under each entry.

Design record: [identifier-stropping.md](docs/development/identifier-stropping.md).

Twelve of the twenty share three root causes (§A, §B, §C), which between them closed D1–D4 and
D7–D14. A fourth, [§D](#d-composed-names-are-spliced-at-the-call-site), was found by asking why the version
rule behind [D5](#d5) and [D20](#d20) is a per-emitter decision at all. Its mechanism is fixed; the
policy that sits on it is deferred.

**Fixed so far:** [D18](#d18) (fork option 2), root causes
[A](#a-three-emitters-have-no-field-scope) — in all four backends that lacked a scope, not the three
the review found — [B](#b-the-claimed-name-tables-were-built-from-one-plain-type) and
[C](#c-c-claims-nothing-on-a-premise-that-is-false), and the driver fixes D6, D15 and D16. With them
D1–D19 except [D20](#d20), and [D21](#d21)–[D24](#d24), plus the mechanism half of root cause
[D](#d-composed-names-are-spliced-at-the-call-site).

**Deferred:** [D20](#d20) only, pending the newest-version-only feature — the mechanism it needs is
landed (`--versioned-type-names`, unversioned by default), but the policy turns on which versions
reach the corpus at all.

| ID | Severity | Area | Defect |
|---|---|---|---|
| [D1](#d1) | High | engine + emitters | ~~C, C++ and Rust name fields without a scope, so distinct DSDL fields emit one duplicate member~~ |
| [D2](#d2) | High | engine | ~~`UNION_OPTION_COUNT` is missing from the claimed-name tables~~ |
| [D3](#d3) | High | engine | ~~Go's union `Tag` field is missing from the claimed-name table~~ |
| [D4](#d4) | High | engine + C | ~~C claims no names on a false premise about trailing underscores~~ |
| [D5](#d5) | High | C++ | ~~`<Service>_Request` collides with a sibling type and Discovery cannot see it~~ |
| [D6](#d6) | High | driver | ~~`obj` writes only the C half of the naming manifest~~ |
| [D7](#d7) | High | tests | ~~The claimed-name test pins the false premise from D4~~ |
| [D8](#d8) | High | docs | ~~The design record and a golden both describe repair that does not happen~~ |
| [D9](#d9) | Medium | engine | ~~The scope's `_2` suffix bypasses the reserved-namespace encoder~~ |
| [D10](#d10) | Medium | engine | ~~PMR members are missing from the claimed-name table~~ |
| [D11](#d11) | Medium | C++ | ~~Fields and constants use separate pools but share one C++ scope~~ |
| [D12](#d12) | Medium | C/C++ | ~~Array-metadata names are projected outside every scope~~ |
| [D13](#d13) | Medium | obj-cpp | ~~`to_c`/`from_c` are missing from the claimed-name table~~ |
| [D14](#d14) | Medium | golden | ~~`naming-roles.txt` pins a C++ identifier containing `__`~~ |
| [D15](#d15) | Medium | driver | ~~`obj` reports reserved-identifier errors twice, verbatim~~ |
| [D16](#d16) | Medium | driver | ~~`--naming-manifest` writes during `--dry-run`~~ |
| [D17](#d17) | Low | docs | ~~The C emitter comment repeats the D4 premise~~ |
| [D18](#d18) | High | lowering | ~~A sixth copy of the naming policy in `LowerToMLIR` makes `--encode-reserved-identifiers` emit C that does not compile~~ |
| [D19](#d19) | High | obj | ~~`--obj-abi-language cpp` compiles its staged C headers as C++, so a C-only escape does not build~~ |
| [D20](#d20) | High | C/C++/obj | Type versions are not always in the type name, so two versions of one DSDL type collide |
| [D21](#d21) | Medium | manifest | ~~The manifest reports an encoded C/C++ file stem that is not the file on disk~~ |
| [D22](#d22) | High | C | ~~Under `--versioned-type-names` the C header and the C implementation name different types~~ |
| [D23](#d23) | High | obj | ~~The object backend accepts `--versioned-type-names` and ignores it~~ |
| [D24](#d24) | High | frontend | ~~Two different types whose generated names collide are accepted when their versions differ~~ |

---

## Root causes

### A. Three emitters have no field scope

`makeSectionFieldScope` repairs a many-to-one projection by appending `_2`. Go, TypeScript and Python
use it. C, C++ and Rust do not: they call `codegenProjectIdentifier` at each site
(`CEmitter.cpp:360`, `CppEmitter.cpp:1540`, `RustEmitter.cpp:1348` and six more Rust sites).

The design assumed those three needed no scope because `Preserve` is injective. It is — but the
stages after it are not. The keyword strop maps `break` onto `break_`, and the claimed-name escape
maps `serialize` onto `serialize_`, either of which can alias a field literally named `break_` or
`serialize_`. Both spellings are conformant DSDL.

Closes D1, D8, D11. Note that `NamingManifest.cpp` *does* use the scope, so the manifest already
reports the repaired names — the fix makes the emitters agree with it rather than the reverse.

**Blocked by [D18](#d18) for C.** The C struct declaration and the C serialiser take their member
names from two independent implementations. Giving the declaration a scope without addressing that
would break every repaired field rather than only the reserved ones.

**Fixed.** All three now build one `NamingScope` per section at the top of each function that names
a field, and pass it by reference into the render callbacks. C++ additionally declares its constants
into the same scope (see [D11](#d11)); C reaches its serialiser through the `c_name` attributes it
stamps on its own clone of the schema (see [D18](#d18)). The driver emits a note for every repair the
scope makes, which §6 of the design record promised and nothing delivered.

### B. The claimed-name tables were built from one plain type

`runtimeOwnedNames` was populated by generating a message with no union, no PMR flavour and no
service, then reading what came out. Everything those shapes add is absent: the union option count
and Go's union tag field, the PMR memory-resource members, the obj-cpp conversion helpers, and the
per-field array metadata.

Closes D2, D3, D10, D12, D13.

**Fixed.** The names were enumerated by generating a union, a service, an array-bearing message and
a PMR type in all six backends and reading what each declared, rather than by adding the four the
review named. `UNION_OPTION_COUNT` goes into both `kMetadata` and `kCppMembers`, `Tag` into Go's
field set, and `set_memory_resource`, `_memory_resource`, `to_c` and `from_c` into `kCppMembers`;
each is claimed for every type rather than only for the shape that emits it, so a member name does
not change with `--cpp-profile` or with a type becoming a union. The array metadata is not a fixed
list and could not go in a table at all, so it is allocated from the section's constant scope
instead — see [D12](#d12).

The enumeration also turned up two things the review did not:

- `CppObjectAbiEmitter.cpp` names fields with a bare projection at five sites, so root cause
  [A](#a-three-emitters-have-no-field-scope) applies to a fourth emitter. Two colliding DSDL fields
  declared one member twice in the canonical struct and in the C shim, and the conversion bodies
  disagreed with the C struct the C emitter had already repaired. Fixed with the same scopes; the C
  side is built from `makeSectionFieldScope(C, …)`, which is what the C emitter declares.
- The corpus compile gate set `-Wreserved-identifier` for C++ and never passed it to the compiler,
  and compiled only the `std` profile. Both are why a PMR-only omission could sit there unnoticed.
  The gate now compiles both profiles and passes the flag — which is what surfaced
  [D9](#d9).

### C. C claims nothing, on a premise that is false

The C arm returns an empty claimed-name set, justified in `NamingPolicy.cpp:369-371` and again in
`CEmitter.cpp:458-459` by: the generated metadata macros carry a trailing `_`, which no projection of
a DSDL name produces.

The C `ConstantName` policy is `kMacroToken` — preserve case, escape, upper-case, **no strop**. It
passes a source name's own trailing underscore straight through. `isReservedIdentifier` rejects only
names that both start *and* end with `_`, so `full_name_` is conformant DSDL and projects to
`FULL_NAME_`.

Closes D4, D7, D17.

**Fixed.** C gets a claimed-name set holding the eight metadata macro spellings *with* their trailing
underscore, returned for `ConstantName` and `MacroName` alike — the two name one thing in C. Fields
still claim nothing, correctly: the only member C adds of its own is a union's `_tag_`, which DSDL
will not accept as a name.

The change breaks the shorthand the table-invariant test was asserting. "No claimed name ends in `_`"
was sufficient for the one-pass escape but was never the actual property, and C's claimed names have
to be spelled the way C emits them. The test now asserts what the pipeline needs: appending `_` to a
keyword or a claimed name lands on neither another keyword, nor another claimed name, nor a reserved
namespace. `FULL_NAME_` escapes to `FULL_NAME__`, which is claimed by nothing and reserved in neither
C nor C++, C reserving only a *leading* `__`.

### D. Composed names are spliced at the call site

The engine answers one question: how is *this one name* spelled in language L. Roles, keywords,
claimed names, reserved namespaces — all of it is about a single identifier, and all of it is shared.

Nothing owns the next question up. A definition does not have *a* name; it has a type name, a file
stem, an include guard and a symbol base, each built from the same parts — namespace components,
short name, major and minor version — under a rule that varies by language. Those are composed by
concatenation wherever they happen to be needed.

`DefinitionPathProjection.h` is where that composition was meant to live. `renderVersionedTypeName`
projects the short name for the language and appends `_<major>_<minor>`; `renderVersionedFileStem`
does the same for a stem. Three files call them.

```console
$ grep -rE 'to_string\([A-Za-z.]*majorVersion\)|formatv\("\{0\}_\{1\}_\{2\}' lib tools \
    | grep -v DefinitionPathProjection.cpp | wc -l
51
$ grep -rl 'renderVersionedTypeName\|renderVersionedFileStem' lib tools | grep -v DefinitionPathProjection
lib/CodeGen/NamingManifest.cpp
lib/CodeGen/PythonEmitter.cpp
lib/CodeGen/TsEmitter.cpp
```

Fifty-one hand-written splices across seventeen files, against three users of the helper. Not all
fifty-one are type names — file stems, include guards, symbol bases and index keys are in there too —
and that is the point rather than a caveat: one rule, applied to four or five different composed
names, written out fifty-one times.

Two of them are not variations at all. `goTypeName` (`GoEmitter.cpp:212`) is
`renderVersionedTypeName(Go, …)` with the language hardcoded, expression for expression. And
`cTypeNameFromInfo` exists three times — `CEmitter.cpp:125`, `CppObjectAbiEmitter.cpp:91`,
`LowerToMLIR.cpp:69` — byte-identical after whitespace normalisation, in three different libraries.

**Why this is a root cause rather than untidiness.** A rule with no home is made privately by whoever
needs it first. C++ decides whether a type name carries its version from `versionCountByFullName_`, a
private member of the C++ emitter populated from its own definition list. Nothing outside that class
can consult it, which is why:

- `Discovery`'s cross-type collision check cannot see the name C++ will actually emit, so [D5](#d5)
  is invisible to the check that exists to catch exactly that;
- `CppObjectAbiEmitter`, which also emits C++ structs, does not know the rule exists and versions
  nothing — one arm of [D20](#d20);
- `CEmitter` versions nothing either, the other and worse arm, which collides on this repository's
  own showroom corpus.

[D18](#d18) was the same shape one level down: a fourth copy of C naming, in the lowering layer, that
had drifted from the other three and was found only because the escape hatch it broke was under test.
It is closed and the three copies above now agree — but nothing keeps them agreeing.

Covers [D5](#d5) and [D20](#d20); [D18](#d18) was an instance of it.

**Fix.** A per-language table for a definition's names, the shape `rolePolicy()` already has for a
single name's spelling: given a definition and a language, answer type name, qualified type name,
file stem, include guard and symbol base. "Does the type name carry its version" becomes a field in
that table rather than code in six emitters, and [D5](#d5) and [D20](#d20) become a one-line edit.

Two things it should not try to be:

- **Uniform.** Rust qualifies its type name with the namespace (`p_ns_Bar_1_0`), C joins components
  with `__`, Go and TypeScript do not qualify at all. The table is parameterised per language, the way
  the role table is; it is not one function.
- **The identity key.** Some of the fifty-one sites build depfile keys and LSP index keys, which want
  a stable identifier for a definition rather than one a caller writes. That is a different question,
  and folding the two together would be its own defect. The layer should cover generated identifiers
  and leave identity keys alone.

This is larger than either defect under it and touches every emitter, so it is worth deciding as a
piece of work rather than arriving at through [D5](#d5).

**Mechanism landed; policy deferred.** `Support/DefinitionNaming.h` now answers a definition's type
name, file stem, include guard, symbol base and section suffix from a per-language table, and every
in-scope producer delegates to it: `mangleSymbol` x3, `sectionSuffix` x4, `cTypeNameFromInfo` x3,
`goTypeName`, `rustTypeName`, `rustModuleName`, `cppTypeName`, `cppTypeNameFromInfo`,
`shimTypeNameFromInfo`, eleven file-name builders and five include guards. Of the 51 hand-written
version splices, 11 remain and all 11 are the identity keys, the LSP source-filename builder and two
integer `DSDL_VERSION_MAJOR` values -- the categories held out above. Output is byte-identical apart
from [D21](#d21), which the consolidation exposed and which is fixed.

What is *not* done is the policy on top: [D5](#d5), [D19](#d19) and [D20](#d20) are still open. See
the note below on why.

---

## Defects

### D1

**C, C++ and Rust name fields without a scope, so distinct DSDL fields emit one duplicate member.**
High · `lib/CodeGen/CEmitter.cpp:360`, `lib/CodeGen/CppEmitter.cpp:1540`,
`lib/CodeGen/RustEmitter.cpp:1348` · root cause [A](#a-three-emitters-have-no-field-scope)

```
uint8 break
uint8 break_
@sealed
```

```console
$ dsdlc --target-language rust ns --rust-crate-name rv --outdir out   # exit 0, no diagnostic
$ grep 'pub break' out/src/root/ns/dup_1_0.rs
    pub break_: u8,
    pub break_: u8,
```

`rustc` rejects the struct. The same pair duplicates a member in C and C++; in C++ so does
`serialize`/`serialize_`, via the claimed-name escape rather than the keyword strop.

The naming manifest reports these fields as `break_` and `break__2`, so it no longer describes what
the backend writes — the property §3 of the design record exists to guarantee.

**Fix.** Route the three emitters' field naming through `makeSectionFieldScope`, as Go, TypeScript
and Python already do. Rust has seven call sites (368, 448, 733, 763, 1348, 1384, 1394); C and C++
one each plus their serialisation-lambda sites.

### D2

**`UNION_OPTION_COUNT` is missing from the claimed-name tables.**
High · `lib/Support/NamingPolicy.cpp:304` · root cause [B](#b-the-claimed-name-tables-were-built-from-one-plain-type)

C++ (`CppEmitter.cpp:1704`), Go (`GoEmitter.cpp:1313`) and Rust (`RustEmitter.cpp:1453`) emit this
constant for every union, in the same scope and under the same prefix as DSDL constants.

```
@union
uint8 a
uint16 b
uint8 UNION_OPTION_COUNT = 3
@sealed
```

```console
$ grep UNION_OPTION_COUNT out/root/ns/UC_1_0.hpp
  static constexpr std::size_t UNION_OPTION_COUNT = 2U;
  static constexpr auto UNION_OPTION_COUNT = 3;
```

`union_option_count` reaches the same identifier through the upper-casing, so the lower-case spelling
collides too. In C++ a union *option* named `UNION_OPTION_COUNT` collides identically, which makes
this a `kCppMembers` omission as well as a `kMetadata` one.

**Fix.** Add `UNION_OPTION_COUNT` to `kMetadata` and `kCppMembers`.

**Fixed** in both, and claimed for every type rather than only for a union: whether a later revision
of a type becomes a union should not rename a member.

### D3

**Go's union `Tag` field is missing from the claimed-name table.**
High · `lib/Support/NamingPolicy.cpp:324` · root cause [B](#b-the-claimed-name-tables-were-built-from-one-plain-type)

`runtimeOwnedNames(Go, FieldName)` claims `Serialize` and `Deserialize`. `GoEmitter.cpp:1358` also
emits a `Tag` field for every union.

```
@union
uint8 tag
uint16 other
@sealed
```

```console
$ dsdlc --target-language go ns --go-module vf --outdir out   # exit 0
$ grep Tag out/root/ns/u_1_0.go
  Tag uint8
  Tag uint8
```

**Fix.** Add `Tag` to the Go `FieldName` claimed set. Check the other backends' union tag member
names for the same omission.

**Fixed.** The other five spell the discriminator `_tag_` (C, C++, Rust) or `_tag` (Python), or have
no member at all (TypeScript, which renders a union as a discriminated type). `_tag_` is not
conformant DSDL, and the snake projection drops the leading underscore of `_tag`, so Go is the only
backend where a DSDL name can reach the discriminator.

### D4

**C claims no names on a false premise about trailing underscores.**
High · `lib/Support/NamingPolicy.cpp:369-371` · root cause [C](#c-c-claims-nothing-on-a-premise-that-is-false)

```
uint8 v
uint8 FULL_NAME_ = 1
@sealed
```

```console
$ grep 'define root__ns__CT_FULL_NAME_ ' out/root/ns/CT_1_0.h
#define root__ns__CT_FULL_NAME_ "root.ns.CT"
#define root__ns__CT_FULL_NAME_ (1)
```

A macro redefinition: user code reading `<Type>_FULL_NAME_` gets `1` rather than the type name. The
same hole exists for `EXTENT_BYTES_`, `IS_DEPRECATED_`, `SERIALIZATION_BUFFER_SIZE_BYTES_`,
`UNION_OPTION_COUNT_` and `<FIELD>_ARRAY_CAPACITY_`.

`ZOH_ALIAS_ELIGIBLE_` is the worst of them: the generated `try_deserialize_view_` consumes it in an
`#elif`, so redefining it changes generated behaviour rather than only a reported value.

**Fix.** Give C a claimed-name set containing the metadata macro spellings *with* their trailing
underscore, and correct both comments (see D17).

**Fixed** as described. The set was enumerated from what the C backend emits for a message, a union
and a service rather than from the list above: it is the same eight, `FULL_NAME_` through
`UNION_OPTION_COUNT_`. `<FIELD>_ARRAY_CAPACITY_` is not among them because it is not a fixed name —
it is allocated from the section's constant scope instead, under [D12](#d12).

A DSDL constant reaching one of these was already a `-Wmacro-redefined` warning, which the corpus
gate turns into an error; the gate never saw it because no fixture carried such a name. One does
now.

### D5

**`<Service>_Request` collides with a sibling type and Discovery cannot see it.**
High · `lib/CodeGen/CppEmitter.cpp:1975`, `lib/CodeGen/CppObjectAbiEmitter.cpp:361`
· root cause [D](#d-composed-names-are-spliced-at-the-call-site)

The C++ service-section rename `__Request` → `_Request` made a previously impossible collision
reachable: before it, the DSDL spelling needed to collide was `Foo__Request`, which the
reserved-namespace gate rejects.

```
ns/Foo.1.0.dsdl          (a service)
ns/Foo_Request.1.0.dsdl  (a message; conformant DSDL)
```

```console
$ dsdlc --target-language cpp ns --cpp-profile std --outdir out   # exit 0
$ grep -h '^struct Foo_Request' out/root/ns/*.hpp | sort | uniq -c
   2 struct Foo_Request {
   2 struct Foo_Request;
```

Two headers define `struct Foo_Request` in one C++ namespace. A translation unit including both is
a redefinition error. `Discovery.cpp:267-315` keys on each definition's own short name — `Foo` and
`Foo_Request` — so the cross-type check does not fire, and §6 of the design record's guarantee that
cross-type collisions are rejected does not hold.

The collision is C++-only. The other five put the version inside the type name — `Foo_1_0_Request`
for the service section against `FooRequest_1_0` or `Foo_Request_1_0` for the message — so nothing
meets. C++ does not always, which is [D20](#d20) and is what makes this reachable.

**Fix.** Needs a decision. Three shapes:

1. **Register the derived names in the collision keyspace.** A service registers `<Type>_Request` and
   `<Type>_Response` beside its own name, and the existing cross-type path rejects the pair with the
   existing diagnostic. Smallest change. Conformant DSDL that five backends handle becomes an error
   whenever C++ is selected — the §8.1 asymmetry, which the design already accepts — and the keys have
   to be computed with the same version rule the emitter uses, so the check stays coupled to it.
2. **Always version the C++ type name**, which is [D20](#d20)'s fix. D5 then stops existing rather
   than being rejected: `Foo_1_0_Request` and `Foo_Request_1_0` are different names. Costs an ABI
   break for current C++ users, softened by a `using <Type> = <Type>_<newest>;` alias — the trick the
   service arm already uses for `using Foo = Foo_Request;`.
3. **A separator no DSDL name can produce**, reverting `_Request` to `__Request`. Already rejected:
   `__` is C++-reserved and `-Wreserved-identifier` flags it.

**Fixed, as option 1.** `checkServiceSectionTypeNameCollisions` registers each service's two section
names beside every definition's own, and reports a pair that lands on one identifier. Three things
made it worth more than the note anticipated:

- **It runs after parsing, not in discovery.** Whether a definition is a service is a parse result,
  and discovery does not lex. `parseDefinitions` has both halves, so the check lives there.
- **The keys are the emitted identifiers, not a name plus a version.** The note worried that the
  check would have to duplicate the emitter's version rule. It does not: it calls
  `renderDefinitionTypeName` and `renderSectionTypeSuffix`, the same two the emitters now call, so
  it cannot compute a different name than the one written. Under `--versioned-type-names` the two
  names differ and nothing is reported -- fix option 2 remains available and is what the diagnostic
  suggests.
- **Only where a scope is actually shared.** C's single global scope, C++'s namespace and Go's
  package can collide; Rust, TypeScript and Python give every definition its own module, so the
  repeat is unreachable and is not reported. That is a property of the language, not a list of
  languages that happen to fail today.

Two things fell out of it. `renderSectionTypeSuffix` replaced **thirteen** independent splices of
`"_Request"`/`"__Request"` across seven files -- the §D pattern that made D5 possible in the first
place. And keying on the emitted identifier rather than on a name-plus-version closed a second hole
the note did not know about: see [D24](#d24).

### D6

**`obj` writes only the C half of the naming manifest.**
High · `tools/dsdlc/main.cpp:245`, `lib/CodeGen/NamingManifest.cpp:125`

`namingLanguagesForTarget("obj")` returns `{Cpp,"obj"}, {C,"obj"}`. `renderNamingManifest` keys
`byLanguage` by the display name, so the second iteration overwrites the first.

```console
$ dsdlc --target-language obj --target-endianness little ns --outdir out --naming-manifest n.json
$ python3 -c "import json; d=json.load(open('n.json')); print(list(d['languages'])); \
    print(d['languages']['obj']['root.ns.M.1.0']['message']['fields'])"
['obj']
{'serialize': 'serialize'}
```

The C++ ABI header that `obj` publishes uses `serialize_`. A build consuming the manifest to
reference C++ symbols gets the wrong identifier with no error, against the flag's documented promise
of "every target language this invocation names".

**Fix.** Key the manifest by the naming language rather than the display name, emitting `c` and `cpp`
entries for `obj`; or give the two entries distinct display names.

**Fixed** by the second, with a correction the review missed: which languages `obj` names depends on
`--obj-abi-language`. With `c` it publishes C headers only and now reports `{c}`; with `cpp` it
publishes the C++ ABI, the AUTOSAR and PMR headers and a C shim, and reports `{c, cpp}` — the C++
entry giving `serialize_` where the C entry gives `serialize`, which is what each header declares.
`namingLanguagesForTarget` takes the ABI option to decide, so the reserved-identifier sweep and the
output-name collision check narrow with it rather than checking a language the invocation will not
emit.

### D7

**The claimed-name test pins the false premise from D4.**
Medium-as-written, High-in-effect · `test/unit/NamingPolicyTests.cpp:211` · root cause [C](#c-c-claims-nothing-on-a-premise-that-is-false)

The case is commented "Not claimed: C metadata macros carry a trailing underscore, so nothing needs
escaping" and exercises `FULL_NAME`, which genuinely needs no escape. The reachable collision needs
`FULL_NAME_`, which the test does not cover, so the suite cannot fail on D4. The golden corpus
contains trailing-underscore names (`Foo_`, `Break_`) but none matching a metadata macro.

**Fix.** With D4: add `FULL_NAME_` and `ZOH_ALIAS_ELIGIBLE_` cases, and extend the golden corpus.

**Fixed.** The claimed-name test gains five C cases — `FULL_NAME_`, its lower-case spelling,
`zoh_alias_eligible_`, and `union_option_count_` under `MacroName` — alongside the `FULL_NAME` case
that documents what stays put. The golden corpus gains `FULL_NAME`, `FULL_NAME_`,
`zoh_alias_eligible_` and `UNION_OPTION_COUNT`, and a `constants_claimed` scope scenario that shows
the escaped spelling meeting a DSDL name already spelled that way: in C++ `FULL_NAME` escapes to
`FULL_NAME_` and a DSDL `FULL_NAME_` then has to move to `FULL_NAME_2`. Two fixtures,
`TrailingUnderscores` and `TrailingUnion`, put the same names through the compile gate.

### D8

**The design record and a golden both describe repair that does not happen.**
High · `docs/development/identifier-stropping.md:89`, `test/unit/golden/naming-roles.txt:443`
· root cause [A](#a-three-emitters-have-no-field-scope)

§3 states the emitters and the manifest both name attributes through the section scopes; §6 states
in-scope collisions are repaired. Neither holds for C, C++ or Rust.

The golden is self-contradicting: line 443 reads "C, C++ and Rust keep the DSDL spelling for fields,
so they need no repair at all", and lines 452/457/462 immediately below record `break_` → `break__2`
repair for those three languages.

**Fix.** Follows from D1 — once the emitters use the scopes, both texts become true. The golden's
prose needs correcting either way.

**Fixed.** The golden's prose now says every backend needs the scope and why the three that preserve
case still do. §3 of the design record describes the shared-scope rule and the one place a scope
crosses a layer; §6's "plus a note" is now true rather than aspirational.

### D9

**The scope's `_2` suffix bypasses the reserved-namespace encoder.**
Medium · `lib/Support/NamingPolicy.cpp:659-666`

`NamingScope::declare` appends `_` + N to the *projected* base without re-entering the pipeline.
Every stropped keyword and every claimed-name escape ends in `_`, so the disambiguated identifier
contains `__` — reserved in C++, and `reservedNamespaceEncoded` stays false, so the driver's default
reject-mode never sees it.

```
uint8 v
uint8 full_name = 1
uint8 FULL_NAME = 2
@sealed
```

```console
$ grep 'constexpr auto FULL_NAME' out/root/ns/CC_1_0.hpp
  static constexpr auto FULL_NAME_ = 1;
  static constexpr auto FULL_NAME__2 = 2;
```

`FULL_NAME__2` is `[lex.name]`-reserved and is an error under the `-Wreserved-identifier -Werror`
the corpus gate applies — the engine rejects *source* names landing in that namespace by default
while generating one itself.

The injectivity property test generates exactly these inputs but asserts only distinctness, so it
cannot catch this.

**Fixed** in `NamingScope::declare`: the ordinal is joined with `_` except where the base already
ends in one, so `break_` disambiguates to `break_2` rather than `break__2`. The property test now
asserts that no assignment lands in the language's reserved namespace as well as that no two
assignments collide, through a new `codegenIsReservedNamespaceIdentifier`. This came out of root
cause [B](#b-the-claimed-name-tables-were-built-from-one-plain-type) rather than in its own turn:
turning on the C++ gate's unused `-Wreserved-identifier` made the corpus fail to compile.

**Fix.** Re-apply `encodeReservedNamespace` to the suffixed candidate, or choose a suffix separator
that cannot form `__`. Extend the injectivity test to assert legality, not only distinctness.

### D10

**PMR members are missing from the claimed-name table.**
Medium · `lib/Support/NamingPolicy.cpp:312` · root cause [B](#b-the-claimed-name-tables-were-built-from-one-plain-type)

`CppEmitter.cpp:1623-1663` emits `set_memory_resource` and `_memory_resource` into the struct under
the PMR flavour. Neither is claimed. `_memory_resource` is conformant DSDL — it does not also end in
`_`.

```console
$ dsdlc --target-language cpp ns --cpp-profile pmr --outdir out    # field: set_memory_resource
$ grep set_memory_resource out/root/ns/P_1_0.hpp
  std::uint8_t set_memory_resource{};
  explicit P(::llvmdsdl::cpp::MemoryResource* memory_resource) { set_memory_resource(memory_resource); }
  void set_memory_resource(::llvmdsdl::cpp::MemoryResource* memory_resource) {
```

A data member and a member function of one name, and the constructor calls the data member.

**Fix.** Add both to `kCppMembers`. They are flavour-specific, so either claim them unconditionally
or make the claimed set flavour-aware.

**Fixed** unconditionally. A member name that changed with `--cpp-profile` would be an ABI that
depends on how the generator was invoked rather than on the DSDL.

### D11

**Fields and constants use separate pools but share one C++ scope.**
Medium · `lib/CodeGen/SectionNaming.cpp:20`

`makeSectionFieldScope` and `makeSectionConstantScope` build independent pools. A C++ struct body
holds both, which the design record's own rule — roles sharing one language scope share one pool —
says must not happen.

```
uint8 FOO
uint8 foo = 2
@sealed
```

```console
$ grep '\bFOO\b' out/root/ns/FC_1_0.hpp
  std::uint8_t FOO{};
  static constexpr auto FOO = 2;
```

`FieldName` preserves `FOO`; `ConstantName` upper-cases `foo` to `FOO`. Neither pool can see the
other.

**Fix.** For C++, build one scope and declare both roles into it. Go, Rust, TypeScript and Python
put constants outside the type scope and are unaffected — the fix should not merge their pools.

**Fixed** in `SectionNaming.cpp`: `makeSectionFieldScope` declares the constants too when the language
declares both into one region, and `makeSectionConstantScope` returns that same scope. C++ is the only
language for which that is true, so the other five keep a pool each without any caller having to know.
The field keeps its spelling and the constant moves, because fields are declared first.

### D12

**Array-metadata names are projected outside every scope.**
Medium · `lib/CodeGen/CppEmitter.cpp:1476`, `lib/CodeGen/CEmitter.cpp:327-331`
· root cause [B](#b-the-claimed-name-tables-were-built-from-one-plain-type)

`<FIELD>_ARRAY_CAPACITY` and `<FIELD>_ARRAY_IS_VARIABLE_LENGTH` are emitted through a bare
`MacroName` projection, in neither the constant scope nor the claimed-name list. An array field
`foo` and a constant `foo_array_capacity` both reach `FOO_ARRAY_CAPACITY`; two array fields differing
only in case reach one metadata name, duplicating a C++ static and silently redefining a C macro.

**Fix.** Derive these from the same scope that names the fields they describe.

**Fixed.** `makeSectionConstantScope` declares them, under `MacroName`, before the DSDL constants;
`arrayMetadataName` composes the scope key from the DSDL field name so equal field projections give
equal metadata projections. The key carries C's trailing `_` and not C++'s absence of one, because
the scope has to compare what is emitted: without that, C would report a collision between a
metadata macro and a DSDL constant that the trailing `_` keeps apart, and rename a macro that was
never in danger.

The order within the region runs fields, then array metadata, then DSDL constants — least to most
willing to move. A field's identifier is the ABI a caller writes against and has to be predictable
from the DSDL alone; a DSDL constant is the only one of the three an author can rename without
changing the wire format or breaking a field access.

Two of the review's claims did not survive checking. Two array fields differing only in case do
*not* collide in Rust, which already uniquifies its pool-class constants, and the C case is a macro
redefinition rather than a silent one. The rest reproduced.

### D13

**`to_c`/`from_c` are missing from the claimed-name table.**
Medium · `lib/CodeGen/CppObjectAbiEmitter.cpp` · root cause [B](#b-the-claimed-name-tables-were-built-from-one-plain-type)

The obj-cpp canonical struct emits these conversion helpers in the same scope as fields. Both are
conformant DSDL field names.

**Fix.** Add to `kCppMembers` with the other omissions.

**Fixed**, along with the missing field scope in the same emitter — see root cause
[B](#b-the-claimed-name-tables-were-built-from-one-plain-type).

### D14

**`naming-roles.txt` pins a C++ identifier containing `__`.**
Medium · `test/unit/golden/naming-roles.txt:457`

The golden records `struct_body cpp: break_ → break__2` as correct, which freezes D9's output as
expected behaviour. Whatever fix D9 takes, this row moves; until then the suite cannot flag it.

**Fix.** Regenerate with D9.

**Fixed:** regenerated with [D9](#d9). Five rows moved from `X__2` to `X_2`.

### D15

**`obj` reports reserved-identifier errors twice, verbatim.**
Medium · `tools/dsdlc/main.cpp:1650`

Both `OutputLanguage` entries for `obj` carry the display name `obj`, and the sweep does not
deduplicate.

```console
$ dsdlc --target-language obj --target-endianness little ns --outdir out   # field: __bar
$ ... | grep -c 'reserved identifier'
2
```

Two byte-identical messages at the same location.

**Fix.** Falls out of D6 if the entries get distinct display names; otherwise deduplicate on
(location, message).

**Fixed** as the first: it fell out of [D6](#d6). `--obj-abi-language c` now reports once, and `cpp`
reports once per language, naming `c` and `cpp` rather than `obj` twice. Two messages for a name
reserved in both is the same shape `ast` has always had, and renaming fixes both.

### D16

**`--naming-manifest` writes during `--dry-run`.**
Medium · `tools/dsdlc/main.cpp:1693`

```console
$ dsdlc --target-language c ns --outdir out --dry-run --naming-manifest n.json
$ ls n.json
n.json
```

`--list-outputs` implies dry-run and is typically invoked at build-system configure time, so this
writes into a tree the build expects to be untouched. The prune-manifest code below skips dry runs
for exactly this reason.

**Fix.** Guard the block on `options.dryRun`, `listInputs` and `listOutputs`.

**Fixed** as described, and `--help` now says so — a flag that quietly declines to do its job is worse
than one that never had the option.

### D17

**The C emitter comment repeats the D4 premise.**
Low · `lib/CodeGen/CEmitter.cpp:458-459` · root cause [C](#c-c-claims-nothing-on-a-premise-that-is-false)

`emitSectionConstants` explains that its scope reserves no metadata names because "they carry a
trailing `_` … which no projection of a DSDL name produces". Same false statement as
`NamingPolicy.cpp:371`, in the code a reader checks first.

**Fix.** Correct with D4.

**Fixed.** Both comments now say the trailing `_` separates the generated names from most DSDL names
rather than from all of them, and name the mechanism that closes the gap.

---

### D18

**A sixth copy of the naming policy in `LowerToMLIR` makes `--encode-reserved-identifiers` emit C that
does not compile.**
High · `lib/Lowering/LowerToMLIR.cpp:80-102`, `:303`, `:394`

`LowerToMLIR` carries its own C keyword table (`:70-77`) and its own `sanitizeIdentifier`, and writes
the result into the `c_name` attribute. `ConvertDSDLToEmitC.cpp:1364` builds every member reference
in the generated C serialiser from that attribute, while the struct declaration comes from
`CEmitter.cpp:360` via the engine. Two implementations, one member name.

`sanitizeIdentifier` is the pre-engine projection: escape plus keyword strop, with no claimed-name or
reserved-namespace stage. The two agree only for names that need neither.

```
uint8 __bar
uint8 v
@sealed
```

```console
$ dsdlc --target-language c ns --outdir out --encode-reserved-identifiers   # exit 0
$ grep 'uint8_t z' out/root/ns/E_1_0.h
  uint8_t zX005FzX005Fbar;
$ grep -o 'obj->[A-Za-z_0-9]*' out/root/ns/E_1_0.c | sort -u
obj->__bar
obj->v
$ clang -std=c11 -I out -c out/root/ns/E_1_0.c -o /dev/null
error: no member named '__bar' in 'struct root__ns__E'
```

The documented remedy for a reserved DSDL name produces C that does not build. It is also why
[A](#a-three-emitters-have-no-field-scope) cannot be applied to C in isolation: any repair the
declaration adopts, the serialiser will not follow.

Phase 2 of the design work claimed no emitter reaches a case-explicit projection outside
`NamingPolicy.cpp`, and verified it by grepping `lib/` for `codegenTo*CaseIdentifier`. This copy calls
none of those functions — it reimplements them — so that check could not have found it.

**Fix.** Needs a decision; see below. Whichever way it goes, the private keyword table and
`sanitizeIdentifier` in `LowerToMLIR.cpp` should not survive it.

**Fixed, fork option 2.** `stampCMemberNames` in `CEmitter.cpp` writes the scoped member name onto
every `dsdl.field` and `dsdl.io` op of the per-definition clone before the EmitC pipeline runs, so the
declaration and the references come from one scope. `LowerToMLIR` keeps writing `c_name`, but as the
unscoped default and through the shared engine rather than a private copy of it: the private keyword
table and `sanitizeIdentifier` are gone, and the four C type-name sites that also used the sanitiser
now call `codegenProjectIdentifier`. Regenerating the embedded catalog against the engine produced a
byte-identical file, which is the check that the two agreed across the whole UAVCAN corpus.

Keeping the default matters for `dsdl-opt`: `convert-dsdl-to-emitc` is reachable outside `dsdlc`, and
two integration tests run it on raw lowering output. It now rejects a field step with no `c_name`
rather than emitting `obj->` with nothing after it.

### D19

**`--obj-abi-language cpp` compiles its staged C headers as C++, so a C-only escape does not build.**
High · `lib/CodeGen/CppObjectAbiEmitter.cpp`, `lib/CodeGen/ObjectEmitter.cpp`

The obj lane stages a C header and a C source, then — under the C++ ABI — includes that header from
the C++ translation units that implement the ABI struct and the C shim. The header is named with C
naming, which escapes C keywords and nothing else, so a DSDL field named after a C++-only keyword
reaches C++ as a keyword.

```
uint8 class
uint8 v
@sealed
```

```console
$ dsdlc --target-language obj --target-endianness little --obj-abi-language cpp ns --outdir out
out/.obj_stage_cpp/c/p/ns/K_1_0.h:23:11: error: declaration of anonymous class must be a definition
out/.obj_stage_cpp/c_shim/p/ns/K_1_0_c_shim.h:17:11: error: declaration of anonymous class must be a definition
out/.obj_stage_cpp/c_shim/p/ns/K_1_0_c_shim.cpp:11:15: error: static assertion failed ...
                                       'sizeof(p::ns::abi::K) == sizeof(llvmdsdl_cppabi__p__ns__K)'
$ dsdlc --target-language obj --target-endianness little --obj-abi-language c   ns --outdir out   # exit 0
```

`class`, `new`, `operator`, `export` and `template` all reproduce it. `--obj-abi-language c` is
unaffected: no C++ ever sees those headers. The published `c_shim` header has the same problem, and
it is not staging — it is part of the artifact.

The standard `uavcan` namespace uses none of these as a field name, and the corpus compile gate
covers the six source backends rather than `obj`, which is why it has gone unnoticed.

**Fix.** Needs a decision. Three shapes:

1. **The obj lane's C-side naming, under the C++ ABI, is C++ naming.** The staged C is internal and
   the shim type (`llvmdsdl_cppabi__<Type>`) is distinct from what `--target-language c` produces, so
   nothing that ships twice would disagree. Costs: the same DSDL gets one member name from the C lane
   and another from the obj lane's C shim.
2. **A keyword set that is the union of C and C++, for that lane.** The most honest description of the
   constraint — every identifier there has to be valid in both — but it is a seventh naming language,
   which §5 deliberately does not have.
3. **Reject.** Error when a DSDL name is a C++ keyword and `--obj-abi-language cpp` is selected.
   Cheapest and correct, but the C++ lane itself handles `class` perfectly well, so the same
   definition would be accepted for `cpp` and refused for `obj`.

Whichever way it goes, the compile gate should generate the corpus through `obj` as well as the six.

**Fixed** by the first option, applied to all C output rather than to the lane: C's keyword set is
now the union of C's and C++'s. The deciding evidence was that the published `c_shim/*_c_shim.h` is a
genuine dual-language surface — written C-clean and compiled as C by
`test/integration/RunObjCppBackendSmoke.cmake:240-247`, and as C++ by the lane itself — and that the
plain C header `p/ns/<T>_1_0.h` is published in that lane too, so a lane-local rule would give one
DSDL type two different published C headers. `extern "C"` changes linkage, not tokenization.

`class`, `new`, `operator` and `export` all now generate and compile; `template` never reached the
emitters, being rejected by the frontend as a DSDL reserved identifier. The cost is a trailing `_` on
C field and type names that are C++ keywords and not C ones — five rows of `naming-map.txt` and five
of `naming-roles.txt`. Macro constants are untouched: `MacroName` in C is never stropped and always
carries a type prefix.

**It changes one name in the standard corpus, and that is the point.** `uavcan.register.Access` has a
field named `mutable`, so `uavcan/register/Access_1_0.h` declared `bool mutable;` — a header that
could not be included from C++ at all, which is an ordinary thing to do with a generated C header. It
is now `mutable_` and the header compiles as C++. The embedded catalog moves by three lines
accordingly. This is a C ABI change for one standard type; anything reading `.mutable` needs the
underscore.

The gate item is done too: `cmake/RunNamingCorpusCompileGate.cmake` now compiles every generated C
header in one translation unit, which its own comment had promised and which no code delivered. That
is the probe that catches two definitions sharing a type name; the per-unit loop above it cannot,
because a collision needs two headers to meet.

### D20

**C, C++ and obj-cpp do not always put a type's version in its type name, so two versions of one
DSDL type meet.**
High · `lib/CodeGen/CppEmitter.cpp:208-218`, `lib/Lowering/LowerToMLIR.cpp:69-88`,
`lib/CodeGen/CppObjectAbiEmitter.cpp:112-115` · root cause [D](#d-composed-names-are-spliced-at-the-call-site)

Found while reproducing [D5](#d5). Rust, Go, TypeScript and Python name a type
`<ShortName>_<major>_<minor>` unconditionally. The other three do not, in three different ways.

**C never includes it.** `cTypeNameFromInfo` builds `<ns>__<ShortName>` from the namespace components
and the short name, and stops there.

```
ns/Bar.1.0.dsdl    uint8  a
ns/Bar.2.0.dsdl    uint16 a
```

```console
$ dsdlc --target-language c ns --outdir out          # exit 0
$ grep -h 'typedef struct' out/p/ns/Bar_?_0.h
typedef struct p__ns__Bar
typedef struct p__ns__Bar
$ clang -std=c11 -I out -fsyntax-only both.c         # #include both headers
error: redefinition of 'p__ns__Bar'
error: typedef redefinition with different types
error: redefinition of 'p__ns__Bar__serialize_'
```

Six errors: the struct, the typedef and every generated function. The two headers have distinct
include guards, so both are admitted and then collide.

This is not a contrived corpus. `uavcan.diagnostic.Record` has 1.0 and 1.1, and the embedded catalog
records `c_type_name = "uavcan__diagnostic__Record"` for both. So does this repository's own showroom,
which ships `lanyard.flight.VehicleState` at 1.0, 1.1 and 2.0 to demonstrate version coexistence:

```console
$ dsdlc --target-language c examples/showroom/dsdl/lanyard --outdir out    # exit 0
$ grep -h 'typedef struct' out/lanyard/flight/VehicleState_*.h | sort | uniq -c
   3 typedef struct lanyard__flight__VehicleState
$ clang -std=c11 -I out -fsyntax-only both.c    # includes 1.0 and 2.0
6 errors
```

The same two headers under C++ compile clean, because three versions are present and C++ suffixes
when they are. It stays latent in C only because nothing compiles two versions of one type into one
translation unit.

**C++ includes it only when more than one version is present**, from `versionCountByFullName_` — an
undocumented rule, carrying no comment in the emitter and no mention in the reference docs. It avoids
the collision, and the showroom's three `VehicleState` versions do compile together. What it costs is
that the type name is a function of the invocation rather than of the definition:

```console
$ dsdlc --target-language cpp ns --cpp-profile std --outdir out   # ns holds Bar.1.0 only
$ grep -oE '^struct [A-Za-z0-9_]+' out/p/ns/Bar_1_0.hpp
struct Bar
$ # someone adds ns/Bar.2.0.dsdl, and regenerates
struct Bar_1_0
```

No alias is emitted, so every caller of `p::ns::Bar` stops compiling the day a second version lands
in the namespace, with nothing to say why. The unresolved-reference path two functions below
(`CppEmitter.cpp:232-234`) appends the version unconditionally, so a resolved single-version
reference and an unresolved one already disagree.

**obj-cpp never includes it**, `cppTypeNameFromInfo` being the bare projection of the short name. Two
versions give one `struct Bar` in the `abi` namespace, and the staged C headers it includes carry C's
collision as well:

```console
$ dsdlc --target-language obj --target-endianness little --obj-abi-language cpp ns --outdir out
$ grep -h '^struct' out/abi/p/ns/Bar_?_0_abi.hpp
struct Bar
struct Bar
```

**Fix.** Needs a decision, and it is the same one [D5](#d5) turns on. Versioning all three
unconditionally is the option that makes the six agree and removes both the collision and the
instability; it is an ABI break for C and C++, mitigable with a `using`/`typedef` alias for the
newest version of each type. Whatever is chosen, `cppTypeName`'s dependence on the compiled set
should not survive it: a generated identifier that changes when an unrelated file appears is not one
a caller can write against.

Where the decision is *made* is root cause [D](#d-composed-names-are-spliced-at-the-call-site). Three emitters answer this
question today because there is no one place that holds it; fixing the three in place would leave
the fourth copy free to drift, which is how [D18](#d18) happened.

The compile gate would not have caught any of this — it compiles each header standalone and the
corpus has no type with two versions.

### D21

**The manifest reports an encoded C/C++ file stem that is not the file on disk.**
Medium · `lib/Support/NamingPolicy.cpp` (the reserved-namespace stage)

Found while consolidating the file-stem producers under root cause
[D](#d-composed-names-are-spliced-at-the-call-site). The C and C++ emitters build a header name from
the raw DSDL short name; the manifest builds it with `renderVersionedFileStem`, which projects under
`IdentifierRole::FileStem`. Those agree for every name except one in a reserved namespace, because
the projection ends with the reserved-namespace encoder and the emitters never ran it.

```
ns/__Foo.1.0.dsdl
```

```console
$ dsdlc --target-language c ns --outdir out --naming-manifest n.json --encode-reserved-identifiers
$ ls out/p/ns/
__Foo_1_0.h  __Foo_1_0.c
$ python3 -c "import json;print(json.load(open('n.json'))['languages']['c']['p.ns.__Foo.1.0']['file_stem'])"
zX005FzX005FFoo_1_0
```

A build rule reading the manifest to find the header looks for a file that is not there. §9 of the
design record claims `file_stem` is exact for all six.

**Fixed.** The reserved-namespace stage no longer runs for `IdentifierRole::FileStem`. A file name is
not an identifier — `__Foo_1_0.h` sits in no namespace C or C++ reserves — so the encoding was
repairing a hazard that does not exist there, and the emitters were right to skip it. Four rows of
`naming-roles.txt` move, all in the `file_stem` column, all for names that start with an underscore.

---

### D22

**Under `--versioned-type-names` the C header and the C implementation name different types.**
High · `lib/CodeGen/CEmitter.cpp` (the restamp on the backend's own clone)

Found by the [Phase 3](#running-both-schemes) lane that runs the naming corpus under both spellings.

The [D18](#d18) arrangement is that lowering writes the unversioned C names -- it does not know the
backend's naming options, and one module serves both the C and object backends -- and the C backend
restamps them on its own clone. The restamp covered member names, which is what motivated it, and
not the type names sitting beside them, which are stamped by exactly the same code for exactly the
same reason. So the header, rendered directly by the emitter, declared `Foo_1_0`, while the
implementation, rendered through EmitC from the MLIR attributes, defined `Foo`.

```console
$ dsdlc --target-language c --versioned-type-names test/lit/fixtures_naming --outdir out
$ cc -std=c11 -I out -c out/fixtures_naming/naming/Arrays_1_0.c -o /dev/null
out/fixtures_naming/naming/Arrays_1_0.c:29:8: error: conflicting types for
    'fixtures_naming_naming_Arrays_1_0__serialize_ir_'
out/fixtures_naming/naming/Arrays_1_0.h:51:8: note: previous declaration is here
```

Every generated C translation unit failed to compile, in every corpus. It was invisible because
nothing generated C with the flag: the flag was new, and the lanes that exercise C were all on the
default.

**Fixed.** The restamp now covers `c_type_name` on the schema and on each plan,
`c_serialize_symbol`, `c_deserialize_symbol`, and `composite_c_type_name` on each io op. The
composite name is re-rendered from the reference's own identity -- the io op already carries
`composite_full_name` and its two version numbers -- rather than by patching the string lowering
left, so it goes through `renderDefinitionTypeName` like every other type name.

The general lesson is the one [§D](#d-composed-names-are-spliced-at-the-call-site) is about: a name
that is composed in two places will disagree in one of them, and the disagreement waits for whichever
option nobody had tried.

---

### D23

**The object backend accepts `--versioned-type-names` and ignores it.**
High · `lib/CodeGen/ObjectEmitter.cpp`

Found by auditing which structs declare `typeNameVersioning` against which ones read it, after
[Phase 3](#running-both-schemes) landed.

The driver sets the option on the obj backend like the other seven. The obj backend drives the C
emitter for its staged headers and, in the obj-cpp lane, the C++ ABI emitter, and forwarded neither.
Both stages ran at the default whatever the user asked for.

```console
$ dsdlc --target-language obj --versioned-type-names --target-endianness little src --outdir out
$ grep -o 'src__vv__T[A-Za-z0-9_]*' out/src/vv/T_1_0.h | head -1
src__vv__T
```

The headers this backend publishes are the archive's interface, so a caller writes these names. The
backend does deliberately opt out of one C option -- deprecation attributes, because the C it stages
is an intermediate nobody sees -- and this was not that: the names are published.

Worse than a silent no-op, it made the feature look tested. `RunObjCppBackendSmoke.cmake` passed the
flag while its harness named unversioned types, and passed *because* the flag did nothing. A survey
of which lanes pass the flag therefore counted obj as covered under both schemes when it was covered
under neither.

**Fixed.** Both stage option structs forward it. Both obj smokes are parameterised and registered
under each scheme, so the forwarding is now what the test depends on.

---

### D24

**Two different types whose generated names collide are accepted when their versions differ.**
High · `lib/Frontend/Discovery.cpp` (the output-name collision key)

Found while fixing [D5](#d5), by asking what the collision key should be once the version is no
longer part of the type name.

The existing check keys each name on `<language>:<role>:<path><name>:<major>:<minor>`. That was right
when every backend put the version in the type name: two names could only meet if their versions
matched. Under the unversioned default the version is not in the identifier, so two *different* DSDL
types whose names project onto one identifier collide whatever versions they carry -- and the key's
version component made the check blind to exactly that.

```console
$ ls ns
FooBar.1.0.dsdl  Foo_bar.2.0.dsdl        # distinct types; distinct file stems, so only the type name meets
$ dsdlc --target-language go ns --outdir out --go-module m
  files generated: 4                     # accepted
$ grep -h '^type ' out/*/ns/foo_bar_*_0.go
type FooBar                              # in package ns
type FooBar                              # also in package ns -- does not compile
```

At the same version the pair is rejected, which is what made this look covered.

**Fixed** with [D5](#d5), by the same change: the new check keys on the identifier as emitted, which
carries the version only when the scheme puts it there. Two versions of *one* definition are
excluded by comparing owners rather than by the key, so [D20](#d20)'s case is still left to the
sentinel and the Go refusal, and a multi-version corpus still generates.

---

## The D18 fork

Three ways to make the C declaration and the C serialiser agree.

1. **Lower through the engine.** `LowerToMLIR` calls `codegenProjectIdentifier(C, FieldName, …)`
   instead of its own sanitiser. Closes D18. Does not close A for C, because the scope repair still
   is not visible to lowering — unless the section scope moves too, and `makeSectionFieldScope` lives
   in `CodeGen`, which depends on `Lowering`. Moving it to `Support` needs `Support` to see
   `SemanticSection`, which today it does not.
2. **Stop using `c_name` in the C path.** The C emitter supplies member names to the EmitC
   conversion, so one implementation produces both. Largest change, and the cleanest end state.
3. **Reject rather than repair, for C, C++ and Rust.** Treat an in-scope field collision in those
   three as an error, as cross-type collisions already are. Needs no agreement between the two
   implementations, because no repair happens. Costs consistency: Go, TypeScript and Python would
   still repair silently, and the same DSDL would be accepted for one backend and rejected for
   another.

Option 2 is the right end state and option 3 is the cheapest correct one. Option 1 alone fixes the
escape hatch without fixing A.

## Deferred: the two that turn on a versioning scheme

[D20](#d20) is not fixed. ([D5](#d5) was grouped with it until it turned out that newest-only cannot
reach it and `--versioned-type-names` already does; it is done. [D19](#d19) was grouped with them
until its fix turned out to be independent of the versioning question; it is done too.) The mechanism they need landed with root
cause [D](#d-composed-names-are-spliced-at-the-call-site), and the two modes described below are now
in the tool. What is still open is the policy: which versions of a type reach the generated corpus at
all.

The design, which is what shipped: two modes rather than one rule, because whether a type name
carries its version is a property of *the consuming code*, not of the corpus. Code that speaks one
version of a type reads better with `p::ns::Bar`; code that deliberately handles two versions side by
side needs `Bar_1_0` and `Bar_2_0` to keep them apart in its own source. So: unversioned by default,
a flag for always-versioned, and in both the name is a function of the definition alone. C++'s
previous "version only when the corpus is ambiguous" is deleted rather than kept, since it made the
identifier depend on what else was in the invocation.

What "unversioned" costs is not uniform, because what scopes a generated type differs:

| Language | Scope holding the type | Two versions, unversioned |
|---|---|---|
| Rust, TypeScript, Python | module per type *and version* | safe |
| C, C++ | global / namespace shared by versions | safe to generate; breaks only if a consumer includes both in one translation unit |
| Go | package per namespace, shared by versions | cannot be generated -- the package will not compile |

Go is what stalls the *default*. The standard `uavcan` corpus has 20 full names with more than one
version, so an unversioned default makes `--target-language go` fail on the most common input until
the flag is passed. It fails with a diagnostic naming the type, the versions and the flag rather than
by emitting a package that will not compile -- but it still fails, and that is the cost the
newest-version-only feature is meant to remove.

**The way out for D20 is a separate feature: generate the newest version of each type by default.**
That removes the collision at its source for every language rather than working around it per
backend, and it changes what D20 should do -- so settling it first would be settling it against a
corpus shape that is about to change. It waits for that.

**[D5](#d5) does not, and cannot.** Grouping the two here was wrong. D5 is a service section
colliding with a *sibling type*, not with another version of itself:

```console
$ dsdlc --target-language cpp ns --cpp-profile std --outdir out   # ns/Foo.1.0.dsdl is a service,
$ grep -rhE '^struct Foo_Request' out | sort | uniq -c              # ns/Foo_Request.1.0.dsdl a message
   2 struct Foo_Request {
   2 struct Foo_Request;
$ dsdlc --target-language cpp --versioned-type-names ns --cpp-profile std --outdir out2
$ grep -rhE '^struct Foo[A-Za-z0-9_]*' out2 | sort -u
struct Foo_1_0_Request;
struct Foo_Request_1_0;
```

Both definitions are the newest of their own name, so newest-version-only leaves both in place and
the collision with them. What *does* separate them is `--versioned-type-names` -- D5's fix option 2,
now available. So D5 is fixed under the versioned scheme and live under the default, which is the
scheme most users get. Option 1 (register `<Type>_Request` and `<Type>_Response` in the collision
keyspace) is the remaining candidate, and it is independent of the newest-only work.

### The modes are landed (2026-08-25)

`--versioned-type-names` exists and `TypeNameVersioning::Unversioned` is the default. What follows is
the record of the attempt that was reverted first, and of what the second attempt cost, because the
gap between the two estimates is the useful part.

#### The first attempt, and why it was reverted (2026-08-24)

The two modes were built end to end and then reverted, deliberately. Everything worked: a
`--versioned-type-names` flag threaded from the driver into all eight backends, lowering left at the
unversioned default with the C emitter restamping `c_type_name` on its own clone (the [D18](#d18)
arrangement), a C/C++ per-type sentinel that turns a two-version include into one `#error` naming the
flag, and a Go generation-time refusal. Versioned mode reproduced the existing Rust and Go output
byte-for-byte, which is the check that the threading was faithful.

It was reverted for one measured reason: **a uniform unversioned default fails 113 of 206 tests**,
and the cause is structural rather than a matter of updating expectations.

Every cross-language parity harness generates C *plus one other language* and names types in both
halves. Those halves disagree today by design -- C is unversioned, the other five are versioned --
and the harnesses are written against exactly that split. So neither setting of one global flag
satisfies them: the default keeps the C half working and breaks the other, and the flag does the
reverse. That is not a fixture problem; it is the same inconsistency §D describes, showing up in the
test suite instead of the emitters.

The counts, for whoever picks this up: 31 cross-language parity harnesses, 19 TypeScript runtime
smokes, 11 Python runtime tests, the three decoder fuzzers, the Go build/determinism/generation
lanes, `llvmdsdl-lit`, and the Python unit tests.

An earlier estimate of this cost was wrong and worth recording as a trap. Measuring *generated files
that change* gives 36 of 38 in Rust and Go and 7 of 38 in C++ -- which understates the work by an
order of magnitude, because the cost is not in the generated tree but in the hand-written code that
references it.

#### What the second attempt actually cost (2026-08-25)

The 113 figure was real but it was a measure of *coupling*, not of work. Parameterising the harnesses
first -- one token per version pair, `@CV1_0@` for C names and `@V1_0@` for the other five, resolved
by `cmake/HarnessTypeNameTokens.cmake` -- took the residue down to **26 tests**, and those fell into
four groups, only one of which was a real defect:

1. **Go on a multi-version corpus (9 tests).** The refusal is correct behaviour, not a failure: the
   `uavcan` corpus has 20 full names at two or more versions and Go compiles a namespace as one
   package. Those lanes ask for `--versioned-type-names` because it is the only scheme that can
   express the corpus. Each Go script now derives the flag from the same variable that sets its
   tokens, so the generator and the harness cannot disagree about a spelling.
2. **Harnesses written against versioned names whose generator lost the flag (8 tests).** One line
   each.
3. **`llvmdsdl-lit` (8 checks in one test).** Expectations and two golden snapshot sets, updated to
   the new default. This is where the default belongs pinned.
4. **A genuine gap in the Phase 1 parameterisation (the rest).** The tokeniser matched
   `Name_1_0` only at a word boundary, so every identifier that *infixes* the version was silently
   left literal: service sections (`ExecuteCommand_1_3_Request`) and Go's derived constants
   (`INT3SAT_1_0_SERIALIZATION_BUFFER_SIZE_BYTES`). 176 occurrences across four harnesses. Worth
   recording as the trap it is -- a regex anchored on the common case passes Phase 1's
   inertness gate precisely because it changed nothing, and the omission only surfaces when the mode
   flips.

The lesson for the next scheme change: the cost of a naming default is dominated by hand-written
code that names generated types, and the way to find all of it is to flip the mode and read the
compiler errors, not to grep.

Two consequences for the eventual design:

- Newest-version-only would shrink this dramatically. With one version of each type in the corpus,
  the C and C++ sentinels never fire, Go never refuses, and the harnesses stop straddling two naming
  conventions.
- If the modes land before that, scoping the default to C, C++ and obj-cpp costs no test churn at
  all -- C is already unversioned and the other four are untouched -- while still fixing every defect
  that exists. Uniformity across all six is the expensive half, and it buys consistency rather than
  correctness.

### Running both schemes

Both spellings are now exercised deliberately rather than whichever one happens to be the default.
Not by doubling the suite: three second registrations, chosen so that every language is compiled and
run under both.

| Lane | Covers |
|---|---|
| `llvmdsdl-naming-corpus-compile-gate-versioned-names` | all six languages over the adversarial naming corpus, under `-Werror` |
| `llvmdsdl-uavcan-cpp-c-parity-versioned-names` | C and C++ |
| `llvmdsdl-uavcan-c-rust-parity-versioned-names` | C and Rust |
| `llvmdsdl-obj-backend-smoke-versioned-names` | obj's published C headers |
| `llvmdsdl-obj-cpp-backend-smoke-versioned-names` | obj-cpp's published C++ headers and C shim |

Rust, Go, TypeScript and Python already ran under both, on lanes that predate the modes. C and C++
ran under neither -- every lane that touched them was on the default -- which is what let
[D22](#d22) sit undetected, and obj *appeared* to run under both only because it was ignoring the
flag ([D23](#d23)).

Each harness script now settles its scheme in one call, `llvmdsdl_harness_naming_scheme`, which
defines the substitution tokens *and* the dsdlc flags from one variable. Before, a script set them
separately, and the two ways of getting them out of step -- a token family expanded for one scheme
against a generator invoked for the other, and a scheme block placed after the generation it was
meant to govern -- both happened, more than once.

The scheme variables are settable per `add_test`, so covering a further axis is a registration rather
than a second copy of a harness.

## Suggested order

0. ~~**[D18](#d18)** — settle the fork above.~~ Settled as option 2 and landed.
1. ~~**[A](#a-three-emitters-have-no-field-scope)** — D1, D8, D11.~~ Landed in all three backends.
2. ~~**[B](#b-the-claimed-name-tables-were-built-from-one-plain-type)** — D2, D3, D10, D12, D13.~~
   Landed, by generating a union, a service, an array-bearing message and a PMR type in all six
   backends and reading what came out.
3. ~~**D9** and **D14** together.~~ Landed with B: the compile gate's C++ leg was passing the flag
   that catches D9 to nothing, and turning it on made the corpus fail to build.
4. ~~**[C](#c-c-claims-nothing-on-a-premise-that-is-false)** — D4, D7, D17.~~ Landed.
5. ~~**D6**, **D15**, **D16** — driver fixes, independent of the rest.~~ Landed.
6. **D5** and **D20** — deferred; see below.

Extend `test/lit/fixtures_naming` with a union, a service, a PMR-relevant type and the
trailing-underscore constants, so the corpus compile gate covers the shapes the tables were derived
from. Regenerate both goldens once, at the end.

`Stropped`, `MemberPool`, `Choice`, `Call`, `ClaimedUnion` and `Arrays` are in the corpus, and
`Claimed` now carries a field for every claimed member rather than only the metadata constants: the
keyword-strop collision in a plain struct, in a union, and in both sections of a service; the
field/constant collision from [D11](#d11); the union claims where they occur; and both array-metadata
collisions; and `TrailingUnderscores` and `TrailingUnion` for the C macro spellings. The gate
compiles the corpus under the PMR profile as well as `std`. Everything the plan called for is in.
