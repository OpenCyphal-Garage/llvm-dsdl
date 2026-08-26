# Identifier naming

Every backend turns DSDL names into identifiers the target language accepts. One engine does that
for every backend, and the frontend's output-name collision check consumes the same engine rather than
approximating it.

This is the as-built record. The user-facing description is in
[the backend reference](../reference/codegen/backends.md).

---

## 1. Terms

Three transformations get conflated under "stropping", and the design separates them:

1. **Case projection** — `flight_control_mode` becomes `FlightControlMode` for Go and
   `FLIGHT_CONTROL_MODE` for a C macro. Cosmetic, and many-to-one.
2. **Escaping** — making a name the language will accept: keywords, reserved namespaces, names the
   generated code has already taken.
3. **Disambiguation** — repairing the collisions the first two introduce.

Nunavut ([`3.0.preview`](https://github.com/OpenCyphal/nunavut/tree/3.0.preview)) implements the
second thoroughly and configurably, the first implicitly per-template, and the third not at all: two
distinct DSDL names can land on one identifier with no diagnostic. Where a choice here is arbitrary
it follows nunavut, so that a reader who knows one tool can predict the other; §4.1 and §5.2 record
where it deviates and why.

---

## 2. DSDL guarantees

The reachable input set is small, and pinning it down removes most of the machinery a general-purpose
identifier encoder would need.

A name reaching a backend has passed `isValidNameComponent` and
[`isReservedIdentifier`](https://github.com/OpenCyphal-Garage/llvm-dsdl/blob/main/lib/Support/ReservedIdentifiers.cpp),
so it:

- matches `[A-Za-z_][A-Za-z0-9_]*` — ASCII only, never starting with a digit;
- does not match `_.*_$`, `u?int\d*`, `float\d*`, `void\d*`, `q\d+_\d+`, `com\d`, `lpt\d`;
- is not one of `truncated saturated true false bool byte utf8 optional aligned const struct super
  template enum self and or not auto type con prn aux nul` (case-insensitively).

**Character escaping is unreachable.** No valid DSDL name contains a character needing an escape, and
none starts with a digit. The escape path exists anyway, specified and tested: `dsdlc` is a library
and the language server feeds it half-typed text.

**The reachable keyword set is computable.** DSDL already reserves `const struct enum self and or not
auto type template super bool true false`, so those never collide. What remains, per language, for a
role projecting to snake_case:

| Language | Reachable keyword collisions | Examples |
|---|---:|---|
| C | 27 | `break` `default` `register` `switch` `typedef` `while` |
| C++ | 81 | `class` `namespace` `operator` `public` `this` `throw` `typename` |
| Rust | 42 | `fn` `impl` `let` `loop` `match` `mod` `move` `mut` `pub` `ref` `use` |
| Go | 22 | `chan` `defer` `fallthrough` `func` `go` `interface` `map` `package` `range` |
| TypeScript | 47 | `class` `delete` `export` `function` `interface` `new` `null` `string` |
| Python | 31 | `def` `del` `elif` `except` `lambda` `pass` `raise` `yield` |

For roles projecting to PascalCase the set collapses to one across every language — Python's `None`. Role
awareness is therefore not a micro-optimisation: it is the difference between escaping 81 names and
escaping one.

---

## 3. Roles and scopes

Two concepts, shared by every language.

A **role** is what the identifier will be used as. This is nunavut's `id_type` with two corrections:
it is exhaustive, and it is mandatory. Nunavut defines roles but defaults every call site to the
union of all of them, which is why its C backend escapes a struct member named `memset` that no C
rule reserves.

```
enum class IdentifierRole {
    TypeName, FieldName, ConstantName, FunctionName,
    LocalName, NamespaceName, FileStem, MacroName,
};
```

A **scope** is a region in which identifiers must not collide: a struct body, a namespace directory,
a module's top level. `NamingScope` takes names in declaration order and appends `2`, `3`, … when a
projection is many-to-one, joined with a `_` except where the base already ends in one — `break_`
disambiguates to `break_2`, not `break__2`, which C and C++ reserve. The suffix is appended after the
pipeline has run and is the one part of an identifier the reserved-namespace stage never sees, so
avoiding the namespace is this rule's job rather than that stage's. Roles share one pool within a scope, because they share one scope in the
language: a Go field and a Go method on the same struct cannot both be `Serialize`.

`makeSectionFieldScope` and `makeSectionConstantScope` build the scopes a section needs. How many
that is depends on the language: a C++ struct body holds the fields and the constants, so both
helpers return one shared scope there, while the other five put constants where a field cannot reach
them and get a pool each. Every emitter and the naming manifest go through these, which is what makes
the manifest a report of what a backend writes rather than a second opinion about it.

### 3.1 One name, and the names built from a definition

The engine above answers how *one* name is spelled. A definition also has names built from its parts
-- its type name, its output file stem, its include guard, its linkage symbol base -- and those are
composed rather than projected. `Support/DefinitionNaming.h` answers them, from a per-language table
of the same shape as the role table: how the namespace is joined into the type name, whether the
version is part of it, and whether the composed result is re-projected.

It takes parts rather than a `DiscoveredDefinition` because `llvmdsdlFrontend` links only
`llvmdsdlSupport`, and `Discovery` -- which has to agree with the emitters about what they will emit
-- is in the frontend. `CodeGen/DefinitionPathProjection.h` holds the overloads that take the richer
types and delegates to it.

Before this existed each emitter composed its own. That is how `mangleSymbol` came to exist three
times verbatim across three libraries, with the C backend linking only because all three agreed with
their consumer by coincidence of identical source text; and how C, C++ and the object backend arrived
at three different answers to whether a type name carries its version.

The C backend is the one place where a scope crosses a layer. Its struct declaration reads the scope
directly; its serializer bodies are emitted from MLIR by `convert-dsdl-to-emitc`, which reads member
names from the `c_name` attribute. Lowering fills that attribute with the unscoped projection, and
the C emitter stamps the scoped name over it on its own clone of the schema before the conversion
runs -- so the declaration and the references cannot disagree, and hand-driven `dsdl-opt` runs still
have a name to work with.

---

## 4. The pipeline

One implementation, shared by every language and role:

1. **Project** — apply the role's case style.
2. **Escape** — replace characters outside `[A-Za-z0-9_]`, prefix a leading digit. Unreachable from
   valid DSDL (§2).
3. **Strop** — append `_` if the result is a keyword.
4. **Upper-case** — for constants and macros.
5. **Claimed names** — append `_` if the generated code or the language runtime already owns the
   result (§5.2).
6. **Reserved namespaces** — encode the underscores that put the result in a namespace C or C++
   reserves (§5.3).

Stage order carries meaning. Keywords are checked before the upper-casing because they are a property
of the cased identifier: a Go constant named `break` becomes `break_` and only then `BREAK_`. Claimed
names are checked after it, because they are a property of the finished identifier: a Go constant
named `full_name` is emitted as `FULL_NAME`, which is the spelling that has to miss the metadata
constant.

Stages 1 and 2 are many-to-one; the scope is what restores injectivity. Stated that way the invariant
is testable, and §7 tests it: for any set of distinct DSDL names in one scope, the map to identifiers
is injective.

### 4.1 A trailing `_`, in every language

Nunavut strops with a leading `_` in C and C++ and a trailing `_` in Python. The leading form is the
source of its worst behaviour: `_` before a capital is itself reserved in C and C++, so stropping
produces an illegal identifier and nunavut needs a per-language failure handler that lower-cases the
following character — `EMACRO_TOKEN` becomes `_eMACRO_TOKEN`. That handler is many-to-one and nothing
repairs it, so `_Foo`, `__foo` and `_foo` all become `_foo` in its C backend.

A trailing `_` cannot enter a reserved namespace in any target and needs no failure
handler.

**It terminates in one pass.** Appending `_` to `X` can only need a further escape if `X_` is itself
something to escape from — another keyword, another claimed name, or a reserved namespace. It never
is. That is a property of the tables rather than of the algorithm, so §7 asserts it over every entry
in every table.

The tempting shorthand for this — "no entry in any table ends in `_`" — is sufficient but not true.
C's claimed names are its metadata macros, which are spelled with a trailing `_` and have to be
claimed as spelled; `FULL_NAME_` escapes to `FULL_NAME__`, which is claimed by nothing and reserved
in neither C nor C++, C reserving only a *leading* `__`.

The argument covers suffixes only. A trailing `_` cannot repair a *prefix* violation — `__bar` is
reserved for any use, and `__bar_` is just as reserved — which is why reserved namespaces are encoded
instead (§5.3).

Rust's `r#` raw identifiers were considered and rejected: `self`, `Self`, `crate` and `super` cannot
be raw, so Rust would need the trailing form regardless, and two escape mechanisms in one language
buy nothing.

---

## 5. Policy tables

One `LanguageNamingPolicy` per language, data only:

| Field | Contents |
|---|---|
| `caseByRole` | role → Snake / Pascal / Preserve, plus escape, strop and upper-case flags |
| `keywords` | hard-reserved in every role |
| `runtimeOwned` | names already taken in the scope, by the generated code or the language runtime |

### 5.0 C escapes against C++'s keywords too

Generated C is compiled as C++ more often than not. The object backend does it to its own staged
headers, and the `c_shim` header it publishes is a dual-language surface -- written C-clean, compiled
as C by the test suite and as C++ by the lane. `extern "C"` changes linkage, not tokenization, so a
member named `class` is a parse error there whatever the linkage says.

So C's keyword set is the union of C's and C++'s. The cost is a trailing `_` on the DSDL names that
are C++ keywords and not C ones -- `class`, `new`, `operator`, `export` and their kin -- in C field
and type names. Macro constants are unaffected: `ConstantName` in C is a macro token, which is never
stropped, and always carries a `<Type>_` prefix.

The alternative, escaping only in the object lane, was rejected: that lane publishes the plain C
header too, so one DSDL type would get two different published C headers depending on which lane
produced it.

### 5.1 Role policies are per-language and uneven

The three flags are separate from the case style because the backends use every combination: a C
macro token is escaped but not stropped, a C header stem is neither, and a Go constant is both and
then upper-cased. A macro is not an identifier in C's namespace and carries its type name as a
prefix, so `<Type>_BREAK` is fine; a header stem is a file name, not an identifier at all.

### 5.2 Claimed names

`runtimeOwned` holds two kinds of name, both of which a DSDL attribute must miss because something
else in that scope answers to them already.

Most are emitted for every type: the seven per-type metadata constants (`FULL_NAME`,
`FULL_NAME_AND_VERSION`, `IS_DEPRECATED`, `EXTENT_BYTES`, `SERIALIZATION_BUFFER_SIZE_BYTES`,
`ZOH_ALIAS_ELIGIBLE`, `ZOH_ALIAS_REASON`) and the generated method names in Go, C++ and Python.
TypeScript's `constructor` and `prototype` are the other kind, claimed by the language runtime rather
than by anything we write.

The rest are emitted only for some shapes — `UNION_OPTION_COUNT` and Go's `Tag` for a union, the
memory-resource pair under the PMR profile, `to_c` and `from_c` by the object backend — and are
claimed for every type regardless. A member name that changed with `--cpp-profile`, or with a later
revision of a type becoming a union, would be an ABI that depends on how the generator was invoked
rather than on the DSDL.

Each entry exists because the case without it did not compile:

| Case | Backend | Without the escape |
|---|---|---|
| DSDL constant named after a metadata constant | C++, Go, Rust | duplicate declaration; `go build` fails |
| DSDL field named `FULL_NAME` | C++ | data member redeclares the generated static |
| DSDL field named `serialize` | C++ | data member and member function share a name |
| DSDL constant named `UNION_OPTION_COUNT`, in a union | C++, Go, Rust | duplicate declaration |
| DSDL field named `tag`, in a union | Go | exported field declared twice |
| DSDL field named `set_memory_resource` | C++ (PMR) | data member and member function share a name |
| DSDL field named `to_c` | obj (C++ ABI) | data member and member function share a name |

The list is derived from what the backends emit for a union, a service, an array-bearing message and
a PMR type, in every language. It was first derived from one plain message, which is why the four
shape-specific groups above were absent from it for as long as they were.

C claims the same list spelled the way it emits it, with the trailing `_`. The trailing underscore
is worth copying into any backend that grows a new generated name, but it separates the generated
names from *most* DSDL names rather than from all of them: `ConstantName` in C is a macro token —
preserve case, escape, upper-case, no strop — so it passes a source name's own trailing underscore
straight through, and DSDL reserves only names that both start and end with one. `full_name_` is a
conformant DSDL constant that reaches `FULL_NAME_`, and `<Type>_ZOH_ALIAS_ELIGIBLE_` is tested by the
generated `try_deserialize_view_` in an `#elif`, so redefining it changes what the generated code
does. `ConstantName` and `MacroName` are one thing in C and are claimed alike.

TypeScript and Python constants need nothing, prefixing theirs with `DSDL_` while DSDL constants take
a type prefix.

This belongs to the policy rather than to a `NamingScope`. A scope reservation describes one
particular scope; these names are claimed for every type, and are needed at call sites that cannot
see a scope — C++ computes member names inside serialisation lambdas that never receive the section.
The escape is also the right shape: `Serialize_`, where a scope reservation would give `Serialize_2`,
and `_2` means "the second name competing for this identifier", which is untrue of a name no other
field wanted.

There is no `predeclared` field for the shadowable-but-legal names — Go's `len` and `cap`, Python's
`str` and `list`. Shadowing them bites only where the name is later used unqualified, which is to say
in a local or a function name, and no DSDL name reaches either: `FunctionName` has no call site, and
`LocalName` has two, both C++, both naming a token this generator built. Leaving them alone is the
point of difference with nunavut, which escapes every Python builtin in every position because its
reserved set is `keyword.kwlist + dir(builtins)` — a set that also makes its output depend on the
interpreter running the generator. A DSDL field named `str` is `str_` there and `str` here.

There is no `fileStemAvoid` field either. The MS-DOS device names are rejected by the frontend as
DSDL reserved identifiers, and stdlib shadowing is a packaging constraint rather than a naming one
(§8.2).

#### Names the table cannot hold

C and C++ give every array field two constants named after it, `<FIELD>_ARRAY_CAPACITY` and
`<FIELD>_ARRAY_IS_VARIABLE_LENGTH`. These are generated names in the same region as the DSDL
constants, but there is no fixed list of them to claim: which exist depends on the type. They are
allocated from the section's constant scope instead, declared before the DSDL constants so that a
constant yields to them rather than the other way round, and keyed on a name built from the DSDL
field name — which is what lets the scope see that two fields whose macro projections are equal
(`fooBar` and `FooBar`) want one constant.

The key carries C's trailing `_` and not C++'s absence of one, because the scope compares what is
emitted. Without it C would report a collision between a metadata macro and a DSDL constant that the
trailing `_` keeps apart, and rename a macro that was never in danger.

### 5.3 Reserved namespaces

C reserves every identifier beginning `__` or `_` plus a capital; C++ reserves those and any
identifier containing `__` anywhere. Clang's `-Wreserved-identifier` makes them errors under
`-Werror`, and it is not implied by `-Wall` or `-Wextra`.

A trailing `_` repairs none of them (§4.1), so the offending underscores are encoded as `zX005F` —
nunavut's character encoding, which is injective and therefore needs no scope to disambiguate
afterwards. The encoding is **always applied**, which keeps the projection total and its result legal
in every target.

Whether a *definition* that needed it is accepted is a separate question, asked once per run by the
driver. By default it is rejected, naming the identifier it would have produced:

```console
$ dsdlc --target-language c my_dsdl --outdir out
ns/Res.1.0.dsdl:1:1: error: field '__bar' is a reserved identifier for target language 'c';
    rename it, or pass --encode-reserved-identifiers to emit it as 'zX005FzX005Fbar'
```

Splitting it this way keeps the mode out of the pipeline: emitters never learn which is in force, and
no projection call site needs an options parameter.

The encoding applies only where a role is given. A token this generator constructed — a `--ts-module`
alias, a symbol assembled from already-projected parts, an MLIR helper binding — carries whatever
shape its emitter gave it, and encoding it would mangle a symbol that has to match something else.

**The generated code observes the same rule.** Five of its own spellings did not, and were changed:
the C++ `Type__serialize_` separator, the MLIR helper binding names, the C++ `Type__Request` service
suffix, a `#define _Static_assert` compatibility shim in the runtime header, and the
`__llvmdsdl_plan_…` prefix the transform passes give lowered helpers. The C-ABI symbols keep `__`,
which is legal in C and must match what the C emitter defines.

---

## 6. Collision detection

`Discovery` asks the engine for the `FileStem` and `TypeName` identifiers each selected backend will
use and keys on both. Keying on one is not enough: the stem folds to snake_case and keeps
underscores, the type name folds to PascalCase and drops them, so `_foo` and `foo_` take two files
and one type name — which does not compile in Go, where one directory is one package.

The two collision classes are treated differently, and the difference is principled:

- **Cross-type** — two DSDL types onto one output file or one type name: **rejected**. Repairing it
  would mean choosing which type to rename, and the only available ordering is filesystem discovery
  order, so the same sources would generate different output on different machines.
- **In-scope** — two fields onto one member name: **repaired**, plus a note. Here the ordering is
  DSDL declaration order, which is stable and part of the wire format.

One diagnostic is emitted per colliding pair, naming every language affected, because renaming one of
the two types fixes all of them at once:

```
error: type name collision in generated output: ns.Foo_bar and ns.FooBar
       map to the same output file name for target languages 'rust', 'go', 'ts', 'python'
```

A name a type does not declare can still collide. A service emits a type per section named after
itself — `Foo` gives `Foo_Request` — and a sibling definition may be *called* `Foo_Request`, which is
conformant DSDL. Neither declared name collides, so the check above cannot see it. A second pass
therefore registers each service's section names beside every declared one:

```
error: type name collision in generated output: 'ns.Foo_Request' and the request section
       of 'ns.Foo' both emit 'Foo_Request' for target language 'cpp';
       pass --versioned-type-names, or rename one of them
```

Two things about that pass are worth stating, because both were wrong in the first attempt:

- **It keys on the identifier as emitted, not on a name plus a version.** Under the unversioned
  default the version is not in the identifier, so two types collide whatever versions they carry; a
  key carrying the version misses the pair whose versions differ. Two versions of *one* definition
  are excluded by comparing owners instead: that is what `--versioned-type-names` and the generated
  include-time sentinel are for, and not this check's business.
- **It reports only where a scope is actually shared** — C's single global scope, C++'s namespace,
  Go's package. Rust, TypeScript and Python give every definition and version its own module, so a
  repeat there is unreachable rather than merely unlikely.

It runs after parsing rather than during discovery, because whether a definition is a service is a
parse result. The section suffix comes from `renderSectionTypeSuffix`, the call the emitters use, so
the check cannot compute a name different from the one written.

### 6.1 Macros stay unique by construction

C and C++ macros are global. Every generated macro is `<TypeName>_<MEMBER>`, and type names are
unique after the check above, so macro uniqueness follows. A future macro that does not carry the
type prefix is a defect rather than a style choice.

---

## 7. Tests

- **Injectivity.** Generated sets of distinct DSDL names, run through a scope for each language and
  role, asserting the map is injective. This is the invariant §4 exists to provide, and the one
  nunavut fails. The same pass asserts that no assignment lands in a reserved namespace: the
  disambiguation suffix is composed after the pipeline, so distinctness alone does not establish that
  the result is a name the program may define.
- **Table invariants.** No keyword and no claimed name in any table ends in `_`, and no keyword
  escapes onto another — asked of every entry in every table, which is what makes §4.1's
  single-pass argument sound. `LanguageNamingPolicy::keywords()` exists for this.
- **Role equivalence.** Every role is checked against the projection its call sites use. Names the
  generated code claims and names in a reserved namespace are excluded and covered separately, since
  the case-explicit helpers the oracles are built from apply neither escape.
- **Adversarial corpus that compiles.** `llvmdsdl-naming-corpus-compile-gate` generates
  `test/lit/fixtures_naming` for every backend and compiles each: every C translation unit, a C++
  unit including every header under both the `std` and PMR profiles, all under
  `-Werror -Wreserved-identifier`, then `cargo build`, `go build ./...`, `tsc --strict`, and a Python
  byte-compile. C and C++ are mandatory; the rest skip when their toolchain is absent. Generating all
  six regardless of what the build targets is the compensating control for §8.1, and compiling both
  C++ profiles is what covers the members only one of them emits.
- **Golden maps.** `test/unit/golden/naming-map.txt` holds the case-explicit projections over an
  adversarial corpus and the collisions they produce; `naming-roles.txt` holds the role table, the
  per-role projections and scope repair. Regenerate both with
  `LLVMDSDL_UPDATE_NAMING_GOLDEN=1 <build>/test/unit/llvmdsdl-unit-tests`. A diff in either changes
  generated identifiers and is an ABI change.
- **Manifest against the tree.** `test/lit/check_naming_manifest.py` asserts every file stem the
  manifest names for Go exists in the Go output.

The unit tests accumulate their results rather than short-circuiting: a naming change usually breaks
more than one property, and `&&` would report only the first.

---

## 8. Decisions

### 8.1 Emitting checks its own backend; analysing checks all of them

`Discovery` checks the languages the invocation will emit. A C-only build does not fail over a
collision only Go would suffer: failing a build over output it was never going to produce is the kind
of diagnostic people learn to route around, and coupling every build to the union of all backend
policies would make *adding* a backend a breaking change for existing namespaces.

An invocation that emits nothing gets the opposite answer. `ast`, `mlir` and the language server check
every language, because there is no build to fail — the diagnostic is information, and these are the modes
people use to ask whether a namespace is sound.

Extending "only the selected backend" to the analysis modes is the consistent-looking reading: they
select nothing, so they would check nothing. It removes the only cheap way to ask whether a namespace
survives every backend. State the rule as *never fail a build over output it will not produce*; the
two readings diverge only where nothing is produced.

### 8.2 Python stdlib shadowing is a packaging constraint

`uavcan.time` generates a package directory named `time`. Under absolute imports that is harmless. It
bites only if the output directory itself is placed on `sys.path`, at which point `import time` may
resolve to the generated package.

That is a property of how the output is installed, not of the name. Renaming the directory to `time_`
would rename a namespace that is correct, propagate into every import path, and diverge from the
other five backends over a hazard correct packaging already avoids. It is documented as a constraint
on the Python backend instead.

### 8.3 The rename note is on by default for file and namespace names

Renames that change a path are announced without being asked for; renames inside a generated file are
not.

The asymmetry is about what the user can see. A field that became `map_` is visible in the same
header they are reading to call it. A type file that became `break__1_0.go` is invisible until a build
rule expecting `break_1_0.go` fails, and by then the message is a missing-file error with no mention
of naming.

An in-scope collision repair (§6) is announced despite living inside a generated file, and the line
between the two is predictability rather than location. `map_` follows from `map` and the rules; the
`_2` in `break_2` follows from *another* name and from declaration order, and the identifier itself
says nothing about which name it lost to.

"A name changed" is useless as a trigger, since every backend renames `FooBar` to `foo_bar` as a
matter of course. `codegenProjectIdentifierDetailed` reports a flag set only when a keyword, a claimed
name, an illegal character or a reserved namespace forced a change beyond the case projection. The
standard `uavcan` namespace produces no notes.

### 8.4 The naming policy is not user-configurable

Nunavut's tables are JSON a user can override from the command line. Generated identifiers are the ABI
between generated code and hand-written code, so two builds with different naming configuration
produce silently incompatible artefacts. The tables here are compiled in and change only with a
documented version bump.

`--encode-reserved-identifiers` is the one exception, and it is not a policy override: it changes
whether a definition is *accepted*, never how a name is spelled.

Nunavut's own `enable_stropping` flag is the cautionary tale. It is honoured by
`filter_short_reference_name` and the namespace path builder but not by `filter_id`, so turning it off
yields unstropped type names alongside still-stropped field names. A switch that is only partly wired
is worse than no switch.

---

## 9. Visibility

- **The naming manifest**, `--naming-manifest <file>`: per target language, each type's file stem and
  namespace path and every field and constant identifier. It lets a build rule reference a generated
  symbol without reimplementing the projection, and gives the language server its hover text. Targets
  that emit no source report every language at once.

  `file_stem` is exact for every backend. `type_name` is reported only for Go, TypeScript and Python; C, C++
  and Rust build namespace-qualified symbols in their own emitters, for which the shared projection is
  only part of the answer, so the manifest omits the key rather than report half a name.

- **Hover** groups languages by the identifier they produce — ``emits as `count` (c, cpp, rust, ts,
  python) · `Count` (go)`` — rather than printing six rows, five of which agree.
