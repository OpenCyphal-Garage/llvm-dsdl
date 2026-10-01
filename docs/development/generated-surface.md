# Generated Surface

A backend translates the bodies the pipeline builds, as [Backend Translation](backend-translation.md)
describes. Around those bodies it declares a surface: the types, their fields and facts, the
functions a consumer calls, the files they are written to and the imports between them.

The surface is held to one standard: an engineer who knows the language finds nothing in it
surprising. Where a language's own convention and consistency across the six languages disagree,
the convention wins.

Five mechanisms produce and hold it. A row per language states what the language can express. The
surface tree names every declaration and places it in a scope. One resolver spells each reference
from the tree. One renderer writes the declarations through a spelling per language. Each
language's own linter judges the output.

## Each language's shape

`uavcan.file.List.0.2`, from the regulated corpus, under the default type names:

| | the request section | its serialise | a fact | a helper |
|---|---|---|---|---|
| C | `struct uavcan__file__List__Request` | `uavcan__file__List__Request__serialize_` | `uavcan__file__List__Request_EXTENT_BYTES_` | `static capacity_check_request` |
| C++ | `uavcan::file::List::Request` | the member `serialize` | `List::Request::EXTENT_BYTES` | the private static member `capacity_check` |
| Rust | `uavcan::file::list_0_2::Request` | `Request::serialize` | `Request::EXTENT_BYTES` | the module's private `capacity_check_request` |
| Go | `file.ListRequest` | `(*ListRequest).Serialize` | `ListRequestExtentBytes` | `listCapacityCheckRequest` |
| Python | `uavcan.file.list_0_2.ListRequest` | `ListRequest.serialize` | `ListRequest.EXTENT_BYTES` | the static method `_capacity_check` |
| TypeScript | `uavcan.file.list_0_2.ListRequest` | `ListRequest.serialize` | `ListRequest.EXTENT_BYTES` | the module's unexported `capacityCheckRequest` |

A service's section is named after the service with nothing between the two in Go, Python and
TypeScript: `GetInfoRequest`.

**C** has no scope below the file, so each identifier carries its path, joined by `__`. A body is
compiled in the definition's source file under the name its entry point takes for the versioned
type, `uavcan__file__List_0_2__Request__serialize_`. The header publishes it as
`uavcan__file__List__Request__serialize_` through a `static inline` forward that converts nothing,
so two versions of one type link into one program. Under versioned type names the two names are
one, and the header declares the body itself. A type's facts and DSDL constants are macros beside
it. A helper is `static` in the definition's source file, named by its section, step and
direction. Each header's guard is named from the DSDL name, its parts joined by `_`.

**C++** nests. A service is a struct that holds the service's own facts as static members and
encloses `Request` and `Response`. `serialize` and `deserialize` are members over `this`, and one
that reads no object is static. A type's facts and constants are `static constexpr` members. A
helper is a private static member of its section's struct, named by its step and direction. The
type is the scope, so nothing is declared beside it: no free entry point and no `detail`
namespace. A class's members may not take its name, so a service named `Request` is declared as
`Request_` and published under the alias `Request`, as a deprecated type is.

A C++ profile states the standard its output is written to: C++20 for `std` and `pmr`, C++14 for
`autosar`. A profile at C++17 or later opens `namespace uavcan::file {`. A `std` or `pmr` body
declares a cast's result `auto`, and an `autosar` body names the type, since AUTOSAR C++14's rule
A7-1-5 refuses `auto` for a fundamental type. A header's guard joins its parts with `_`, since C++
reserves `__`.

