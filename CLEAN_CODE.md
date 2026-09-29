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

Taking `uavcan.file.List.0.2` from the regulated corpus, at `b1c5dbe8`:

| | the section type | a synthesised helper |
|---|---|---|
| C | `struct uavcan__file__List__Request` | `int8_t llvmdsdl_plan_capacity_check__uavcan__file__List_0_2__Request` |
| C++ | `struct uavcan::file::List_Request` | `inline std::int8_t uavcan::file::mlir_llvmdsdl_plan_capacity_check_uavcan_file_List_0_2_request` |
| Rust | `pub struct Request` in `uavcan::file::list_0_2` | `fn capacity_check_request` |
| Go | `type ListRequest struct` | `func listCapacityCheckRequest` |
| Python | `class ListRequest` | `def _capacity_check_request` |
| TypeScript | `export interface ListRequest` | `function capacityCheckRequest` |

The corpus yields 658 helper symbols. Rust, Go, Python and TypeScript name each by the scope that
holds it, which phase 1 below records. C and C++ still carry the whole definition in the symbol:
in C++ the helpers sit at namespace scope, reachable by ADL, and in C they carry external linkage,
so a corpus contributes 658 `llvmdsdl_`-prefixed symbols to the global namespace and two corpora
linked together collide.

A C++ identifier restates the scope that already contains it.
`mlir_llvmdsdl_plan_capacity_check_uavcan_file_List_0_2_request` is declared inside
`namespace uavcan::file` beside `List_Request`, and names both again.

Beyond the helpers, each language carries its own:

**C** is the closest to right, having been rebuilt through the body translator in #40.
`uavcan__file__List__Request` uses `__` as the separator a language without scopes needs, and the
out-of-line `_ir_` entry points, each published under the section's name by an inline forward, are
a deliberate shape. The helpers need internal linkage.

**C++** flattens `List.Request` to `List_Request` where the language has both nested classes and
namespaces. The free `List_Request_serialize_` duplicates the `serialize` member, so one operation
has two public spellings.

**Python** puts the type's facts in module-level `DSDL_*` constants rather than on the class they
describe.

**TypeScript** exposes `makeListRequest`, `serializeListRequestInto` and
`deserializeListRequestFrom` — free functions carrying the type name, which is how a language
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

The helper name shows the cost. `lib/Transforms/Passes.cpp` mints the helper's identity,
`uavcan.file.List.0.2.request.plan.capacity_check`, as the MLIR symbol. C spells it
`llvmdsdl_plan_capacity_check__uavcan__file__List_0_2__Request` in its emitter, and
`renderHelperBindingIdentifier` spells it `mlir_llvmdsdl_plan_capacity_check_uavcan_file_List_0_2_request`
for C++. Each spelling is composed away from the scope that declares it, so each restates the whole
definition that scope already names.

## Language classification

The classification is the input the surface layer reads. One row per language, stating what the
language can express, indexed by the questions a declaration's shape depends on. The rows are
`LanguageTraits` in `llvmdsdl/Support/LanguageTraits.h`, and `LanguageTraitsTests` pins each.

| | scopes below the file | nested type declarations | methods | internal linkage | error convention | type's own constants | fields a class of their own | reserved by underscores | name classes in one scope |
|---|---|---|---|---|---|---|---|---|---|
| C | none | no | no | `static` | status code | macros in the enclosing scope | yes | leading `__`, `_X` | ordinary identifiers and structure tags; macros across the translation unit |
| C++ | namespace, class | yes | yes | private member | status code | `static constexpr` in the class | no | those, and `__` anywhere | one; macros across the translation unit |
| Rust | module | no | yes, in `impl` | private by default | `Result<T, E>` | associated `const` | yes | none | types, modules among them, values, and fields |
| Go | none below the package | no | yes, by receiver | lower-case initial | `(T, error)` | package scope, typed | no | none | one per package, across its files |
| Python | class | yes | yes | `_` prefix | exception | class attribute | no | none | one |
| TypeScript | class, namespace | yes | yes | not exported | exception | `static readonly` | yes | none | types and values; modules by path |

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

A pass, `project-dsdl-surface`, runs last in `lower-dsdl-bodies` for a target. It builds a tree of
the scopes that target's output opens, with every identifier the output declares allocated in its
scope, and the emitters read names and placements from the tree instead of composing them. The
pipeline registered without a target builds no tree.

### The tree names the IR

An IR symbol is an identity: it says which schema, helper or body a reference means. A name in the
tree is a spelling, and so is its place. In C++'s target shape the tree puts `serialize` in
`List_0_2::Request` and `capacity_check` there as a private static member, and the renderer writes
the flat `func.func` the pipeline built into that position. Clang compiles C++ member functions to
flat functions in LLVM IR in the same way: the nesting lives in the declarations.

Bodies stay top-level functions, and their symbols do not change:

- three of the ways a body refers to an entity are strings a symbol rename would not reach: the
  helper names on plan steps, `llvmdsdl.schema_sym`, and the `!dsdl.object` identity;
- `planBodySymbol` recomposes a nested call's callee from the full name and version rather than
  looking it up;
- `schemaFunctions`, `PlanBodyLookups`, `schemaOf`, the emitters' accessor lookup,
  `cloneFunctionsOf` and the canonicaliser nested on `func.func` read a flat module;
- the object lane exports every function, and renames each function and each call into another
  definition to the C link name it reads back from the symbol through `parsePlanSymbol`, so a body's
  symbol is what its ABI name is spelt from.

Moving the bodies into scope ops and rewriting their references with `replaceAllSymbolUses` would
produce the same output at the cost of every item above. A symbol is renamed only in the object lane,
before LLVM conversion, to the C link name its header declares; there a symbol is also what a linker
reads.

The tree is built from three ops in `DSDLOps.td`, with typed attributes:

| op | holds | attributes |
|---|---|---|
| `dsdl.surface` | the root scope for one target, only when a target is set | `target`, and `profile` where one language's profiles declare different names |
| `dsdl.scope` | scopes and declarations, in the order the language opens and writes them | `kind` (root, namespace, module, package, file, type), `name`, `path` for the file a scope is written to, and `of` with `section` for a type |
| `dsdl.decl` | nothing: a leaf | `name`, `kind`, `class` (which the kind implies for every kind but an import), `visibility` (public or private), `origin` (definition or generated), `fact` where a generated name states one its kind and `of` do not tell apart, and `of` with `section` and `member` where it names an entity |

```mlir
dsdl.surface target = "rust" {
 dsdl.scope root "llvmdsdl_generated" path = "src/lib.rs" {
  dsdl.decl "dsdl_runtime" kind = module origin = generated
  dsdl.scope namespace "uavcan" path = "src/uavcan/mod.rs" {
   dsdl.scope namespace "file" path = "src/uavcan/file/mod.rs" {
    dsdl.scope module "list_0_2" path = "src/uavcan/file/list_0_2.rs" {
      dsdl.decl "Path" kind = import class = type visibility = private of = @uavcan.file.Path.2.0
      dsdl.decl "capacity_check_request" kind = helper visibility = private
          of = @uavcan.file.List.0.2.request.plan.capacity_check
      dsdl.scope type "Request" of = @uavcan.file.List.0.2 section = "request" {
        dsdl.decl "entry_index" kind = field of = @uavcan.file.List.0.2
            section = "request" member = "entry_index"
        dsdl.decl "FULL_NAME" kind = constant origin = generated fact = full_name
            of = @uavcan.file.List.0.2 section = "request"
        dsdl.decl "serialize" kind = entry of = @uavcan.file.List.0.2.request.serialize
      }
      dsdl.decl "List" kind = alias origin = generated
    }
   }
  }
 }
}
```

