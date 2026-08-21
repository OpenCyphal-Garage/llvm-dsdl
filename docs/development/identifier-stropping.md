# Identifier stropping (design)

Every backend has to turn a DSDL name into an identifier the target language will accept. When this
note was written, six emitters each did that for themselves by calling a handful of free functions in
`lib/CodeGen/NamingPolicy.cpp`, and the frontend re-implemented part of the same projection to detect
output-file collisions. The policy was therefore stated three times and agreed only by accident — §3
shows a case where it did not, and a whole type was silently lost.

This note specifies a single stropping engine: one pipeline, one table per language, and collision
detection that consumes the engine rather than approximating it.

Status: implemented except for §3.3, which §5.2 explains. This is the rationale record; the
user-facing description lives in [the backend reference](../reference/codegen/backends.md).

---

## 1. Terms

Three separate transformations get conflated under the word:

1. **Case projection** — `flight_control_mode` → `FlightControlMode` for Go, `flight_control_mode`
   for Rust, `FLIGHT_CONTROL_MODE` for a C macro. Cosmetic, but many-to-one.
2. **Escaping** — making a name the target language will accept at all: keywords, reserved
   namespaces, illegal characters.
3. **Disambiguation** — repairing the collisions that (1) and (2) introduce.

Nunavut ([`3.0.preview`](https://github.com/OpenCyphal/nunavut/tree/3.0.preview)) implements (2)
thoroughly and configurably, does (1) implicitly per-template, and does not do (3) at all — two
distinct DSDL names can land on one identifier with no diagnostic. llvm-dsdl did (1) and (3) and only
the keyword half of (2) when this note was written; §5 records what closed.

Where a choice is arbitrary this design takes nunavut's, so that a reader who knows one tool can
predict the other. Where nunavut's choice is demonstrably defective — §4.3 — this design deviates
and says why.

---

## 2. DSDL guarantees

The reachable input set is much smaller than nunavut's engine assumes, and pinning it down removes
most of the machinery.

By the time a name reaches a backend it has passed `isValidNameComponent` and
[`isReservedIdentifier`](https://github.com/OpenCyphal-Garage/llvm-dsdl/blob/main/lib/Support/ReservedIdentifiers.cpp),
so it:

- matches `[A-Za-z_][A-Za-z0-9_]*` — ASCII only, never starts with a digit;
- does not match `_.*_$`, `u?int\d*`, `float\d*`, `void\d*`, `q\d+_\d+`, `com\d`, `lpt\d`;
- is not one of `truncated saturated true false bool byte utf8 optional aligned const struct super
  template enum self and or not auto type con prn aux nul` (case-insensitively).

Two consequences shape the design:

**Character escaping is unreachable.** No valid DSDL name contains a character needing an escape,
and none starts with a digit. Nunavut's `zX%04X` encoder and its leading-digit rules never fire on
valid input either; they exist because its `filter_id` is also a general-purpose Jinja filter. Ours
is not — but `dsdlc` is a library and the language server feeds it half-typed text, so the escape
path stays as a defensive fallback, specified and tested, just never exercised by the compiler.

**The reachable keyword set is computable.** DSDL has already reserved `const struct enum self and
or not auto type template super bool true false`, so those can never collide. What remains, per
language, for a role that projects to snake_case:

| Language | Reachable keyword collisions | Examples |
|---|---:|---|
| C | 27 | `break` `default` `register` `switch` `typedef` `while` |
| C++ | 81 | `class` `namespace` `operator` `public` `this` `throw` `typename` |
| Rust | 42 | `fn` `impl` `let` `loop` `match` `mod` `move` `mut` `pub` `ref` `use` |
| Go | 22 | `chan` `defer` `fallthrough` `func` `go` `interface` `map` `package` `range` |
| TypeScript | 47 | `class` `delete` `export` `function` `interface` `new` `null` `string` |
| Python | 31 | `def` `del` `elif` `except` `lambda` `pass` `raise` `yield` |

For roles that project to PascalCase the set collapses almost to nothing — Python's `None` is the
only hit across all six. That is worth stating because it means role-awareness is not a
micro-optimization: it is the difference between escaping 81 names and escaping one.

---

## 3. Defects

This section records the tree these defects were found in, because it is the motivation for §4 — not
a description of the current state. §3.1, §3.2, §3.4 and §3.5 are closed; §3.3 is open, and §5.2
explains what it needs. §5.1 records five further defects, all of them names the *generated code* had
claimed rather than names the language reserves.

**3.1 Collision detection does not model the escape step, and a type is silently lost.**
[`Discovery.cpp:251`](https://github.com/OpenCyphal-Garage/llvm-dsdl/blob/main/lib/Frontend/Discovery.cpp) folds each type name with
`canonicalSnakeCase` and errors when two types fold together. The emitters then build the actual
file stem with `codegenToSnakeCaseIdentifier`, which is `canonicalSnakeCase` *plus the keyword
escape*. The two disagree exactly where the escape fires:

```
Break.1.0.dsdl    -> Discovery key "break"    -> file stem "break__1_0"
Break_.1.0.dsdl   -> Discovery key "break_"   -> file stem "break__1_0"
```

Both names are valid DSDL, the Discovery keys differ so no error is raised, and the second type
overwrites the first — four DSDL files in, three generated files out, no diagnostic.

This reaches the four backends that fold the stem: Rust, Go, TypeScript and Python. C and C++ name
the header after the raw DSDL short name, so they are not affected — which is itself part of the
problem, since it means the frontend cannot state a single rule about its own output. `Break`/`Break_`
hits all four; `Map`/`Map_` is Go only, `Class`/`Class_` TypeScript and Python, `Match`/`Match_` Rust
and Python. The collisions section of `test/unit/golden/naming-map.txt` inventories them.

This is the same failure class as the already-fixed sibling-filename collision, re-entered through a
different door — which is the argument for §4.5 rather than for another patch.

**3.2 Policy is stated in three places.** The keyword tables and the escape live in
`NamingPolicy.cpp`; five separate copies of the macro-token transform live in the emitters — a
private `sanitizeMacroToken` in `CEmitter.cpp`, `CppEmitter.cpp` and `CppObjectAbiEmitter.cpp`, and
two more written inline in the header-guard builders; `Discovery.cpp` carries the fold. Only `GoEmitter.cpp` reserves the names its
own generated code claims (`Serialize`, `Deserialize`); the other five backends have the same
hazard and no guard.

**3.3 No reserved-namespace handling.** `codegenSanitizeIdentifier` checks an exact keyword set and
nothing else. C reserves `_[A-Z]*`, `__*`, file-scope `_*`, `*_t`, and `E[A-Z]*`; C++ reserves any
identifier containing `__`. A DSDL field named `_Foo` or a type named `foo_t` produces a
technically-reserved identifier today. It compiles, so nobody has noticed.

**3.4 Nothing tells the user a rename happened.** A field named `map` becomes `map_` in Go with no
record anywhere except the generated source.

**3.5 The file-stem and type-name projections disagree, and Go stops compiling.** The file stem folds
to snake_case, which *keeps* underscores; the type name folds to PascalCase, which *drops* them. So
`_foo` and `foo_` — two legal, distinct DSDL type names — take different files and the same type
name:

```console
$ dsdlc --target-language go probe --go-module demo/probe --outdir out
$ go build ./...
probe/vendor/foo__1_0.go:10:7: FOO_1_0_FULL_NAME redeclared in this block
        probe/vendor/foo_1_0.go:10:7: other declaration of FOO_1_0_FULL_NAME
```

Go puts one directory in one package, so the two files land in one scope and the package does not
build. TypeScript and Python emit one module per file, so there the duplicate is latent until both
are imported into one scope. The consequence for §4.5 is that keying the frontend check on the file
stem alone is not enough — it has to key on the (stem, type name) pair, because either half can
collide while the other does not.

---

## 4. Design

### 4.1 Roles and scopes

Two concepts, both shared across all languages.

A **role** is what the identifier will be used as. This is nunavut's `id_type`, with the two
corrections that make it useful: it is exhaustive, and it is mandatory. (Nunavut defines roles but
defaults every call site to `any` — the union of every role's rules — so its C backend escapes a
struct member named `memset` that no C rule reserves.)

```
enum class IdentifierRole {
    TypeName,       // struct / class / type alias
    FieldName,      // struct member, object key, attribute
    ConstantName,   // constant, enumerator, #define value
    FunctionName,   // generated free function or method
    LocalName,      // local or parameter inside a generated body
    NamespaceName,  // namespace, package, module identifier
    FileStem,       // output file name, no extension
    MacroName,      // C/C++ preprocessor token, including the include guard
};
```

A **scope** is the region in which the result must be unique: one struct body, one namespace
directory, one module's top level. Macros are global, and stay unique by construction because every
macro is prefixed with its type name (§4.6).

The engine's only entry point is a scope object:

```
NamingScope scope = policy.scopeFor(language, ScopeKind::StructBody, sectionFieldNames);
out << scope.get(IdentifierRole::FieldName, field.name);
```

No emitter calls a projection or a sanitizer directly. That is the property that makes §3.2
structurally impossible to reintroduce.

### 4.2 The pipeline

Five stages, one implementation, shared by every language:

1. **Project** — apply the case style the policy assigns to this role.
2. **Escape** — replace characters outside `[A-Za-z0-9_]` with `zX%04X` (borrowed from nunavut,
   including the spelling); prefix a leading digit with `_`. Unreachable from valid DSDL (§2), kept
   for library and LSP callers.
3. **Strop** — if the result is in the language's keyword set, matches a role-masked reserved
   pattern, or is claimed by the generated runtime, append `_`.
4. **Verify** — re-run stages 2 and 3 and assert a fixpoint. See §4.3 for why one iteration always
   suffices; the assert is what keeps that true as tables grow.
5. **Allocate** — hand the result to `CodegenIdentifierAllocator` (existing, unchanged) against the
   scope's set of already-issued identifiers, which appends `_2`, `_3`, … until unique.

Stages 1 and 2 are many-to-one; stage 5 is what restores injectivity. Stating it that way makes the
invariant testable: *for any set of distinct DSDL names in one scope, the map to identifiers is
injective* (§6).

### 4.3 Deviation: trailing `_` for every language

Nunavut strops with a leading `_` for C and C++ and a trailing `_` for Python. The leading form is
the source of its worst behaviour:

- `_` followed by an uppercase letter is itself reserved in C and C++, so stropping produces an
  illegal identifier and nunavut needs a per-language failure handler that lowercases the character
  after the underscore: `EMACRO_TOKEN` → `_eMACRO_TOKEN`.
- That handler is many-to-one, and nothing downstream repairs it: `_Foo`, `__foo`, and `_foo` all
  become `_foo` in nunavut's C backend.

llvm-dsdl uses a trailing `_` for all six languages. It cannot enter a reserved namespace in any of
them, it needs no failure handler, and it is what the emitters already produce — so the change in
§4.1–4.2 is a refactor, not a regeneration.

**Fixpoint argument.** Appending `_` to `X` can only produce a name needing further escape if `X_`
is itself reserved. No keyword in any of the six languages ends in `_`, and no name the generated
code claims does either, so one iteration always terminates. This is an invariant of the tables, not
of the algorithm, so §6 asserts it directly.

The argument covers suffixes only. A trailing `_` cannot repair a *prefix* violation: `__bar` is
reserved for any use in C and C++, and `__bar_` is just as reserved. Prefix-anchored patterns need a
different repair — removing or replacing the offending prefix — and that repair is many-to-one
(`__bar` and `bar` both become `bar`), so it needs a scope to restore injectivity. §5.2 carries the
consequences.

Rust's `r#` raw identifiers were considered and rejected: `self`, `Self`, `crate`, and `super`
cannot be raw, so Rust would need the trailing form anyway, and having two escape mechanisms in one
language buys nothing.

### 4.4 Policy tables

One `struct LanguageNamingPolicy` per language, data only, no behaviour:

| Field | Contents |
|---|---|
| `caseByRole` | role → Snake / Pascal / UpperSnake / Preserve |
| `keywords` | hard-reserved in every role |
| `predeclared` | shadowable names, reserved only in the roles where shadowing bites |
| `reservedPatterns` | `llvm::Regex` + role mask (C `^_[A-Z]`, `^__`, `_t$`, `^E[A-Z0-9]`; C++ `__`) |
| `runtimeOwned` | names our generated code claims — the seven per-type metadata constants, Go/C++/Python method names, TypeScript `constructor`/`prototype` |

`runtimeOwned` is checked against the *finished* identifier, after the case projection and any
upper-casing, because that is the spelling that has to miss the generated one: a Go constant named
`full_name` is emitted as `FULL_NAME`, and comparing before the upper-case would never match. It is
also deliberately independent of `strop`. Keyword stropping asks whether the language will parse the
identifier; this asks whether something the backend already emitted has taken the name. A C++ macro
token needs the second question answered and not the first.

It belongs to the policy, not to a `NamingScope`. A scope reservation describes one particular
scope; these names are claimed for *every* type the backend emits, and they are needed at call sites
that cannot see a scope — C++ computes member names inside serialization lambdas that never receive
the section. The escape is also the right shape: `Serialize_`, where a scope reservation would give
`Serialize_2`, and `_2` means "the second name competing for this identifier", which is untrue of a
name no other field wanted.

`predeclared` is where role-awareness pays. Python's builtins (`str`, `list`, `id`) are shadowable
and harmless as attribute names — nunavut escapes all of them because it computes its reserved set
as `keyword.kwlist + dir(builtins)`, which also makes its output depend on the interpreter version
running the generator. Here they are reserved for `LocalName` and `FunctionName` and left alone for
`FieldName`. The tables are compiled-in and versioned, so nothing about generated output depends on
the host.

There is deliberately no `fileStemAvoid` field. The names it would have held are already handled
elsewhere: the MS-DOS device names (`con`, `prn`, `aux`, `nul`, `com\d`, `lpt\d`) are rejected by the
frontend as DSDL reserved identifiers, and stdlib shadowing is a packaging constraint rather than a
naming one (§7.2).

### 4.5 Collision detection consumes the engine

`Discovery.cpp` stops folding names itself. It asks the engine for the `FileStem` and `TypeName`
identifiers under each target language selected for *this invocation* and keys on both, which makes
§3.1 and §3.5 unrepresentable: the check and the emitter are by construction the same function. The
scope of that selection is decision §7.1.

The two collision classes get different treatment, and the difference is principled:

- **Cross-type** (two DSDL types → one output file *or* one type name — §3.5 shows the two are
  separate hazards, so both are keyed): **error**. Auto-repair would have to pick which
  type gets the `_2`, and the only available ordering is filesystem discovery order — so the same
  sources would generate different output on different machines. That trades a loud failure for a
  reproducibility bug.
- **Intra-scope** (two fields → one member name): **repair**, as today, plus a remark. Here the
  ordering is DSDL declaration order, which is stable and part of the wire format, so the repair is
  deterministic.

### 4.6 Macros stay unique by construction

C and C++ macros are global. Every generated macro is `<TypeName>_<MEMBER>`, and type names are
unique after §4.5, so macro uniqueness follows. The design records this as an invariant so that a
future macro not carrying the type prefix is recognisably a bug rather than a style choice. The
private `sanitizeMacroToken` copies become the `MacroName` role with `UpperSnake` case.

### 4.7 Visibility: diagnostics and manifest

- A remark fires when a name changes: `note: field 'map' is emitted as 'map_' in Go`. It is **on by
  default for the `FileStem` and `NamespaceName` roles** and off by default for everything else
  (§7.3). The split is by consequence, not by volume: a renamed output file or package directory
  changes what a build system has to reference, and the user has no other signal that it happened,
  whereas a renamed struct member is right there in the header they are already reading.
- The emitted manifest gains a `names` section: DSDL full name and field → generated identifier, per
  language. This is what nunavut has no equivalent of. It gives the language server hover text
  ("emits as `map_` in Go"), gives build integrations a way to reference generated symbols without
  reimplementing the projection, and gives §6 its differential-test oracle.

### 4.8 Non-goal: user-configurable policy

Nunavut's tables are JSON that a user can override from the command line. That is a footgun for us:
generated identifiers are the ABI between generated code and hand-written code, so two builds with
different naming config produce silently incompatible artifacts. The tables here are compiled-in and
change only with a documented version bump.

Nunavut's own `enable_stropping` flag is the cautionary tale — it is honored by
`filter_short_reference_name` and the namespace path builder but not by `filter_id`, so turning it
off yields unstropped type names alongside still-stropped field names. A switch that is only
partially wired is worse than no switch.

---

## 5. Artifacts and outcome

Three artifacts hold the naming behaviour still. A diff in any of them is a change to generated
identifiers, which is an ABI change.

| Artifact | Holds | Regenerate |
|---|---|---|
| `test/unit/golden/naming-map.txt` | the case-explicit projections over an adversarial name corpus, and the collisions they produce | `LLVMDSDL_UPDATE_NAMING_GOLDEN=1 <build>/test/unit/llvmdsdl-unit-tests` |
| `test/unit/golden/naming-roles.txt` | the role table, the per-role projections, and scope repair | same command |
| `test/lit/naming-stropping.txt` over `test/lit/fixtures_naming/` | what the six emitters write | edit the expectations by hand |

`NamingPolicyTests.cpp` asserts every cell of the role table against the projection its call sites
use, which is what keeps the table from drifting out of step with the emitters.

### 5.1 Defects this design closed

§3.3 concerns names the *language* reserves, which is a compiles-anyway concern. Generating those
cases turned up five that do not compile, every one a name the *generated code* had already claimed:

| Case | Backend | Symptom |
|---|---|---|
| DSDL constant named after one of the seven metadata constants | C++, Go, Rust | duplicate declaration; `go build` fails |
| DSDL field named `FULL_NAME` | C++ | data member redeclares the generated static |
| DSDL field named `serialize` | C++ | data member and member function share a name |
| Two DSDL constants folding together (`foo_bar` + `FOO_BAR`) | C | duplicate `#define`; the second value wins |
| Same pair | C++ | duplicate `static constexpr` |

The last two are the many-to-one fold the other four backends had always repaired: C and C++ had no
constant scope. `test/lit/field-method-name-collision.txt` asserted C++ was *unaffected* by the third
row, which is what happens when a case is reasoned about rather than generated.

C needs no claimed-name escape because its metadata macros carry a trailing `_`: `<Type>_FULL_NAME_`
cannot meet `<Type>_FULL_NAME`. That convention is worth copying into a backend that grows a new
generated name.

The standard `uavcan` namespace generates byte-identically across all seven targets and reports no
renames, so none of this touches conformant DSDL.

### 5.2 Open: reserved patterns

C's `^__` and `^_[A-Z]` and C++'s `__` anywhere need a repair that a trailing `_` cannot provide
(§4.3). Stripping the offending prefix would work and is many-to-one, so it needs a scope to restore
injectivity — and C++ has no field scope, because `Preserve` is injective and none was needed. Two
decisions and a threading change. The hazard is real but theoretical: `__bar` as a C member is
reserved for any use and compiles everywhere.

### 5.3 Unrelated finding

The `obj` backend's `llvmdsdl_generated.a` differed between two runs of the same binary on the same
input, because `ar` records each member's modification time, uid and gid. The determinism matrix did
not report it: that table listed six backends and `obj` was not among them, and a backend absent from
the table reads as covered rather than missing. Fixed in `ObjectEmitter.cpp`, gated by
`llvmdsdl-uavcan-obj-determinism`, and `obj` is now in the table.

---

## 6. Tests

- **Injectivity property test.** Generate random sets of distinct valid DSDL names, run each through
  a scope for each language and role, assert the map is injective. This is the invariant §4.2 exists
  to provide, and it is the one nunavut fails.
- **Fixpoint test.** For every entry in every keyword set and every reserved pattern, assert
  `strop(strop(x)) == strop(x)`.
- **Table invariant test.** No keyword in any table ends in `_`. This is what makes the one-iteration
  argument in §4.3 valid; if a future language breaks it, the test says so instead of the fixpoint
  assert firing at generation time.
- **Adversarial corpus that compiles.** A DSDL namespace whose names are drawn from the §2 reachable
  sets — `Break`, `Break_`, `Map`, `_Foo`, `__foo`, `foo_t`, `FooBar` + `foo_bar` in one struct —
  generated and compiled in all six languages on the existing native lanes. This is also the
  compensating control for §7.1: a user's build only checks the backends it selected, but the
  project's own corpus is checked against all six on every run, so a collision that is unique to one
  backend is caught here rather than by the first user to select it.
- **Differential test.** Manifest against golden map, so any unintended change to the mapping shows
  up as a diff rather than as a downstream compile error.

---

## 7. Decisions

The three questions this note opened are settled. They are recorded here rather than folded silently
into §4 because each one traded something real away.

### 7.1 Emitting checks its own backend; analysing checks all of them

`Discovery` runs the check for the target languages the invocation will actually emit. A C-only
build does not fail over a collision that only Go would suffer: failing a build over output it was
never going to produce is the kind of diagnostic people learn to route around, and coupling every
build's success to the union of all backend policies would make *adding* a backend a breaking change
for existing namespaces.

An invocation that emits nothing is the opposite case, and gets the opposite answer. `ast`, `mlir`
and the language server check all six, because there is no build to fail — the diagnostic is pure
information, and these are the modes people use to ask whether a namespace is sound. Hiding a
collision there helps nobody.

Extending "only the selected backend" to the analysis modes is the consistent-looking reading — they
select nothing, so they would check nothing. It removes the only cheap way to ask whether a namespace
survives every backend. State the rule as *never fail a build over output it will not produce*; the
two readings diverge only where nothing is produced.

One diagnostic is emitted per colliding pair, naming every language affected:

```
error: type name collision in generated output: ns.Foo_bar and ns.FooBar
       map to the same output file name for target languages 'rust', 'go', 'ts', 'python'
```

Renaming one of the two types fixes every language at once, so a line per language would be noise;
and a pair whose file names already collide is not also told that its type names do.

### 7.2 Python stdlib shadowing is a packaging constraint, not a naming rule

`uavcan.time` generates a package directory named `time`. Under absolute imports that is harmless;
it only bites if the generated output directory itself lands on `sys.path`, at which point
`import time` may resolve to the generated package.

That is a property of how the output is installed, not of the name, and the naming engine is the
wrong place to fix it: mangling the directory to `time_` would rename a namespace that is correct,
propagate into every import path, and diverge from the other five backends for a hazard the user can
avoid by installing the package correctly. It is documented as a constraint on the Python backend —
put the *parent* of the root namespace on `sys.path`, never the root namespace itself — in
[the backend reference](../reference/codegen/backends.md).

This is also what empties `fileStemAvoid` (§4.4): the only other candidate members, the MS-DOS device
names, are already rejected by the frontend as DSDL reserved identifiers.

### 7.3 The rename remark is on by default for file and namespace names

Renames that change a path are announced without asking; renames that stay inside a generated file
are not (§4.7).

The asymmetry is about what the user can see. A field that became `map_` is visible in the same
header they are reading to call it. A type file that became `break__1_0.go` is not visible anywhere
until a build rule that expected `break_1_0.go` fails, and by then the message is a missing-file
error with no mention of naming. The expected volume is also low enough to afford it: a path-level
remark requires a type or namespace name that is a keyword in a selected target, which §2 shows is a
few dozen names per language, none of them likely in a real namespace.