**Rust** has no type nested in a type, so a definition's module carries the enclosure: `pub struct
Request` in `uavcan::file::list_0_2`, with `impl Request` holding `serialize`, `deserialize`,
`to_bytes`, `from_bytes` and the type's facts as associated consts. A helper is a private `fn` of
the module, named by its section, step and direction. `serialize` and `deserialize` answer
`Result<usize, Error>` over the runtime's `Error`, an enum deriving `Debug` and implementing
`Display` and `core::error::Error`, which `core` has carried since Rust 1.81, so `no_std` keeps it.
The variants are values, so returning one allocates nothing, and a code the runtime does not define
is `Unrecognised`, which carries the code. A struct whose initialise body sets every member to its
type's `Default` derives `Default`.

**Go** has no scope between the package and the type. A package holds a whole DSDL namespace, and
its names are distinct across every file in its directory. A package's name is one lower-case word,
in a directory of that name, and is documented in that directory's `doc.go`:
`uavcan.si.sample.angular_acceleration` is `angularacceleration`.

A Go type implements `encoding.BinaryAppender`, `encoding.BinaryMarshaler` and
`encoding.BinaryUnmarshaler`, so it drops into anything that already speaks them. `AppendBinary`
appends to a slice the caller owns, which allocates nothing once its capacity suffices, and
`MarshalBinary` answers a new one. A type holding a view unmarshals a copy of its data, which the
interface asks it not to keep. `encoding.BinaryAppender` is Go 1.24's; the method needs nothing
newer than the module's `go 1.22`. Beside them, `Serialize` and `Deserialize` write into and read
from a slice the caller has sized, and answer `(int, error)`. The error is the runtime's `Error`, a
code whose named values are constants such as `ErrBufferTooSmall`, as `syscall.Errno` is: returning
one costs no allocation, and every code has a value.

A Go name takes Go's case. A constant or a field is exported and a helper unexported, and each word
of a source with no lower case is recased, the capitals of a word that has one are kept, and the
words `ST1003` calls initialisms are upper-cased: `FULL_NAME` is `FullName`, `unique_id` is
`UniqueID`, and `VSLAMPoseUpdate` stays. A constant and a helper carry their type's name, and an
ordinal joins with nothing: `FooBar2`. A method's receiver is the initial of its type's head noun,
the name's last word: `r` for `ListRequest`, `h` for `Heartbeat`. A doc comment opens with a
sentence naming its declaration, and every file carries Go's mark of generated code.

**Python** declares a section as a dataclass deriving from the runtime's `CompositeObject`. The
class holds the type's facts, a union's option tags and the DSDL constants as class attributes:
`Heartbeat.MAX_PUBLICATION_PERIOD`. `serialize` answers new bytes, and `deserialize` is a class
method. A body raises `ValueError` with the text of an error code. A helper is a static method of
its section's class, which a method reaches through `self`, a class method through `cls`, and a
static method by the class's name. A service's own facts stay beside its sections, named after it
(`LIST_FIXED_PORT_ID`), and its name is an alias of its request: `List = ListRequest`. The class
claims the facts' names, so a DSDL constant named `FULL_NAME` is `FULL_NAME_`. A line is kept to
120 characters, the width the DSDL sources are written to, which the generated `pyproject.toml`
states as ruff's `line-length`.

**TypeScript** declares a section as an interface and a `const` of the same name. The interface
declares into the type namespace and the `const` into the value namespace, so `ListRequest` is both
the type a consumer annotates with and the object its operations hang off, and
`ListRequest.deserialize(bytes)` completes in an editor. The `const` holds the type's facts, a
union's option tags, the DSDL constants, the factory `create`, the bodies `serializeInto` and
`deserializeFrom`, the entry points `serialize` and `deserialize`, and the accessors. A body throws
an `Error` with the text of an error code, and reaches a global through `globalThis` where its
module binds the global's name, as `uavcan.file.Error`'s does. A helper is a module-private
camelCase function. A service's own facts stay beside its sections, named after it, and its name
stands for its request's interface and `const`.

Each TypeScript directory's `index.ts` re-exports its namespaces' indexes and then its modules,
each under its own name, so a consumer imports `uavcan` from the package's root and reaches
`uavcan.node.heartbeat_1_0.Heartbeat`.

## Language classification

The rows are `LanguageTraits`, in
[`include/llvmdsdl/Support/LanguageTraits.h`](https://github.com/OpenCyphal-Garage/llvm-dsdl/blob/main/include/llvmdsdl/Support/LanguageTraits.h),
and `LanguageTraitsTests` pins each. A row has three parts:

- the classification, what the language can express;
- `BodyInterface`, what the generated interface hands a body, which the lowering folds against;
- `Composition`, how the output composes its declarations where the language leaves a choice: its
  files and directories, how a file imports, where a helper is declared, what encloses a service's
  sections, the free functions beside a type, and the length of a line.

The classification:

| | scopes below the file | nested types | methods | internal linkage | errors | a type's constants | fields a class of their own | reserved by underscores | name classes in one scope | `if` is an expression |
|---|---|---|---|---|---|---|---|---|---|---|
| C | none | no | none | `static` | status code | macros in the enclosing scope | yes | leading `__`, `_X` | ordinary identifiers and structure tags; macros across the translation unit | no |
| C++ | namespace, class | yes | members | private member | status code | `static constexpr` in the class | no | those, and `__` anywhere | one; macros across the translation unit | no |
| Rust | module | no | in `impl` | private by default | `Result<T, E>` | associated `const` | yes | none | types, modules among them, values, and fields | yes |
| Go | none below the package | no | by receiver | lower-case initial | `(T, error)` | package scope, carrying the type's name | no | none | one per package, across its files | no |
| Python | class | yes | members | `_` prefix | exception | class attribute | no | none | one | no |
| TypeScript | namespace, class | yes | members | not exported | exception | properties of the type's `const` | yes | none | types and values; modules by path | no |

A row is a claim about the language, not a preference, which makes it testable and keeps it out of
the emitters. A column is added when a backend needs one: generic parameters, trait or interface
conformance, finer visibility or operator overloading. A preference stays with the profile that
holds it: `pmr` versus `std` containers is a profile, not a capability.

Code that needs a fact about a language reads its row.
[`tools/check_language_classification.py`](https://github.com/OpenCyphal-Garage/llvm-dsdl/blob/main/tools/check_language_classification.py),
run as `llvmdsdl-language-classification`, refuses a comparison with a `Language` enumerator, a
`case` on one, or a comparison with a language's `--target-language` spelling, outside the
spelling tables and the driver's dispatch.

## The surface tree

A pass, `project-dsdl-surface`, runs last in `lower-dsdl-bodies` for a target. It builds a tree of
the scopes that target's output opens, with every identifier the output declares allocated in its
scope, and the emitters read names and placements from the tree. The pipeline registered without a
target builds no tree.

### The tree names the IR

An IR symbol is an identity: it says which schema, helper or body a reference means. A name in the
tree is a spelling, and so is its place. In C++ the tree puts `serialize` in `List::Request` and
`capacity_check` there as a private static member, and the renderer writes the flat `func.func` the
pipeline built into that position. Clang compiles C++ member functions to flat functions in LLVM IR
in the same way: the nesting lives in the declarations.

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

A symbol is renamed only in the object lane, before LLVM conversion, to the C link name its header
declares; there a symbol is also what a linker reads.

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
classes column of its row, and the verifier holds the tree to them:

- every scope but a `file` scope is a namespace. A `file` scope declares into the namespace around
  it, as a C or C++ header and a Go file do; its imports are its own, as Go's file block holds them,
  and may not meet a name the namespace declares;
- a type scope's name is a type in its namespace, and a namespace, module or package scope's name is
  a module there, which the row puts among the types or keeps apart as a path;
- macros are one class across the surface, the translation unit a set of headers can meet in;
- no two scopes are written to one path, and every path stays within the output directory;
- every `of` resolves against the module's symbol table, a type scope's to a schema, and a `section`
  or `member` to one the schema declares;
- a scope holds one declaration of a name, so a collision the allocator misses fails the run rather
  than the build.

### Declarations and the root

The tree holds every name declared at file, namespace and type scope, generated ones included:
metadata constants, guards, `to_bytes`, `AppendBinary`, pool constants. A DSDL constant's ordinal
depends on the generated names claimed before it, so the generated names are allocated with the
rest. Names a language binds in a scope by importing them are declarations too: Python's
`dataclass` and `field`, TypeScript's `dsdlRuntime`, Go's package names. Names inside a function
stay with the body translator's value naming.

The root is the generated package: the crate, the npm package, the Python package or the Go module,
and for C and C++ the output directory. It carries the package's name from the command line or its
default, and holds the generator's root-level names as generated declarations: Rust's `dsdl_runtime`
module, Python's `_runtime_loader`, `_dsdl_runtime` and `__version__`, Go's `dsdlruntime`,
TypeScript's `dsdl_runtime`, and the identifiers the C and C++ runtime headers declare. A root
namespace that meets one of them is reported with every other collision.

The root namespaces are the root's children, and each namespace scope names the file it is written
to: Rust's `mod.rs`, Python's `__init__.py`, Go's `doc.go` and TypeScript's `index.ts`. Where the
row's `fileAndDirectoryAreOneModule` holds, as in Rust, Python and TypeScript, a directory's
subdirectories and files share one name class, and `dsdlc` refuses a run that writes a definition's
module beside a namespace's directory of one name.

A tree is built per language and C++ profile. `pmr` declares `_memory_resource` and
`set_memory_resource`, and `--cpp-profile both` builds two trees; the Rust profile and memory mode
declare the same items. The object lane reads C's tree.

### One allocator, three callers

Discovery checks collisions for every language inside `parseDefinitions`, which the language server
calls on each analysis. An analysis run renders the naming manifest for every language before any
MLIR exists, and the pass runs after lowering, for one target. All three call one library,
`allocateSurface`, in
[`include/llvmdsdl/Support/SurfacePlan.h`](https://github.com/OpenCyphal-Garage/llvm-dsdl/blob/main/include/llvmdsdl/Support/SurfacePlan.h),
which takes a language row, a profile and the definitions as parts and answers a surface plan.

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
lowering decides which bodies exist; the rest of bands 1 to 3 is the definition layer.

## Spelling a reference

A reference is written the way a person writing at that spot would write it: as the shortest spelling
that the language's own name lookup, starting at the reference, resolves to the declaration it means.
Inside `struct Request` a constant is `EXTENT_BYTES`; outside `List` it is
`List::Request::EXTENT_BYTES`.

`spellReference`, in
[`include/llvmdsdl/Support/SurfaceLookup.h`](https://github.com/OpenCyphal-Garage/llvm-dsdl/blob/main/include/llvmdsdl/Support/SurfaceLookup.h),
answers each spelling from the plan and the row's `Lookup`, for a site that is a scope and what the
enclosing function is to its type: free, a method handed an instance, a static member, or a method
handed its class. Nothing stores a spelling per reference. It tries candidates shortest first and
writes the first that resolves:

1. the bare name;
2. the receiver form, where the language reaches the site's own type through one: Rust's `Self::`,
   Python's `self.` or `cls.`, TypeScript's type `const`;
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
| TypeScript | the block, each enclosing function, the module's declarations and imports, the globals | through the type's `const` | bare |
| Python | the function, each enclosing function, the module's definitions and imports, the builtins | `self.X`, `cls.X` in a class method, the class name in a static method | `cls` in a class method, the class name elsewhere |

A shorter spelling is safe only where no declaration the resolver cannot see could capture it, so the
resolver shortens through two kinds of scope: a definition's own scopes, which one file declares
whole, and the C++ namespace or Go package a definition shares with the run's other definitions of its
DSDL namespace, whose names Discovery and the verifier keep distinct. An enclosing C++ namespace is
open to any header, so a reference that leaves its own namespace is written from the root:
`uavcan::si::unit::length::Scalar`. A spelling captured by a nearer declaration is lengthened until it
resolves; a field named `Path` of type `Path` is declared `uavcan::file::Path Path{};`.

## The declaration renderer

`translateFunction` holds the structure of a body and asks a `BodySpelling` for the language. The
declaration half takes the same shape: `DeclarationRenderer` walks the surface tree, opening and
closing scopes, emitting declarations in dependency order and placing definitions, and asks a
`DeclarationSpelling` for the syntax.

The renderer holds the order of a file's parts, from a layout each language states; scope nesting;
forward declarations where a language needs them; which declarations are public; and where a body is
attached. The spelling holds how the language opens a namespace, writes a field, declares a
constant, spells a signature, marks a declaration internal, and returns an error. A lowered
function's signature is spelt once, from its IR function's types and its declaration's place in the
tree, and serves as the prototype C and C++ declare and as the definition; `translateFunction` writes
only the body. The signatures of functions no body lowers, such as Go's constructor, are the
spelling's too. What the tree does not hold, such as a field's type and default, a constant's value,
a section's metadata or a doc, the spelling reads from a view of the definition's facts the renderer
hands it. Every reference the renderer writes, in a declaration or a body, is spelt by
`spellReference`.

No declaration wraps a body. A body is written in the shape its language publishes: the member's
types, the row's error convention, and a member or a free function where the language places it.
The interface that encodes a value into bytes and decodes one from them is a body too:
`dsdl-build-wire-image-bodies` builds it over each section's serialise and deserialise for every
language whose row publishes it, and each language translates it as it translates the pair. C
publishes an unversioned name over the versioned link name it compiles each body under, which
unversioned type names require.

[`tools/count_emission_sites.py`](https://github.com/OpenCyphal-Garage/llvm-dsdl/blob/main/tools/count_emission_sites.py)
counts the places in each emitter that write generated text, in three figures: the `BodySpelling`,
the declaration half, and the packaging writers each language keeps in a support file.
`llvmdsdl-emission-sites` holds the counts to
[`test/integration/emission-sites.json`](https://github.com/OpenCyphal-Garage/llvm-dsdl/blob/main/test/integration/emission-sites.json),
so a count rises only by a retake whose commit says why the renderer could not state what was added.
The declaration figure counts every line built as a string in every file of a language's declaration
half, import blocks and C's header text included.

A template engine is the usual answer to string concatenation in a code generator, nnvg's included,
and this compiler has none. A template is a second statement of the shape, in a language the
compiler cannot check. Here the
shape lives in a classification a pass reads: a new language is a row and a spelling, and its shape
follows from what the language can express.

## Imports

A generated file's imports are the modules that provide the names it writes, and nothing else.
`ImportSet`, in
[`include/llvmdsdl/CodeGen/ImportSet.h`](https://github.com/OpenCyphal-Garage/llvm-dsdl/blob/main/include/llvmdsdl/CodeGen/ImportSet.h),
is where that is decided. The code that writes a name from another module names it through the
file's set, which records the module and answers the spelling; the file is rendered first, and its
import block is written from the set. A backend states where each of its names comes from and how
its language writes an import. It keeps no list of imports a file may need.

Reading the imports back off the rendered text is the obvious shortcut, and wrong: a doc comment
that mentions `bytes.Clone(` would import a package the file does not use, which Go refuses to
compile.

- **Python** imports `sys` where a body bounds an index, `dataclasses` where a class and its
  defaults are declared, the runtime where a body calls it, and a nested type where the file names
  it.
- **Go** imports `unsafe` where a body moves an object as bytes or a layout is asserted, `slices` and
  `bytes` in the encoding methods, the runtime where a body calls it, and a nested type's package
  where a field names it. The module's own packages are written in the order of their paths, which
  is gofmt's.
- **TypeScript** imports the runtime's namespace where a body or entry point calls it, and a nested
  definition's interface and `const`, by its name. A name used in type positions alone is imported
  with `import type`.
- **C++** includes a standard name from the header the standard declares it in, a C runtime function
  from `dsdl_runtime.h`, a C++ runtime name from `dsdl_runtime.hpp`, the vocabulary's types and
  operations from the headers the binding names, and a nested type from its own header where a
  declaration or a body names it.
- **C** includes a standard name from the header the standard declares it in, a runtime name from
  `dsdl_runtime.h`, and a nested type or entry point from its own header where a declaration or a
  body names it. The header and the implementation file each record their own.
- **Rust** imports a nested definition's struct where a field or its default names it, under the
  local name the surface tree declares for its import. A standard or runtime name is written as its
  full path, which needs no import. Its `use` declarations are in the order of their paths, which is
  rustfmt's.

C and C++ write their includes alike, in `c::renderIncludeLines`: three groups set apart by blank
lines, the standard library's, a bound library's and the generated tree's own, each in the order of
its paths. An implementation file includes its own header ahead of them, which declares the bodies
it defines; the file itself declares only its helpers, which the lowering may define after the body
that calls them.

A language that gives an import a local name records the name the file spells. Rust, TypeScript and
Python claim it in one `ImportNameScope`, in
[`include/llvmdsdl/Support/ImportNameScope.h`](https://github.com/OpenCyphal-Garage/llvm-dsdl/blob/main/include/llvmdsdl/Support/ImportNameScope.h),
which reserves what the file declares before any import is claimed, so the import is what moves, and
qualifies a clash with as much of its namespace as tells it apart: `TemperatureScalar`. Go aliases a
package rather than a type, under `pkg_` and its namespace. The allocator claims every import in
band 5.

## Style judges

A compiler accepts an un-idiomatic name by design, so each language's own linter judges the
generated code. The lanes are `llvmdsdl-uavcan-<language>-style-judge`, one per language, each
generating the regulated corpus and running
[`tools/judge_generated_code.py`](https://github.com/OpenCyphal-Garage/llvm-dsdl/blob/main/tools/judge_generated_code.py)
over it:

| language | judge |
|---|---|
| C, C++ | `clang-tidy`, under a ruleset per language in `test/integration/judge-rulesets/` |
| Rust | `cargo clippy -- -D warnings` |
| Go | `staticcheck`, every check |
| Python | `ruff`, `E,F,W,N,UP,B,SIM,RUF` |
| TypeScript | `eslint` 10 or later with `typescript-eslint`'s `naming-convention` |

Each lane holds its judge to a per-rule baseline in
[`test/integration/judge-baselines/`](https://github.com/OpenCyphal-Garage/llvm-dsdl/tree/main/test/integration/judge-baselines).
The comparison is per rule, so one rule cannot grow behind another shrinking, and a rule absent from
a baseline is a regression at any count. A baseline records the judge's version and is not compared
against another version: off CI the mismatch is a skip, and on CI, where the image pins every judge,
it is a failure. A lane is registered only for a judge the build can find, and
[`tools/assert_style_judges.py`](https://github.com/OpenCyphal-Garage/llvm-dsdl/blob/main/tools/assert_style_judges.py)
fails a CI image or devcontainer that lacks one.

The baselines hold every judge at zero except these, each left by decision:

- C++, `readability-identifier-naming` (3): a deprecated type declared apart keeps its trailing `_`.
- C and C++, `readability-non-const-parameter` (5 each): every serialise takes a writable buffer,
  which an empty section's serialise does not write to.
- C, `clang-analyzer-security.ArrayBound` (1): the analyser follows a path through the runtime's
  bit copy that copies a whole byte and a partial one in a copy of at most eight bits, which cannot
  both happen.

A ruleset is the project's own `.clang-tidy` applied to generated code: the same families, with a
check subtracted only where its reason holds there, and the reason written beside it. Every header
is judged as a translation unit of its own, because `misc-include-cleaner` reads only a unit's main
file. The C++ judge reads the `std` profile at C++20, the lowest standard that profile compiles
under, so no modernize check suggests what a consumer on C++20 could not write.

eslint reads a `.ts` file only through the TypeScript parser, and without one it lints nothing and
exits zero, so the parser is asserted beside eslint, and so is the version `eslint` resolves to: an
image can carry eslint 10 as a dependency of `typescript-eslint` while `/usr/bin/eslint` is an
older release that predates flat configuration. Go's mark of generated code turns staticcheck's
naming and doc-comment checks off, so the judge reads a copy without it. Python's judge reads the
line length from the generated `pyproject.toml`.

## The adversarial corpus

`Discovery` rejects a corpus in which two DSDL names reach one generated identifier. The adversarial
corpus covers the other class: a DSDL name reaching an identifier a backend emits for every type,
such as a trait its bodies name unqualified, a module its crate root declares, a global the standard
headers put beside a namespace, an accessor composed from another field, or a union's synthetic tag.
Reaching one needs a name pair the regulated corpus has no instance of.

[`test/integration/generate_naming_adversarial_corpus.py`](https://github.com/OpenCyphal-Garage/llvm-dsdl/blob/main/test/integration/generate_naming_adversarial_corpus.py)
writes the corpus rather than checking it in, so an axis is a name in a list, and
`RunNamingAdversarialGate.cmake` hands each backend's output to that language's own compiler.

The gate compiles, so it finds what a compiler diagnoses and nothing else. `namespace std`
compiles: [namespace.std] makes a declaration added to it undefined behaviour rather than a
diagnostic. A rule the language states and no compiler enforces needs an assertion on the name
emitted, as `naming-stropping.txt` asserts for `std`. Compile gates and text assertions are two
halves of one check.

Python diagnoses least. A module that imports three classes under one name, or a class under the
name of the class it declares, byte-compiles and imports, and every use after the second binding
reaches the wrong class. The gate reads each Python module's top-level bindings, and a name bound
twice fails it.

## Acceptance

| gate | holds |
|---|---|
| each language's own compiler and style judge over the regulated corpus | the output is accepted by the tools that judge that language |
| the adversarial corpus, compiled in every backend | a generated name does not meet another generated name |
| the name emitted, asserted in lit | a rule no compiler enforces still holds |
| `corpus_determinism.py --matrix oracle` against the merge base, on the `surface` label | a mechanism change changes no output |
| `llvmdsdl-language-classification` | no language is compared by name outside the classification |
| `llvmdsdl-emitter-ir-queries`, an empty inventory | no emitter queries the IR to decide what a body means |
| `llvmdsdl-emission-sites` | string-built emission does not grow unremarked |
| round-trip, the C↔language parity lanes, cross-language equivalence | the wire is unchanged |
| a generation run's naming manifest against the surface tree | the manifest reports what the backend writes |
| no in-source diagnostic suppression in generated output | a warning is answered by changing what is emitted |

A suppression such as `#pragma GCC diagnostic ignored` or `#![allow(non_snake_case)]` moves a
build-policy decision into source the consumer compiles, and hides whatever else lands inside it.

## Decisions

**C++ section types are nested.** `uavcan::file::List::Request`. Source compatibility with nnvg's
C++ output is not a goal of this compiler.

**C++ emits no free entry points.** An operation on a type is a member of that type, and a static
operating on a type is a static member of it, helpers included.

**C++ keeps its status code.** The AUTOSAR profile is C++14 and the embedded profiles run without
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
([reference](../reference/codegen/vocabulary.md)); the backend states the concept, the operations it
performs, and the built-in bindings name the standard library alone. The `autosar` profile is C++14
and has no built-in span, so it fails until a file binds one; no fallback stands in. CETL's binding
is an example file, not a flag, and there are no named packs. Each further concept, such as a
bounded vector, an optional or a variant, arrives in a change of its own when generated code needs
it, with a lit fixture whose binding overrides every operation.

**TypeScript emits an interface.** Generated wire types are data that crosses serialisation
boundaries, such as `JSON.parse`, `structuredClone` and a worker's `postMessage`, and each of those
returns a plain object. A class survives none of them: the prototype is dropped, the methods with it,
and `instanceof` answers false. An interface is erased at compile time and describes what comes
back. [Protobuf-ES took this direction in 2.0](https://buf.build/blog/protobuf-es-v2), dropping the
classes it generated in 1.0 for plain objects and a schema value. The `const` of the type's name
gives the discoverability a class would.

**TypeScript's index is a barrel per directory.** A module's public path follows from its own DSDL
name, so adding a definition cannot rename another's export.

**A file stem is projected from the versioned name, in every language.** `Break.1.0` composes to
`break_1_0` before the keyword check, so it needs no escape, and no separator before the version
leaves the `__` that `non_snake_case` reports in a module name. One composition rule serves every
module-scoped language. C and C++ name a header after the raw short name. `Discovery` reads the same
name from `allocateSurface`, so the collision check keys on the stem that is written.

**A body stays a flat function, and the tree places it.** The tree names each schema and function
and decides where each language declares it; the IR's symbols and the bodies' references do not
change. Moving the bodies into scope ops would produce the same output after converting three kinds
of string reference to symbols and changing every consumer that reads a flat module at once
([The tree names the IR](#the-tree-names-the-ir) lists them).

**The tree is rooted at the generated package.** A root namespace that meets one of the generator's
root-level names is then reported with every other collision. Rooting it at the DSDL namespaces would
keep those names in each language's reserved-name table only, and give the tree no scope without a
DSDL source.

**An analysis run's manifest reports the definition layer.** Build rules and the language server's
hover read it, and neither needs a helper's or an entry point's name; a build rule that does reads a
generation run's manifest. A complete analysis manifest would cost six lowerings per analysis run.

**No declaration wraps a body.** A wrapper is a function the generator writes around one lowered
function to adapt its shape, such as a function that raises on the status code its body returns, or
a member that forwards to a free function. Each adapts a shape the generator chose, so the generator
writes the shape instead. The interface that encodes into bytes and decodes from them is a body the
lowering builds, so each language calls its own entry point directly. A runtime function handed the
entry point would make that call indirect, and in Go the compiler then assumes the caller's buffer
escapes, so an append into a stack array allocates. C's unversioned public name over each versioned
link name stays, so that two versions of a type link into one program.

**A signature is the declaration spelling's.** A signature is a function's name, its parameters and
their types, its return type, and the qualifiers that place it. The renderer asks for a lowered
function's signature once and writes it as the prototype and as the definition.
`BodySpelling::openFunction` names the parameters and writes the body's opening lines, such as
C++'s memory-resource lines.

**A helper is a member of its type where the language keeps a member from its users by the
member's own declaration.** That is C++'s `private` and Python's leading `_`. Elsewhere a helper is
private to the module, file or package that declares it. `LanguageTraitsTests` asserts the relation
over every row.

**Python's classes derive from `CompositeObject`.** The name is the one PyCyphal gives the base of
every class generated from DSDL. The size of a class's largest image is a public class attribute,
`SERIALIZATION_BUFFER_SIZE_BYTES`, where Python puts every fact of the type.