A declaration is not an MLIR symbol. One symbol table is one namespace, and a scope holds as many
name classes as its language gives it; a symbol table per class would split a scope's declarations
and lose the order the renderer writes them in. The classes each language keeps apart are the name
classes column of its row in [Language classification](#language-classification), and the verifier
holds the tree to them:

- every scope but a `file` scope is a namespace. A `file` scope declares into the namespace around
  it, as a C or C++ header and a Go file do; its imports are its own, as Go's file block holds them,
  and may not meet a name the namespace declares;
- a type scope's name is a type in its namespace, and a namespace, module or package scope's name is
  a module there, which the row puts among the types or keeps apart as a path;
- macros are one class across the surface, the translation unit a set of headers can meet in;
- no two scopes are written to one path, and every path stays within the output directory;
- every `of` resolves against the module's symbol table, a type scope's to a schema, and a `section`
  or `member` to one the schema declares.

### What the tree holds

The tree holds every name declared at file, namespace and type scope, generated ones included:
metadata constants, wrappers, guards, `to_bytes`, `AppendBinary`, pool constants. The allocator cannot
reproduce today's names without them, since a DSDL constant's ordinal depends on the generated names
claimed before it. Names a language binds in the scope by importing them are declarations too:
Python's `dataclass` and `field`, TypeScript's `dsdlRuntime`, Go's package names. Names inside a
function stay with the body translator's value naming.

The root is the generated package: the crate, the npm package, the Python package or the Go module,
and for C and C++ the output directory. It carries the package's name from the command line or its
default, and holds the generator's root-level names as generated declarations: Rust's `dsdl_runtime`
module, Python's `_runtime_loader`, `_dsdl_runtime` and `__version__`, Go's `dsdlruntime`,
TypeScript's `dsdl_runtime`, and the identifiers the C and C++ runtime headers declare. Those names are
reserved as each language's projection table reserves them today, so the output does not change, and
a root namespace that meets one is reported with every other collision.

The root namespaces are the root's children, and each namespace scope names the file it is written
to: Rust's `mod.rs`, Python's `__init__.py`, and TypeScript's `index.ts` once TypeScript's phase
replaces the flat root index. A directory is a scope whose subdirectories and files share one name
class, since Rust's `pub mod`, Python's package attributes and TypeScript's import paths name both in
one space. Until TypeScript's phase, the flat root `index.ts` stays with the TypeScript emitter, outside
the tree: its aliases share a namespace with nothing but one another.

A tree is built per language and C++ profile. `pmr` declares `_memory_resource` and
`set_memory_resource`, and `--cpp-profile both` builds two trees; the Rust profile and memory mode
declare the same items. The object lane reads C's tree.

### One allocator, three callers

Discovery checks collisions for every language inside `parseDefinitions`, and the language server
calls it on each analysis (`lib/LSP/Analysis.cpp:646`). An analysis run renders the manifest for every
language before any MLIR exists, and the pass runs after lowering, for one target. All three call one
library, `allocateSurface`, which takes a language row, a profile and the definitions as parts and
answers a surface plan. The composers and scope builders move behind it.

The plan has two layers:

- the **definition layer** covers files, types, sections, fields, constants, option tags and imports,
  and needs only the parts the front end has;
- the **body layer** covers helpers, entry points and accessors, and needs the lowered symbols, which
  only the pass has.

Discovery and an analysis run's manifest see the definition layer. A collision with a helper or an
entry point is reported by the verifier in a generation run, and a generation run's manifest reports
the whole tree for its target.

Within a scope, names are claimed in five bands, and a later band never moves a name an earlier band
claimed:

1. language reservations: keywords, runtime-owned names, reserved namespaces;
2. generated claims: `FULL_NAME`, `EXTENT_BYTES`, the metadata constants, `_tag_`, pool constants;
3. declarations, in declaration order: types, fields, constants, array metadata, option tags,
   accessors, entry points;
4. helpers, qualified by type where a package holds many definitions;
5. imports, claimed last, so a declaration keeps its public name and the import takes the alias.

The accessors and entry points that close band 3, and band 4, are the body layer, since the
lowering decides which bodies exist; the rest of bands 1 to 3 is the definition layer. Band 5 is
`ImportNameScope`'s rule, which Rust, TypeScript and Python follow.

### The drift the tree removes

Five stages decide names from their own inputs today, and they disagree in places:

- a C++ struct is allocated twice: `makeSectionFieldScope` declares its union option tags, and the
  C++ spelling's scope does not;
- the driver's repair notes build `makeSectionConstantScope` for every language, where Go allocates
  with `makeGoConstantScope`;
- Go's accessors, TypeScript's accessors, C's wrappers and Rust's pool constants are declared in no
  scope, and Go's import aliases are checked only against one another;
- TypeScript's root `index.ts` aliases each module by its path and a counter, so `ns.FileList.1.0`
  and `ns.file.List.1.0` both reach `ns_file_list_1_0`, and which one gets `_1` depends on the rest of
  the run;
- the manifest omits entry points, accessors, helpers, import aliases, guards, the Rust module name
  and the C++ type name.

Some of today's pools are split where the language has one scope: Rust's helpers are kept apart from
its module's values, C++'s option tags are in one of its two scopes, and Go's and TypeScript's
accessors are in none. Phase 4 reproduces each split through a `Composition` flag, and each merge
lands afterwards in a change of its own, with an adversarial axis that reaches the collision it
fixes. The oracle covers the corpora, so a merge inside the refactor would pass it unexamined.

## Spelling a reference

A reference is written the way a person writing at that spot would write it: as the shortest spelling
that the language's own name lookup, starting at the reference, resolves to the declaration it means.
Inside `struct Request` a constant is `EXTENT_BYTES`; at namespace scope it is
`List_0_2::Request::EXTENT_BYTES`.

The resolver tries candidates shortest first and writes the first that resolves:

1. the bare name;
2. the receiver form, where the language reaches the site's own type through one: Rust's `Self::`,
   Python's `self.` or `cls.`, a Go receiver, TypeScript's companion `const`;
3. the name qualified by its enclosing scopes, outward, while each qualifier is within the horizon;
4. an import's local name, for a declaration in another file of a language that imports;
5. the rooted path, with C++'s leading `::` only where the tree shows the root name shadowed.

A body's own type is written as the table below gives it, ahead of its bare name, where the
language names it from within: `Self` in Rust, `cls` in a Python class method.

Resolving a candidate repeats the language's lookup over the tree: the scopes the site sees, in the
language's order, checking the name classes the reference's position considers. Member access
through a value, such as `self.directory_path.serialize(…)`, writes the member's name after the value
and is never qualified.

| language | a function body sees, in order | its own type's members | its own type |
|---|---|---|---|
| C | one file scope; every name is global and carries its path | the bare name | the bare name |
| C++ | the block, the completed class, each enclosing class, each enclosing namespace, the global namespace | bare, through class-member lookup and the implicit `this` | bare |
| Rust | the block, the module's items, its `use` declarations, the prelude | `Self::X`, and `self.x` for fields | `Self` |
| Go | the block, the package across its files, the file's imports, the universe | through the receiver; constants and helpers are package-level and bare | bare |
| TypeScript | the block, each enclosing function, the module's declarations and imports, the globals | through the companion `const` | bare |
| Python | the function, each enclosing function, the module's definitions and imports, the builtins | `self.X`, `cls.X` in a classmethod, the class name in a staticmethod | `cls` in a classmethod, the class name elsewhere |

A shorter spelling is safe only where no declaration the resolver cannot see could capture it, so the
resolver shortens through two kinds of scope: a definition's own scopes, which one file declares
whole, and the C++ namespace or Go package a definition shares with the run's other definitions of its
DSDL namespace, whose names Discovery and the verifier keep distinct. An enclosing C++ namespace is
open to any header, so a reference that leaves its own namespace is written from the root:
`uavcan::si::unit::length::Scalar`. A spelling captured by a nearer declaration is lengthened until it
resolves; a field named `Path` of type `Path` is declared `uavcan::file::Path Path{};`.

A resolver per language, `spell(site, declaration)`, answers each spelling from the tree, where a site
is a scope together with what the enclosing function is to its type. Nothing stores a spelling per
reference. The lookup model is a `Lookup` part of the `LanguageTraits` row, which
`llvmdsdl-language-classification` holds to naming no language. C++ writes rooted names today, such as
`::uavcan::file::Path` inside its own namespace, so phase 4 reproduces them through a `Composition`
column, `qualification = rooted`, which C++'s output change in phase 5 sets to `shortest` as it nests
its types.

## One renderer, one declaration spelling per language

`translateFunction` holds the structure of a body and asks a `BodySpelling` for the language.
The declaration half takes the same shape: a shared renderer, `DeclarationRenderer`, walks the
surface tree — opening and closing scopes, emitting declarations in dependency order, placing
definitions — and asks a `DeclarationSpelling` for the syntax.

The renderer holds: the order of a file's parts, from a layout each language states; scope nesting;
forward declarations where a language needs them; which declarations are public; and where a body is
attached. The spelling holds: how this language opens a namespace, writes a field, declares a
constant, spells a signature, marks a declaration internal, and returns an error. A lowered
function's signature is spelt once, from its IR function's types and its declaration's place in the
tree, and serves as the prototype C and C++ declare and as the definition; `translateFunction` writes
only the body. The signatures of functions no body lowers -- Go's encoding methods, C's public names,
the initialiser factories -- are the spelling's too. What the tree does not hold -- a field's type and
default, a constant's value, a section's metadata, a doc -- the spelling reads from a view of the
definition's facts the renderer hands it. Every reference the renderer writes, in a declaration or a
body, is spelled by `spell(site, declaration)`.

No declaration wraps a body. A body is written in the shape its language publishes -- the member's
types, the row's error convention, a member or a free function where the language's target places
it -- rather than in a neutral shape with an adapter around it. The interface that allocates and
returns the encoded bytes is written once in each language's runtime and reached by the type; Go
keeps a one-line method per type, which its `encoding` interfaces require, calling the runtime. C
publishes an unversioned name over the versioned link name it compiles each body under, which
unversioned type names require.

This is where the string emission falls. `tools/count_emission_sites.py` counts the places that write
generated text. When phase 2 took the count the six emitters held 1,230, 918 of them outside the
`BodySpelling` subclass — the declaration half — and 412 of those in `Ts.cpp` alone, 340 of which
wrote TypeScript's runtime line by line. A signature is assembled by concatenation at every entry
point, for every profile, in every language:

```cpp
w.line("inline std::int8_t " + plan.typeName + (serialize ? "_serialize_(const " : "_deserialize_(") +
       plan.declaredName + "* const " + object + ", " + (serialize ? "" : "const ") +
       "std::uint8_t* const buffer, std::size_t* const inout_buffer_size_bytes" + resource + ")");
```

A lowered function's IR states its parameters and its return, and the spelling writes one parameter
list from them. `llvmdsdl-emission-sites` holds each emitter's counts to
`test/integration/emission-sites.json`, so a count rises only by a retake that says why. The
declaration half is the measure the renderer is gated on, so the count scans every file of a
language's declaration half and counts every line built as a string. A language's packaging writers
live in a support file of their own, counted as a third figure that may not rise either.

This is not licence to add a template engine. A template is a second statement of the shape, in a
language the compiler cannot check, and it is what every code generator reaches for — nnvg included,
which is why the pull towards one is worth naming. Where nnvg's shape lives in a per-language
template tree that a human keeps in agreement with five others, ours lives in a classification a
pass reads: adding a language is a row in the table and a spelling, and the shape follows from what
the language can express. Giving up that property to save string concatenation trades the reason
this compiler exists for a convenience the renderer already provides.

## Imports are named, not found

A generated file's imports are the modules that provide the names it writes, and nothing else.
`ImportSet`, in `include/llvmdsdl/CodeGen/ImportSet.h`, is where that is decided. The code that
writes a name from another module names it through the file's set, which records the module and
answers the spelling; the file is rendered first, and its import block is written from the set. A
backend states where each of its names comes from and how its language writes an import. It keeps
no list of imports a file may need, and reads none back off the text it wrote.

Python names through it: `sys` where a body bounds an index, `dataclasses` where a class and its
defaults are declared, the runtime where a body calls it, and a nested type where the file names
it. A file that names none of them imports none of them, which took Python's `F401` from 198 to
none.

Go names through it too: `unsafe` where a body moves an object as bytes or a layout is asserted,
`slices` and `bytes` in the encoding methods, the runtime where a body calls it, and a nested type's
package where a field names it. Go had read its imports off the text, doc comments included, so a
definition whose comment mentioned `bytes.Clone(` imported a package the file did not use, and Go
refused to compile it. Its import declaration writes the module's own packages in the order of their
paths, which is gofmt's.

TypeScript names through it as well: the runtime's namespace where a body or entry point calls it,
and a nested definition's interface as a type and its functions as values. The set records how a
member is used for this, and a member named in type positions alone is imported with `import type`.
TypeScript had walked the semantic model for every name a nested definition exports and kept the
ones a whole-word scan found in the text.

C++ names through it: a standard name from the header the standard declares it in, a C runtime
function from `dsdl_runtime.h`, a C++ runtime name from `dsdl_runtime.hpp`, the vocabulary's types
and operations from the headers the binding names, and a nested type from its own header where a
declaration or a body names it. C++ had read its includes off the text, where the C runtime's
functions matched the tokens that included `dsdl_runtime.hpp`, which includes the C runtime and
declares none of them: `misc-include-cleaner`'s 485. A `pmr` header also included
`<memory_resource>`, from which it names nothing.

C names through it: a standard name from the header the standard declares it in, a runtime name
from `dsdl_runtime.h`, and a nested type or entry point from its own header where a declaration or a
body names it. The header and the implementation file each record their own. C had read a header's
includes off its text and walked the semantic model for its nested types, and included the standard
library and the runtime in every implementation file, so the two whose bodies call nothing from the
runtime included it, which `misc-include-cleaner` reported.

C and C++ write their includes alike, in `c::renderIncludeLines`: three groups set apart by blank
lines, the standard library's, a bound library's and the generated tree's own, each in the order of
its paths. An implementation file includes its own header ahead of them, which declares the bodies
it defines; the file itself declares only its helpers, which the lowering may define after the body
that calls them.

Rust names through it: a nested definition's struct where a field or its default names it, under the
local name the surface tree declares for its import. A standard or runtime name is written as its
full path, which needs no import, and `RustFileNames` is where that is decided. Rust had imported
each definition a semantic-model walk found, in the walk's order. Its `use` declarations are in the
order of their paths, which is rustfmt's.

A language that gives an import a local name records the name the file spells. Rust, TypeScript and
Python claim it in one `ImportNameScope`, in `include/llvmdsdl/Support/ImportNameScope.h`, which
reserves what the file declares before any import is claimed, so the import is what moves, and
qualifies a clash with as much of its namespace as tells it apart: `TemperatureScalar`. A TypeScript
type brings its factory and its two body functions with it, so a clash there is judged on all four
names. Python had claimed no name at all: a module holding three `Scalar`s imported each under one
name, and every field was built as the last of them. Go aliases a package rather than a type, with
`pkg_`. The scope is band 5 of the surface tree's allocation, where the allocator claims Rust's and
Python's imports; TypeScript's and Go's aliases join it there.

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
`serialize` and `deserialize` answer `Result<usize, Error>` over the runtime's `Error`, an enum
deriving `Debug` and implementing `Display` and `core::error::Error` — stable in `core` since 1.81,
so `no_std` keeps it. The variants are values, so returning one allocates nothing, and a code the
runtime does not define is `Unrecognised`, which carries it.

**Go.** `type ListRequest struct` in package `file`. Go's own wire interfaces come first:
`AppendBinary([]byte) ([]byte, error)`, `MarshalBinary() ([]byte, error)` and
`UnmarshalBinary([]byte) error`, so a generated type satisfies `encoding.BinaryAppender`,
`encoding.BinaryMarshaler` and `encoding.BinaryUnmarshaler` and drops into anything that already
speaks them. `AppendBinary` appends to a slice the caller owns and can reuse, which allocates
nothing once its capacity suffices, and `MarshalBinary` is `AppendBinary(nil)`.
`encoding.BinaryAppender` is Go 1.24's; the method itself needs nothing newer than the module's
`go 1.22`. A type holding a view unmarshals a copy of its data, which the interface asks it not to
keep. The buffer-oriented pair stays beside them, writing into a slice the caller has sized:
`func (r *ListRequest) Serialize(buffer []byte) (int, error)`. The error is the runtime's `Error`,
a code whose named values are constants such as `ErrBufferTooSmall`, as `syscall.Errno` is: a
returned `error` costs no allocation, and every code has a value.
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
does not. Helpers stay module-private and take camelCase names. Each directory's `index.ts`
re-exports its subdirectories and modules under their own names, so a consumer writes
`uavcan.file.list_0_2`.

## Phases

Each phase has a gate that fails against the tree before the phase is written.

Rust landed first, in #41, ahead of the mechanism phases rather than after them. That was not the
order below and it earned something: phase 4's gate is that the surface tree reproduces today's
output byte for byte, and today's output is now idiomatic Rust rather than the flat names. A tree
that reproduces `list_0_2::Request` is tested against the shape it exists to produce; one that
reproduced `uavcan_file_List_Request` would only have been tested against the shape it replaces.

**1 — The judges.** *Landed: every judge is in the image, asserted, and holding its lane to a baseline.*

The compilers already run over the regulated corpus: `RunUavcanRustCargoCheck`
runs `cargo check`, `RunUavcanGoBuild` runs `go test ./...`, `RunUavcanTsTypecheck` runs `tsc`, and
the C and C++ generation lanes compile under `-Werror`. What none of them judges is idiom, because
a compiler accepts an un-idiomatic name by design. Python had no lane of either kind.

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
`readability-identifier-naming` over the generated C and C++. `ts26.4.5` carries all of them:
`staticcheck` 2025.1.1, `ruff` 0.16.8, `eslint` 10.11.0 with `typescript-eslint` 8.70.1, `clippy`
0.1.93, and the `clang-tidy` 22.1.2 the lint lane already runs. `tools/assert_style_judges.py` is what
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
beside `typescript-eslint` 8.70.1 and TypeScript 5.2.2, `clippy` 0.1.93, and `clang-tidy` 22.1.2.
Each is recorded per rule in `test/integration/judge-baselines/`, written by the lane rather than
added up by hand.

Two of those differ from what a local run may have. `clippy` 0.1.95 reports `nonminimal_bool` six
times where 0.1.93 reports `collapsible_else_if` thirty-four, so a host reading 607 and a lane
reading 641 are two judges rather than a regression. TypeScript 5.2.2 against 6.0.3 changes none of
these counts, which is why the pairing the image ships is enough. `typescript-eslint` holds
TypeScript below 6.1 in any case. `no-useless-assignment` arrived in `eslint` 10 and reports a defect the
older release did not, so a lane pinned behind it would have ratcheted in a shape two other judges
already name. The lint lane holds `eslint` at 10 or newer for that reason, and holds the others to
nothing: the versions tried report identically, rule for rule.

The lanes are `llvmdsdl-uavcan-<language>-style-judge`, one per language, each generating the regulated
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

The C and C++ judge is `clang-tidy` under a ruleset for each language, in
`test/integration/judge-rulesets/`. Each is the project's own `.clang-tidy` applied to generated
code: the same families, with a check subtracted only where its reason holds there, and the reason
written beside it. `readability-identifier-naming` states the design above. C's composed names are
accepted in the three forms the design gives them -- a type, an entry point or accessor, and a fact
or constant -- and every other C name takes C's own case. The C++ ruleset accepts no composed form,
so `List_Request` and its free entry points are findings until C++'s output change in phase 5 nests
them.

Every header is judged as a translation unit of its own, because `misc-include-cleaner` reads only a
unit's main file and would otherwise never check a header's includes. The C++ judge reads the `std`
profile at C++20, the lowest standard that profile compiles under, so no modernize check suggests
what a consumer on C++20 could not write. It leaves the C runtime header the C++ tree carries to the
C judge.

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
inherits it. `dsdl-fold-null-guards` runs after `build-dsdl-plan-bodies` under the target's
`TargetNullability`, which its row states: Rust keeps neither test, Go, TypeScript and Python keep the one
on the object a caller may omit, and C and C++ keep both. It canonicalises the bodies it changed
rather than waiting for `--optimize-lowered-serdes`, which is off unless a caller asks for it, so a
guard folded to a constant is never left where a reader would find it. The C and C++ output is
unchanged byte for byte.

Two findings survived the fold as spelling rather than lowering, and both were bodies whose plan
cannot fail. Rust wrapped the plan's error code in `if err == 0i8 { Ok(..) } else { Err(err) }`
against an error the fold had made the constant zero, and bound the slice's length to a size local
that the body overwrote before reading. Both are answered in the lowering:
`dsdl-mark-infallible-bodies` marks a body whose every return answers an error of zero, whose
success arm Rust spells alone, and `dsdl-fold-body-sizes` makes the size a body answers a result, so
no local holds it.

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
| `HelperBindingNaming` | one lowered symbol per helper, in four languages | a name the scope holding it reaches it by |
| `Ts.cpp` | `interface X {}`, which any non-nullish value satisfies | `Record<string, never>` |
| `dsdl-fold-unobserved-accessor-sizes` | a composite getter's written-back length, in a local each of four languages suppressed | erased where the getter returns a view that carries its length |

The composite getter was the one defect three judges named alike, 44 times each. It answers a
pointer to the nested type's bytes and writes their length through another, which C reads. Go,
Python, TypeScript and Rust answer a slice, a `memoryview` or a `Uint8Array` instead, which carries
the length, so the write landed in a local nothing read -- and each language had been taught to
hide it: `_ = outSize`, `void outSize`, a leading underscore. The write is erased in the lowering
for every language whose getter answers a view, where it is a question about the getter's signature
and not about nulls, and the canonicaliser takes what fed it, including the subtraction a getter at
a non-zero offset used. The parameter goes with the write: the getter's signature has none, and a
getter that read the size back would fail the fold. C++ answers a span, so the write is erased for
it too.

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
| Python | ruff | 4,188 | 1,160 |
| TypeScript | eslint | 1,810 | 75 |
| | | **10,106** | **2,194** |

The C and C++ judge was installed after the sweep, and read 4,808 findings over the generated C and
5,437 over the C++ at C++14. The C comes to 4,806 with a getter reading a null buffer as an empty
one, and the C++, read at C++20 with the accessors taking spans, to 4,946. With each file including
the headers that declare what it names, the C comes to 4,804 and the C++ to 4,461, and the C to
4,803 once the runtime header includes only what it uses. An implementation file that declares only
its helpers, and leaves its bodies to its header, takes the C to 3,990. Naming the IR by DSDL
identity leaves the C at 3,990 and takes the C++ to 4,294: its headers lose their version sentinels
and guard with `#pragma once`. Giving C's accessors the member's types takes the C to 3,337: the
locals an accessor rebound its size and value into go, and so do the casts its wrappers made.

What is left is per-language.

| count | language | lint | cause |
|------:|----------|------|-------|
| 1,910 | C++ | `modernize-use-auto` | a declaration spells the type its initialising cast already names |
| 1,808 | C | `readability-identifier-naming` | 813 out-of-line bodies named apart from their type (`uavcan__file__List_0_2__Request__serialize_ir_`), 658 helpers and 334 `LLVMDSDL_SELECTED_*_` guards |
| 1,163 | C++ | `readability-identifier-naming` | 658 helpers, 390 free entry points, and the section types and facts that nesting places inside the type |
| 982 | Python | `E501` | long lines |
| 732 | C | `bugprone-narrowing-conversions` | `int8_t` initialised from a conditional of `int` literals, where C++ spells the cast |
| 658 | C | `misc-use-internal-linkage` | the helpers, which the design makes `static` |
| 454 | C++ | `readability-redundant-casting` | `static_cast<std::int8_t>` around operands that already are |
| 381 | Rust | `needless_late_init` | an `scf.if` with statements in an arm; only Rust has a block expression to take it |
| 290 | Go | `ST1000`/`ST1021`/`ST1022` | a package comment, and doc comments that open with the identifier |
| 176 | Python | `SIM300` | `2112 > p0` rather than `p0 < 2112` |
| 175 | C++ | `cppcoreguidelines-pro-type-reinterpret-cast` | `reinterpret_cast<const std::uint8_t*>("")` standing in for a null buffer in a deserialise |
| 168 | C++ | `modernize-concat-nested-namespaces` | `namespace uavcan { namespace node {`, where C++17 writes `namespace uavcan::node {` |
| 167 | C++ | `portability-avoid-pragma-once` | `#pragma once`, one per header |
| 158 | Rust | `unnecessary_cast` | a load casts to the storage type where the field already spells it |
| 123 | C, C++ | `readability-redundant-parentheses` | `!(rejected)`, 123 in each |
| 75 | TypeScript | `naming-convention` | accessor names that keep a field's underscores (`getScalarMeter_per_second_per_second`) |
| 59 | Rust | `derivable_impls` | a written-out `Default` that `#[derive(Default)]` covers |
| 54 | C++ | `modernize-type-traits` | `std::is_standard_layout<T>::value`, where C++17 writes `std::is_standard_layout_v<T>` |
| 34 | Rust | `collapsible_else_if` | an `else` holding one `if`, which the branch shapes leave behind |
| 34 | C++ | `modernize-use-integer-sign-comparison` | a signed value compared with an unsigned one, where C++20 has `std::cmp_less` |
| 28 | Go | `ST1003` | a package name with an underscore, and the runtime scaffold's own constants |

Three of the C and C++ judge's findings were one defect rather than shape.
`clang-analyzer-security.ArrayBound` followed an accessor handed a null buffer and a non-zero size:
the accessor read from, or returned a pointer past, the `""` it substitutes for the buffer, where a
deserialiser refuses that pair. A getter has no error to answer with, so the lowering reads a null
buffer as an empty one, taking its size as zero, and every backend inherits it. Rust, Go,
TypeScript and Python cannot be handed a null buffer, and their output is unchanged byte for byte.
The `ArrayBound` that remains, in the runtime's bit copy, assumes a whole byte and a partial one in
a copy of at most eight bits, which cannot both hold.

**2 — The classification.** *Landed: every decision on a language reads its row, and a check refuses
one that does not.*

`llvmdsdl::Language` is the one enumeration of the languages; the three the naming code, the literal
renderer and the storage tokens each kept are gone. `LanguageTraits` holds a row per language in
three parts: the classification above; `BodyInterface`, what the generated interface hands a body,
which the pipeline takes in place of three positional arguments and which the lowering reads without
meeting a language; and `Composition`, how the output composes its declarations today. The name is
not `LanguageProfile`, which this plan used before: a profile here is `std`, `pmr` or `autosar`, and
stays with the vocabulary.

Everything [Discipline](#discipline) found deciding a capability by a language's name reads the row
instead: the driver's three capability blocks, the section joiner, where a type's constants go,
which names a module reserves, the generated constants' `_`, Python's helper prefix, C++'s reserved
underscores, `Discovery`'s scope and deprecation checks, and the vocabulary's language list.
`DefinitionNamePolicy` became part of the row, losing a field no row had set. The driver resolves
a `--target-language` value to its row through the CLI table, and dispatches to a backend by
switching on the language.

The composition is where the output departs from the classification, and `LanguageTraitsTests`
names the departure the two columns can show: Python and TypeScript declare a type's constants in
the module. The rest -- C++'s flat section types, TypeScript's free functions -- are in declarations
the surface tree takes over.

`tools/check_language_classification.py`, run as `llvmdsdl-language-classification`, refuses a
comparison with a `Language` enumerator, a `case` on one, or a comparison with a language's
`--target-language` spelling, outside the spelling tables and the driver's dispatch; the tree before
this phase has 27. `llvmdsdl-emission-sites` holds each emitter's count of the places that write text.
No generated byte changed: over the showroom and the regulated corpus, all seven targets, their
naming manifests and the MLIR are identical before and after.

**3 — The plan semantics the emitters hold.** The four in [Discipline](#discipline) move out of the
emitters: whether a body can fail becomes an IR fact, a nested call's adaptation to a callee that
answers its size or an error moves into the lowering, a bool array's run of bits gets one expansion,
and whether a size or a parameter is read is stated where the signature is decided. C's two walks
for a definition's dependencies go with them. Rust's and Go's error types follow on the first,
before the next release.

Gate: `llvmdsdl-emitter-ir-queries` holds each emitter's queries of the IR around the op it spells --
the uses of a value, the op that defined an operand, every return of a function, the block an op
sits in -- to an inventory that must match the tree exactly: 28 when the gate was written. A query
added fails, and a query removed is retaken, so the inventory is always the tree's. The phase is
done when it is empty, and the judge findings the four cause fall with it.

All four have moved, with C's two walks for a definition's dependencies, and the inventory is
empty. `dsdl-mark-infallible-bodies` states
whether a body can fail, and Rust reads it. `dsdl-fold-nested-call-sizes` runs for a target whose
nested entry point is handed the space as its buffer's length and answers what it used -- a column
of the row -- and turns the plan's call through a size local into `dsdl.call_serdes_sized`, which
takes the space by value and answers the error and what was used as two results the shared
translator names. Go, Rust, TypeScript and Python each spell one call where each had clamped, called,
split the answer and written a local back under names of its own; C and C++ take the call as the
plan builds it, byte for byte as before. What a nested call used means something only where its
error is zero, which is how the four spellings can hold the answer as it came.

`dsdl-expand-bool-runs` runs for a target that stores a bool per element, which the row's
`boolArrays` column states: every bool array in Rust, Go, TypeScript and Python, and a
variable-length one in C++. The plan builder marks a run whose array's length varies, and the pass
turns each run the target stores a bool per element into an `scf.for` moving one element and one
bit per turn, through `dsdl.load_element` and `dsdl.write_bit` or `dsdl.read_bit` and
`dsdl.store_element`. Five spellings had each recovered the array from the run's address and
written the loop under indices of their own. C packs every bool array and takes the run as the plan
builds it; its output and the object lane's are byte for byte as before.

`dsdl-mark-unread-arguments` runs last and marks each argument its function never reads as
`llvmdsdl.unread`. C's `(void)` of a helper's operand, the accessor size C++ and Rust bind, and
Rust's `_buffer` read the mark. `dsdl-fold-unobserved-accessor-sizes` erases the composite getter's
size parameter with its write, so five spellings no longer ask whether a plan still reads it. No
generated byte changed.

`dsdl-fold-body-sizes` runs for a target whose body takes a buffer that carries its length and
answers the size it used, which the row's `bodiesAnswerSize` column states: Rust, Go, TypeScript and
Python. Each read of the size pointer becomes `dsdl.buffer_length`, and the write back becomes the
body's second result, an `index` as `dsdl.call_serdes_sized` answers it from the caller's side. What
computes the size moves to the body's top level where every operation of it is pure, and is
otherwise answered out of its arm, with zero from the arm that fails. The four spellings drop the
local each held the pointer in, and Rust its analysis of how the body used it. A conversion a
language spells as nothing names the value it converts, which is what Python's index conversions
are. A body of a definition with no fields reads nothing of its buffer, which TypeScript marks with
`void` and Rust names with a leading underscore, both from `llvmdsdl.unread`. The judges hold
everywhere, and Python's `E501` falls by one.

C's two walks went after them. The implementation file includes the header of each nested type a body
calls, and C's spelling records that header as it spells the call, so the bodies are spelt before
the includes are written and include what they call. The object lane clones the schemas whose
layouts a definition reaches, which `schemasReachedBy` answers beside the plan steps, resolving a
composite to its schema by symbol as the plan steps already did. No generated byte changed.

Rust's and Go's error types followed, on the infallible mark. A Rust body answers
`Result<usize, Error>` and a setter `Result<(), Error>`, over the runtime's `Error` enum; a Go body
answers `(int, error)` and a setter `error`, over the runtime's `Error`, a code as `syscall.Errno`
is. A body or setter marked unable to fail answers `Ok` or nil without testing its code. The plan
still carries its error as the runtime's code: each spelling names the error at the return and
reads the code back from a nested call's. Rust's `deserialize_with_consumed`, which answered a code
and the whole buffer on failure as the C harness reports them, went with them. C, C++, TypeScript,
Python and the object lane are byte for byte as before, and the Rust and Go judges hold.

Go's types then took `AppendBinary`, `MarshalBinary` and `UnmarshalBinary` over that pair, so
each satisfies the `encoding` package's interfaces, and `MarshalBinary` is `AppendBinary(nil)`. A type holding a view unmarshals `bytes.Clone(data)`, since its
views would keep the data the interface asks it not to keep. Whether a section holds a view,
directly or through a composite it holds, is `DefinitionIndex::holdsView`, which Rust's lifetime
reads too. A method's receiver is the initial of its type's head noun, the name's last word: `r`
for `ListRequest`, `h` for `Heartbeat`, `i` for `NodeID`.

The judges moved with it. TypeScript's `naming-convention` fell from 545 to 81, the `_bound0_` and
`_result1_` the spelling had invented, and Python's `SIM108` from 164 to none, the branch each call
split its answer with. Python's `E501` rose by 14, since a call and its split now take one line where
they took five.

With the bool runs, TypeScript's `naming-convention` fell to 75, the six `_bitN_` indices, and its
`no-unused-vars` to none. C++'s `modernize-use-auto` rose by three: the new loops convert their
index with a cast, which the C++ spelling of a cast declares with its type named twice.

**4 — The surface tree.** `project-dsdl-surface`, its ops, the allocator and the reference resolver,
as [The surface tree](#the-surface-tree) and [Spelling a reference](#spelling-a-reference) describe.
The tree reproduces today's output, so the phase changes no generated byte over the corpora. It lands
in eight steps, each with a gate that fails before the step and passes after it:

| step | what lands | gate |
|---|---|---|
| 4.0 | the oracle as a CI lane behind the `surface` label: `tools/determinism/corpus_determinism.py` over the merge base and the head, for every target, over the showroom, the regulated corpus and the views, accessors-only and adversarial fixtures, with the naming manifests and the lowered MLIR | green on a change that moves no output and red on a seeded one-byte change, which a self-test holds |
| 4.1 | `dsdl.surface`, `dsdl.scope` and `dsdl.decl`, their verifier, and `dsdl-opt` round-tripping | lit: a tree prints and parses back, a collision in one class is rejected, the same name in two classes is accepted, and an `of` that resolves to nothing is rejected |
| 4.2 | `allocateSurface`, with the composers and scope builders behind it, and the resolver with each language's `Lookup`; the manifest renders from the allocator's plan | every manifest byte-identical over the corpora; unit tests of the bands and of each lookup model; `llvmdsdl-language-classification` |
| 4.3 | Discovery's two collision checks call the allocator | Discovery's lit tests unchanged; the adversarial gate |
| 4.4 | the pass writes the plan into the module for its target and profile | lit tests of each language's tree; the tree's names equal the manifest's; `lower-dsdl-bodies-neutral.txt` unchanged; the oracle at zero |
| 4.5 | the emitters read the tree, one language per change, in the order Rust, C, Go, Python, TypeScript, C++, each deleting its own composition and scopes | per language: the oracle at zero; emission sites do not rise; `llvmdsdl-emitter-ir-queries` stays empty; the adversarial gate |
| 4.6 | a generation run's manifest reports the whole tree for its target | the new fields pinned in lit; the existing fields unchanged |
| 4.7 | each split pool merged into the scope the language has, one change per pool | an adversarial axis reaching the collision the split hid; the oracle at zero over the corpora |

Step 4.0 has landed as the `output-oracle` job. It runs twelve targets over seven corpora: C++ in
each profile, Rust in each memory mode, and the regulated corpus a second time with
`--optimize-lowered-serdes`. Those two options change definition files; the Rust profile and the
runtime specialisations change support files alone, and the oracle leaves them out.
`tools/determinism/test_corpus_determinism.py` holds the comparison against a stand-in `dsdlc`, and
the targets against the languages `dsdlc --help` lists.

Step 4.1 has landed. The verifier reads the name classes from each language's row, and
`dsdl-surface-roundtrip.mlir` and `dsdl-surface-invalid.mlir` hold a tree per language and each
defect the verifier rejects.

Step 4.2 lands in three parts: the definition layer, then the body layer, then the resolver. The
definition layer has landed. `allocateSurface`, in `llvmdsdl/Support/SurfacePlan.h`,
opens the scopes that hold the definitions and their type scopes, and declares each field, constant,
array metadata constant and option tag. The section scopes it allocates from are in
`llvmdsdl/Support/SectionScopes.h`, where the emitters' scopes are built too, and the manifest
renders the plan. Two composition columns came with it: `namespaces`, how the output opens a DSDL
namespace, and `constantsAreMacros`.

The body layer has landed. A definition's parts carry its lowered functions, and the plan names
each helper and, where the bodies are compiled apart, each body's link name, through the composers
in `llvmdsdl/Support/BodyNaming.h` that the emitters call too. The `helpers` composition column
states how a helper is named. A deprecated type is declared under a name of its own, with its
public name an alias of it, in C++ and Rust alike; the row had said C++ alone. `ImportNameScope`
moved to Support, keyed by a definition's identity. A language's own names -- its entry points,
accessors, generated members and the reservations its imports are claimed against -- move behind
the allocator in that language's step 4.5 change, where the oracle proves each one against the
output the emitter writes.

The resolver has landed, and step 4.2 with it. `spellReference`, in
`llvmdsdl/Support/SurfaceLookup.h`, repeats each language's lookup over a plan as its row's `Lookup`
states it, and `SurfaceLookupTests` holds each lookup model to the cases above. The `qualification`
composition column is `rooted` for C++ and `shortest` elsewhere. `namePartition`, in
`llvmdsdl/Support/SurfacePlan.h`, states how a language's classes of name fall into the partitions a
scope keeps apart, and the verifier and the resolver both read it.

Step 4.4 has landed. `project-dsdl-surface` runs last in `lower-dsdl-bodies` for a target, in every
codegen run, and writes the target's surface: one per C++ profile, rooted at the package the run
names, with a file's path where the language writes a definition to a file of a namespace
(`fileExtension` and `directoriesProjected` are the columns it reads). A DSDL namespace is a
`namespace` scope in every language and a `module` a definition's own, so a definition beside a
namespace of its name is two scopes; the verifier lets the two share the name where the row keeps a
file and a directory apart, as TypeScript does. Two versions of one definition may declare one
name, as the unversioned scheme generates them. Every codegen run's surface verifies, which the
oracle's runs prove over every corpus; `surface-tree.txt` holds a tree per language, and
`surface-tree-manifest.txt` the tree's names to the manifest's in every language.

Step 4.3 has landed. Discovery's checks -- the file stems and type names before parsing, a file
meeting a namespace's directory, and the type names a shared scope holds after parsing -- read every
name they compare from `allocateSurface`, and so does `checkGeneratedNameCollisions`. The plan now
declares a service's own name, as an alias of its request, where no section's type already has it.

Step 4.5 lands a language at a time, and Rust in two parts. The first has landed: Rust reads from
the tree the modules and namespaces it declares, the file each is written to, its type names and
aliases, its fields, constants and option tags, and the local name each `use` imports a definition
under, and its own composers and scopes for them are gone. The emitters read a surface through
`SurfaceTree`, in `llvmdsdl/Transforms/SurfaceTree.h`, beside the writer the pass calls. Four
composition columns place a scope in a file -- `sourceDirectory`, `packageDirectory`,
`namespaceFile` and `rootFile` -- and `surface-tree-paths.txt` holds every language's placements to
the files its output writes; a run of several C++ profiles places each profile's files in the
directory the profile names. The `imports` column states how a file names what it takes from
another definition's file, and the allocator claims band 5 where a file imports a definition's type
by name, as Rust and Python do, reserving what the file declares in the class an import is made in.

The second part has landed, and Rust's composition and scopes with it. Rust reads its helpers,
entry points, the functions that wrap them, its accessors, its generated constants and data members,
and a service's own constants from the tree. The members a language's type holds beyond its DSDL
names are spelling tables in `NamingPolicy`: `generatedTypeMembers`, `generatedDataMembers`,
`generatedServiceConstants`, `entryPointNames` and `memberAccessorVerbs`, each filled for a language
in its step 4.5 change. A generated declaration states a fact, the `fact` attribute of
`dsdl.decl`, so a reader finds `FULL_NAME` by what it states rather than by its spelling, and the
array metadata constants of C and C++ carry theirs. A structure's fields are a name class of their
own where the row's `fieldsApart` says so: a Rust field `get_foo` and the getter of a field `foo`
are two names. An accessors-only run declares no data member in any language, and the tree omits
them. Rust's bodies reserve the module's helper names against their locals.

C's change has landed. C reads every name it declares, and every file it writes, from the tree, and
the object lane links each body under the name the tree gives it. For C the allocator declares the
guards a header opens with, from `generatedFileGuards`; each structure's tag, where the row keeps
tags as a class of their own; the type's facts as macros beside it, and a service's under the
service's name; and the free functions the row's `freeFunctions` names: a wrapper of each body a
caller reaches, where the bodies are compiled apart from them, and a union's test and selector of
each option. C's header renderers take the names they write. In an accessors-only run the tree
declares C's structure, its tag, and its array and option macros, which the output leaves out.

Go's change has landed. Go reads its packages, files, types, fields, constants, methods, accessors,
helpers and package imports from the tree. A wrapper states what it does, `WireImage`,
`AppendWireImage` or `FromWireImage`, from a `generatedWrappers` table, since Go wraps its
serialisation twice; an entry point may be a function beside its type, as Go's constructor is; and
where the bodies are not compiled apart, each free accessor the row names is its body. A Go file
imports each other package under `pkg_` and its namespace, in band 5. Go writes a constructor only
where the initialiser stores something other than its type's zero, which the emitter decides from
the body, and the tree declares one for every section.

Python's change has landed. Python reads its package, the `__init__.py` of the package and of each
namespace, its modules, classes, fields, methods, accessors, helpers and imports from the tree. A
definition's facts at the top of its module are a `generatedModuleConstants` table, filled where
the row declares constants at module scope, so TypeScript's tree declares them too. A section's
bodies are the class's own methods, and the ones a consumer calls wrap them. The accessors are
claimed among the fields, after every field.

TypeScript's change has landed. TypeScript reads its modules, types, properties, constants,
functions, helpers and imports from the tree; the flat root `index.ts` stays with its emitter. An
entry point or a wrapper beside its type is named around the type's name, as `serializeMsgInto` and
`serializeMsg` are, and the row's `freeFunctions` names the accessors verb first, as
`getMsgSpeed`. A file imports each definition's type with the functions named after it, in band 5,
and a clash is judged on them all against everything the file declares. The tree declares each
function an import brings, where the file imports those it names: the factory of a union's option
the default does not select is not one. In an accessors-only run the tree declares the object type
and a service's alias, which the output leaves out.

C++'s change has landed, and with it every emitter reads the tree. C++ reads its namespaces,
headers, structures, data members, statics, functions, accessors and helpers from the tree, and
each profile's from a tree of its own: `allocateSurface` takes the profile, and `pmr` adds
`_memory_resource` to a structure that is not its wire image. The row's `freeFunctions` makes
each body a free function beside its type, as `Msg_serialize_`, and a service's own under the
service's name; the structure's `serialize` and `deserialize` call them. The `serviceConstants`
column names a service's constants beside its types as that scope names a type's constants:
`Ask_FULL_NAME` in C++ and `ASK_HAS_FIXED_PORT_ID` in Rust. An accessor's key keeps the verb's
`_`, and `joinedBeforeUnderscore` says whether its name does, which C++'s does not before a name
that begins with `_`. The tree does not declare `pmr`'s `set_memory_resource`: an accessor of a
field `_memory_resource` overloads it, and a scope holds one declaration of a name.

Step 4.6 has landed. A generation run writes its naming manifest once the lowering has run, and
reports for its target, beside the definition layer, every declaration the tree makes: each
definition's file, helpers, imports and guards, and each section's declared type, entry points,
wrappers, accessors, methods and generated members, keyed by the fact each states. A C++ run of
several profiles reports each profile's apart. `naming-manifest-surface.txt` pins the new fields
in every language and holds a generation run's manifest to an analysis run's, with nothing
changed or dropped.

Step 4.7 has landed. Of the splits the phase reproduced, one hid a collision: Go declared a type's
accessors in no scope, so a DSDL constant `GET_SPEED` beside a field `speed` gave it
`const FlatGetSpeed` and `func FlatGetSpeed`, which Go rejects. Go's and TypeScript's free accessors
are claimed among the values their file declares, after every one of them, and the getter moves to
`FlatGetSpeed2`; the adversarial corpus holds the case. The other splits hide none. C++'s second
scope went with its emitter's composition in step 4.5. A Rust or Python helper is a snake-case
function, which no value its module declares can spell, and TypeScript's constants are upper case.
`pmr`'s `set_memory_resource` and the setter of a field `_memory_resource` are overloads, which C++
allows. The verifier holds each scope to one declaration of a name, so a collision the allocator
misses fails the run rather than the build.

**5 — The declaration renderer.** `DeclarationSpelling` and the shared renderer; the hand-assembled
signatures and scope prefixes are deleted from all six emitters.

TypeScript's runtime is an embedded source, `runtime/ts/`, as every other language's is. `Ts.cpp`
had written it line by line in 340 of its 412 declaration sites. The count measures exactly what the
renderer replaces: C's header text in `CHeaderRender.cpp` and `CIncludes.cpp`, the import blocks and
every other line built as a string are counted, and the packaging writers are counted apart, in each
language's `Packaging.cpp` file. Retaken, the declaration half holds 569 sites -- C 155, C++ 99, Rust 89,
Go 98, TypeScript 68, Python 60 -- and packaging 54.

The renderer lands a language at a time, in the order C, Python, Go, Rust, TypeScript, C++, and each
language in two changes:

1. Its output changes. The defects a renderer would otherwise encode are fixed -- the doubled blank
   lines in C, C++ and TypeScript and in the accessors-only output of Go, Rust and TypeScript, and
   the `deserialize` buffer that C and C++ forward without `* const` -- and its bodies take the
   shape its language publishes, which removes its wrappers. Gate: the language's judge, the
   round-trip and parity lanes, and the oracle retaken.
2. Its declaration half moves onto the renderer, byte-identical against that output. C's change
   carries the renderer, the `DeclarationSpelling` interface and the facts view. Gate: the oracle
   byte-identical, and the language's declaration count, which must fall.

C's output change gives its accessors the member's types. `dsdl-type-accessors`, selected by the
row's `accessorsTakeMemberTypes`, types each accessor's value as the member is stored and its size
and index as an `index`, with the conversions in the body, so the C the backend writes and the
object the `obj` target assembles take one signature; the object target lowers an `index` to the
target's `size_t`. A composite getter writes its size only where the caller hands it a pointer. The
header publishes each accessor under the section's name by a forward that converts nothing.

C's renderer change moves C's declaration half onto `DeclarationRenderer`. C's layout names the
parts of its header and of its source file, and of a section; each generated constant is one
`#define` whose value is read off the facts by what the declaration states; each lowered function's
signature is spelt once, for its prototype, its definition and the forward that publishes it. C's
declaration count falls from 152 to 52.

Python's output change makes its bodies raise. A body and a setter raise `ValueError` with the text
of an error code, and each class derives from the runtime's `CompositeObject`, whose `serialize` and
`deserialize` compose the bodies once for every type; the class states the size `serialize` writes
into as `SERIALIZATION_BUFFER_SIZE_BYTES`, the first of the type's facts on the class. A `from` import
too long for a line is wrapped as `ruff format` writes it. Ruff's findings fall from 1,355 to 1,160:
the quoted annotations were the wrappers' return types.

Python's renderer change moves Python's declaration half onto `DeclarationRenderer`. Its section is
the first whose functions are defined inside its type: the layout opens the class, defines the
section's bodies and accessors in it, and closes it, and a helper is defined at module level. The
renderer finds a section's declarations in its type scope as well as in the file's. Python's
declaration count falls from 46 to 26.

C++'s output change nests its types, and comes last: nesting is the largest change to the surface
tree any language asks for, and taking it after five languages have exercised the tree tests it on
the shape that stresses it most.

**6 to 11 — One phase per language**, each flipping its row from *as today* to the target above and
turning its judge from phase 1 green. Phase 5 gives each language's bodies their public shape and
nests C++'s types; these phases carry the rest of each row. Rust's and Go's names landed ahead of the
mechanism, in #41 and #42, and C++'s accessors in #49 and #54. C is cheapest and can go anywhere.

Only phase 3 touches a plan body, and it moves what emitters decide into the IR they translate. The
wire is fixed by the round-trip, parity and cross-language equivalence lanes throughout, and a phase
that moves a wire byte has failed.

## Discipline

A fix belongs as high in the pipeline as it can go. A fact about a plan is an IR fact, stated once by
the lowering and translated by every backend. A fact about a language is a row in the
classification, read wherever it is needed. An emitter spells. A fix made lower than it could be is
made again by every language that meets the same defect, and by every language added later.

The work to #55 was audited against that at `8c2f0a7`. The lowering held: no pass names a language,
and #45, #47, #51 and #52 changed IR that every backend translates. #47 removed a C-only null test
from the header wrapper as it went. Three kinds of drift sat around it.

**Capabilities keyed on a language's name.** The driver decided host-image folding,
`TargetNullability` and `accessorsReturnViews` by comparing `--target-language` with each name, and
handed the pipeline positional booleans; the block grew in #33, #42, #45 and #49. Five files of
shared naming answered columns of the classification with `language ==`: the section joiner was the
nested-types column, `constantsShareTheFieldScope` the type's-own-constants column, `moduleScoped`
and the manifest's `goLike` the scopes column, and Python's `_` the internal-linkage column. Seven
places enumerated the languages: `CodegenNamingLanguage`, `ConstantLiteralLanguage`,
`StorageTokenLanguage`, the vocabulary's `LanguageSpec`, `allTargetLanguages`, `allOutputLanguages`
and the driver. Phase 2 moved every one onto the row.

**Plan semantics answered in emitters.**

| what | emitters | where it belongs |
|------|----------|------------------|
| a nested call's adaptation to a callee that answers its size or an error: the size clamped to the buffer, the result split, the size written back | Go, TypeScript, Python | the lowering; the names each invents are TypeScript's 545 `naming-convention` findings |
| a bool array's run of bits as a loop over one element per bool, beside `boolContainerOf` recovering the container from the address | C++, Rust, Go, TypeScript, Python | one expansion, where the classification says a target stores a bool per element |
| whether a body can fail, as `everyReturnIsZero` | Rust | an IR fact; the error types of Go, TypeScript and Python ask it too |
| whether a size or parameter is read, to declare or name it | C++, Rust, Go, TypeScript, Python | one signature decision |

**The declaration half grows by hand.** Each change to a language's public surface since this plan
was written -- Rust's in #41, Go's in #42, C++'s accessors in #49 and #54 -- was string code in its
emitter, and nothing measured it, so the renderer's gate could not fail.

A language added at `8c2f0a7` implements 49 `BodySpelling` hooks, about 1,200 lines with the
adaptations above among them; writes a declaration half of 1,200 to 1,600 lines by hand; and gains
a case in the driver's three capability blocks, three enumerations, five naming files, the
vocabulary and `Discovery`. Phases 2 and 3 are what bring that to a row and a spelling.

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

Python diagnoses least. A module that imports three classes under one name, or a class under the
name of the class it declares, byte-compiles and imports, and every use after the second binding
reaches the wrong class. The gate reads each Python module's top-level bindings, and a name bound
twice fails it.

## Acceptance

| gate | phases | holds |
|---|---|---|
| each language's own compiler and linter, at maximum strictness, over the regulated corpus | 1, then 6–11 | the output is accepted by the tools that judge that language |
| the adversarial corpus, compiled in every backend | 1 onwards | a generated name does not meet another generated name |
| the name emitted, asserted in lit | 1 onwards | a rule no compiler enforces still holds |
| byte-diff of all seven targets before and after, by `corpus_determinism.py` against the merge base | 2, 4, 5 | a mechanism change changes no output |
| no language compared by name outside the classification, `llvmdsdl-language-classification` | 2 onwards | a new language is a row, not a search |
| no emitter queries the IR to decide what a body means, `llvmdsdl-emitter-ir-queries` | 3 onwards | a plan's semantics are stated once, in the IR |
| emission sites per emitter, held to a baseline by `llvmdsdl-emission-sites` | 2 onwards, falling from 5 | the shape is stated once, the syntax six times |
| round-trip, the C↔language parity lanes, cross-language equivalence | all | the wire is unchanged |
| a generation run's naming manifest against the surface tree | 4 onwards | the manifest reports what the backend writes |
| no in-source diagnostic suppression in generated output | 6–11 | a warning is answered by changing what is emitted |

The last row is the existing rule, applied where it was not. `#pragma GCC diagnostic ignored` was
removed from generated C and C++ in #24 for the same reason the three Rust `#![allow]` lines were
removed in #41: a suppression moves a build-policy decision into source the consumer compiles, and
hides whatever else lands inside it.

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
twice. The names landed in #41 and #42 and the error types in #57, all after v0.3.0.

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
short name and do not move. `Discovery` reads the same name from `allocateSurface`, so the
collision check keys on the stem that is written.

C++ keeps its status code. The AUTOSAR profile is C++14 and the embedded profiles run without
exceptions, so neither an exception nor `std::expected` is available across the targets the backend
serves.

**C++ field accessors take and answer spans.** A getter takes a span of `const std::uint8_t`, a
setter one of `std::uint8_t`, and a composite getter answers the nested type's bytes as a span, as
Rust, Go, TypeScript and Python answer a slice or a view. A pointer beside a size, with the size
written back through another pointer, is C's idiom; in C++ it let `get_timestamp(buffer, size,
nullptr)` compile and write through the null pointer. The `std` and `pmr` profiles therefore require
C++20. Serialise and deserialise take a pointer and an in-out size. A member held as a view under
`--aliasable-views` is a span of `const std::uint8_t` too, so a holder's member goes to the nested
type's accessors as it is.

**The backend names no library.** Which span a profile uses is a binding in a `--vocabulary` file
([reference](docs/reference/codegen/vocabulary.md)); the backend states the concept -- the
operations it performs -- and the built-in bindings name the standard library alone. The first cut
spelt CETL's `cetl::pf20::span` inside the emitter and the AUTOSAR runtime, which made the profile's
library a property of the compiler. The `autosar` profile is C++14 and has no built-in span, so it
fails until a file binds one; no fallback stands in. There are no named packs: CETL's binding is an
example file, not a flag. The concepts that follow -- a bounded vector, an optional, a variant --
arrive one change each, when generated code needs them, each with a lit fixture whose binding
overrides every operation.

**A body stays a flat function, and the tree places it.** The tree names each schema and function
and decides where each language declares it; the IR's symbols and the bodies' references do not
change. Moving the bodies into scope ops would produce the same output after converting three kinds of
string reference to symbols and changing every consumer that reads a flat module at once
([The surface tree](#the-tree-names-the-ir) lists them).

**Rust moves onto the tree first**, then C, Go, Python, TypeScript and C++. Rust's output is already
its target shape, so moving it first tests the tree against the shape it exists to express. C first
would have proven the object lane's integration in the first change; C second still proves it before
four more emitters depend on the tree.

**An analysis run's manifest reports the definition layer.** Build rules and the language server's
hover read it, and neither needs a helper's or an entry point's name; a build rule that does reads a
generation run's manifest. Lowering once per language would have made the analysis manifest complete,
at six lowerings per analysis run.

**The tree is rooted at the generated package.** A root namespace that meets one of the generator's
root-level names is then reported with every other collision. Rooting it at the DSDL namespaces would
have kept those names in each language's reserved-name table only, and given the tree no scope
without a DSDL source.

**No declaration wraps a body.** A wrapper is a function the generator writes around one lowered
function to adapt its shape: TypeScript's and Python's functions that raise on the status code their
body returns, C++'s member that forwards to a free function. Each adapts a shape the generator chose, so the generator writes the shape instead. The
interface that allocates and returns bytes composes the primitive the same way for every type, so it
lives once in each runtime rather than in every type. C's unversioned public name over each versioned
link name stays, so that two versions of a type link into one program.

**A language's output changes before its renderer change.** The renderer is then held
byte-identical to the output it keeps, and never learns a wrapper or a defect; the public API
changes during phase 5, a language at a time. Holding phase 5 to today's bytes would have written
each wrapper and defect into a spelling to delete in the language's phase.

**A signature is the declaration spelling's.** A signature is a function's name, its parameters and
their types, its return type, and the qualifiers that place it. The renderer asks for a lowered
function's signature once and writes it as the prototype and as the definition. C and C++ build the
two apart, and C++'s copies disagree on a parameter's `const`. `BodySpelling::openFunction` names
the parameters and writes the body's opening lines, such as C++'s memory-resource lines.

**The renderer lands with C, then Python, Go, Rust, TypeScript and C++.** C's output change is the
smallest, so its renderer change is mostly the renderer itself, and C's shape tests the interface
hardest short of nesting: two files per definition, prototypes, guards, macros, and a tag beside its
typedef. Python is the simplest class-member shape, and C++ comes last with its nesting.

**Python's classes derive from `CompositeObject`.** The name is the one PyCyphal gives the base of
every class generated from DSDL. The size `serialize` writes into is a public class attribute,
`SERIALIZATION_BUFFER_SIZE_BYTES`, where Python's target puts every fact of the type, rather than a
private one held apart until Python's phase names them.

**The count measures exactly what the renderer replaces.** It scans every file of a language's
declaration half and counts each line built as a string, since C's header text in
`CHeaderRender.cpp` and `CIncludes.cpp` and every import block would otherwise read as new when the
renderer writes them. Packaging files are no declaration the renderer writes, so each language's
packaging writers move to a support file, counted as a third figure that may not rise either.

**TypeScript's index becomes per-directory barrels, in TypeScript's phase.** A module's public path
then follows from its own DSDL name, so adding a definition cannot rename another's export. The
public import surface changes; keeping the flat index would have kept it, with each alias unique only
against the run's other aliases.
