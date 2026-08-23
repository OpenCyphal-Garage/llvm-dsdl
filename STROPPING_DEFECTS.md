# Identifier naming defects

Eighteen defects in the identifier-naming subsystem. Seventeen were found by review of the unmerged range
`f8c2fad..docs/roadmap-g8-scorecard`; [D18](#d18) was found while preparing the fix for root cause
[A](#a-three-emitters-have-no-field-scope), in a file no reviewer had been given. Each is reproduced
against a built `dsdlc`; the transcripts are the generated output quoted under each entry.

Design record: [identifier-stropping.md](docs/development/identifier-stropping.md).

Twelve of the eighteen share three root causes (§A, §B, §C). Fixing those closes D1–D4, D6–D9 and
D11–D14.

**Fixed so far:** [D18](#d18) (fork option 2), root cause [A](#a-three-emitters-have-no-field-scope)
in all three backends, and with it [D1](#d1), [D8](#d8) and [D11](#d11).

| ID | Severity | Area | Defect |
|---|---|---|---|
| [D1](#d1) | High | engine + emitters | ~~C, C++ and Rust name fields without a scope, so distinct DSDL fields emit one duplicate member~~ |
| [D2](#d2) | High | engine | `UNION_OPTION_COUNT` is missing from the claimed-name tables |
| [D3](#d3) | High | engine | Go's union `Tag` field is missing from the claimed-name table |
| [D4](#d4) | High | engine + C | C claims no names on a false premise about trailing underscores |
| [D5](#d5) | High | C++ | `<Service>_Request` collides with a sibling type and Discovery cannot see it |
| [D6](#d6) | High | driver | `obj` writes only the C half of the naming manifest |
| [D7](#d7) | High | tests | The claimed-name test pins the false premise from D4 |
| [D8](#d8) | High | docs | ~~The design record and a golden both describe repair that does not happen~~ |
| [D9](#d9) | Medium | engine | The scope's `_2` suffix bypasses the reserved-namespace encoder |
| [D10](#d10) | Medium | engine | PMR members are missing from the claimed-name table |
| [D11](#d11) | Medium | C++ | ~~Fields and constants use separate pools but share one C++ scope~~ |
| [D12](#d12) | Medium | C/C++ | Array-metadata names are projected outside every scope |
| [D13](#d13) | Medium | obj-cpp | `to_c`/`from_c` are missing from the claimed-name table |
| [D14](#d14) | Medium | golden | `naming-roles.txt` pins a C++ identifier containing `__` |
| [D15](#d15) | Medium | driver | `obj` reports reserved-identifier errors twice, verbatim |
| [D16](#d16) | Medium | driver | `--naming-manifest` writes during `--dry-run` |
| [D17](#d17) | Low | docs | The C emitter comment repeats the D4 premise |
| [D18](#d18) | High | lowering | ~~A sixth copy of the naming policy in `LowerToMLIR` makes `--encode-reserved-identifiers` emit C that does not compile~~ |

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

### C. C claims nothing, on a premise that is false

The C arm returns an empty claimed-name set, justified in `NamingPolicy.cpp:369-371` and again in
`CEmitter.cpp:458-459` by: the generated metadata macros carry a trailing `_`, which no projection of
a DSDL name produces.

The C `ConstantName` policy is `kMacroToken` — preserve case, escape, upper-case, **no strop**. It
passes a source name's own trailing underscore straight through. `isReservedIdentifier` rejects only
names that both start *and* end with `_`, so `full_name_` is conformant DSDL and projects to
`FULL_NAME_`.

Closes D4, D7, D17.

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

### D5

**`<Service>_Request` collides with a sibling type and Discovery cannot see it.**
High · `lib/CodeGen/CppEmitter.cpp:1975`, `lib/CodeGen/CppObjectAbiEmitter.cpp:361`

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

**Fix.** Needs a decision. The suggested mechanism: have a service register `<Type>_Request` and
`<Type>_Response` as additional output names in the same collision keyspace, so the pair is rejected
by the existing path. The alternative — reverting to a separator no DSDL name can produce — reopens
the `-Wreserved-identifier` problem the rename solved.

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

### D7

**The claimed-name test pins the false premise from D4.**
Medium-as-written, High-in-effect · `test/unit/NamingPolicyTests.cpp:211` · root cause [C](#c-c-claims-nothing-on-a-premise-that-is-false)

The case is commented "Not claimed: C metadata macros carry a trailing underscore, so nothing needs
escaping" and exercises `FULL_NAME`, which genuinely needs no escape. The reachable collision needs
`FULL_NAME_`, which the test does not cover, so the suite cannot fail on D4. The golden corpus
contains trailing-underscore names (`Foo_`, `Break_`) but none matching a metadata macro.

**Fix.** With D4: add `FULL_NAME_` and `ZOH_ALIAS_ELIGIBLE_` cases, and extend the golden corpus.

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

### D13

**`to_c`/`from_c` are missing from the claimed-name table.**
Medium · `lib/CodeGen/CppObjectAbiEmitter.cpp` · root cause [B](#b-the-claimed-name-tables-were-built-from-one-plain-type)

The obj-cpp canonical struct emits these conversion helpers in the same scope as fields. Both are
conformant DSDL field names.

**Fix.** Add to `kCppMembers` with the other omissions.

### D14

**`naming-roles.txt` pins a C++ identifier containing `__`.**
Medium · `test/unit/golden/naming-roles.txt:457`

The golden records `struct_body cpp: break_ → break__2` as correct, which freezes D9's output as
expected behaviour. Whatever fix D9 takes, this row moves; until then the suite cannot flag it.

**Fix.** Regenerate with D9.

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

### D17

**The C emitter comment repeats the D4 premise.**
Low · `lib/CodeGen/CEmitter.cpp:458-459` · root cause [C](#c-c-claims-nothing-on-a-premise-that-is-false)

`emitSectionConstants` explains that its scope reserves no metadata names because "they carry a
trailing `_` … which no projection of a DSDL name produces". Same false statement as
`NamingPolicy.cpp:371`, in the code a reader checks first.

**Fix.** Correct with D4.

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

## Suggested order

0. ~~**[D18](#d18)** — settle the fork above.~~ Settled as option 2 and landed.
1. ~~**[A](#a-three-emitters-have-no-field-scope)** — D1, D8, D11.~~ Landed in all three backends.
2. **[B](#b-the-claimed-name-tables-were-built-from-one-plain-type)** — D2, D3, D10, D12, D13. Derive
   the tables by generating a union, a service, and a PMR type rather than by reading one plain
   message.
3. **[C](#c-c-claims-nothing-on-a-premise-that-is-false)** — D4, D7, D17.
4. **D9** and **D14** together — the encoder fix and the golden it moves.
5. **D6**, **D15**, **D16** — driver fixes, independent of the rest.
6. **D5** last: it needs a mechanism decision, not an implementation.

Extend `test/lit/fixtures_naming` with a union, a service, a PMR-relevant type and the
trailing-underscore constants, so the corpus compile gate covers the shapes the tables were derived
from. Regenerate both goldens once, at the end.

`Stropped`, `MemberPool`, `Choice` and `Call` are in the corpus already — the keyword-strop collision
in a plain struct, in a union, and in both sections of a service, plus the field/constant collision
from [D11](#d11). A PMR-relevant type and the trailing-underscore constants are still to come, with
[B](#b-the-claimed-name-tables-were-built-from-one-plain-type) and
[C](#c-c-claims-nothing-on-a-premise-that-is-false).
