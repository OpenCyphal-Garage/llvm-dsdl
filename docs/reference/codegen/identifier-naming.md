# Identifier naming

A DSDL name is not an identifier in any of the six target languages. `break` is a keyword in all six,
`fooBar` and `foo_bar` are one name in three of them, and `_Leading` is in the region C and C++
reserve for their implementations. This describes what happens to a name on its way into generated
code, and what a corpus is rejected for.

The projection is deterministic and depends only on the definition: the same DSDL name yields the
same identifier in every invocation that emits that language.

## Roles

One DSDL name can become several different things, and each one is spelled by its own rule. The rule
is chosen by the name's **role**:

| Role | What it names |
|---|---|
| `TypeName` | The generated type |
| `FieldName` | A member of it |
| `ConstantName` | A generated constant |
| `FunctionName` | A generated function |
| `LocalName` | A variable inside a generated body |
| `NamespaceName` | A namespace, package or module component |
| `FileStem` | The output file, without its extension |
| `MacroName` | A preprocessor macro |

A field called `value` and a constant called `value` in the same definition do not receive the same
identifier, because `ConstantName` upper-cases and `FieldName` does not.

## The pipeline

Every name passes through the same six stages, in order. A stage that does not apply to the role or
the language is skipped.

1. **Case projection.** Fold the name to the language's convention for that role: `snake_case`,
   `PascalCase`, or preserve it as written.
2. **Escape.** Replace anything outside `[A-Za-z0-9_]`, and prefix a leading digit.
3. **Keyword strop.** If the result is a keyword, append `_`. `break` becomes `break_`.
4. **Upper-case.** Constants and macros are upper-cased.
5. **Claimed names.** Generated code declares members of its own — a serialise method, a metadata
   constant, a union tag. A DSDL name landing on one of those is escaped the same way a keyword is.
6. **Reserved namespaces.** A name that lands in a region the language reserves for its
   implementation is rejected, or encoded on request.

Stages 1 and 4 are the ones that differ per language; the tables below give them per role.

## Uniqueness within a scope

Case projection is many-to-one: `fooBar`, `foo_bar` and `FooBar` are three DSDL names and one
`snake_case` identifier. Keyword stropping is many-to-one too, since `break` and `break_` both give
`break_`.

Where such names meet in one region of generated code — a struct body, a constant block — the region
is a **scope**, and the scope keeps the map injective by appending an ordinal to the later name. The
first declaration in DSDL order keeps the plain spelling:

```
uint8 break
uint8 fooBar
uint8 foo_bar
uint8 _leading
```

becomes, in Go:

```go
Break     uint8
FooBar    uint8
FooBar_2  uint8
Leading   uint8
```

The ordinal is assigned in DSDL declaration order, so it is stable across runs and moves only when
the definition does. Each rename is reported:

```
note: field 'foo_bar' is emitted as 'FooBar_2' for target language 'go';
      another name in the same scope already projects to 'FooBar'
```

A scope covers one region, not a whole file. C++ declares a definition's fields and constants into
one struct body, so those share a scope; the other five put constants where a field cannot reach
them, and give each its own.

## Rejected corpora

A scope repairs a collision by renaming, which works while the name is internal to a region. Two
kinds of name cannot be repaired that way, because the choice of which definition to rename would
depend on the order they were discovered in:

- **Output file names.** `ns.FooBar.1.0` and `ns.Foo_bar.1.0` both want `foo_bar_1_0` in the four
  languages that snake_case their file stems. One file would overwrite the other.
- **Type names.** Two definitions projecting onto one type name in a language that shares a scope
  across a namespace.

Both are rejected at discovery, naming the pair and the languages affected:

```
error: type name collision in generated output: ns.Foo_bar and ns.FooBar map to the
       same output file name for target language 'go'
```

Only the languages the invocation emits are checked, so a build never fails over a hazard in output
it was not going to produce. An invocation that emits nothing — analysis, or the language server —
checks all six, because there is no build to fail.

A service is checked the same way against its own sections. A service `Foo` emits `Foo_Request`, and
a sibling definition may be *called* `Foo_Request`; the pair is rejected where the two would meet.

## Reserved namespaces

C and C++ reserve part of the identifier space for their implementations. A DSDL name landing in it
is rejected:

| Name | C | C++ | Rust, Go, TypeScript, Python |
|---|---|---|---|
| `_leading` | accepted | accepted | accepted |
| `_Leading` — leading `_` before a capital | rejected | rejected | accepted |
| `__dunder` — leading `__` | rejected | rejected | accepted |
| `foo__bar` — interior `__` | accepted | rejected | accepted |

C++ reserves any `__` run; C reserves one only at the start of a name.

Renaming the definition is the expected answer, and the diagnostic says so. Where a corpus cannot be
renamed, the offending characters can be encoded instead — the diagnostic names the flag and shows
the identifier it would produce.

## Per-language rules

### Case and stropping by role

`preserve` keeps the DSDL spelling. `escape` replaces characters outside `[A-Za-z0-9_]`. `strop`
appends `_` to a keyword. `upper` upper-cases the finished identifier.

| Language | Role | Case | Escape | Strop | Upper |
|---|---|---|---|---|---|
| C | TypeName, FieldName, FunctionName, LocalName, NamespaceName | preserve | ✅ | ✅ | |
| C | ConstantName, MacroName | preserve | ✅ | | ✅ |
| C | FileStem | preserve | | | |
| C++ | TypeName, FieldName, FunctionName, LocalName, NamespaceName | preserve | ✅ | ✅ | |
| C++ | ConstantName, MacroName | preserve | ✅ | | ✅ |
| C++ | FileStem | preserve | | | |
| Rust | TypeName, FieldName, FunctionName, LocalName, NamespaceName | preserve | ✅ | ✅ | |
| Rust | ConstantName, MacroName | snake | ✅ | ✅ | ✅ |
| Rust | FileStem | snake | ✅ | ✅ | |
| Go | TypeName, FieldName, FunctionName, LocalName | pascal | ✅ | ✅ | |
| Go | ConstantName, MacroName | snake | ✅ | ✅ | ✅ |
| Go | NamespaceName, FileStem | snake | ✅ | ✅ | |
| TypeScript | TypeName | pascal | ✅ | ✅ | |
| TypeScript | FieldName, FunctionName, LocalName, NamespaceName, FileStem | snake | ✅ | ✅ | |
| TypeScript | ConstantName, MacroName | snake | ✅ | ✅ | ✅ |
| Python | TypeName | pascal | ✅ | ✅ | |
| Python | FieldName, FunctionName, LocalName, NamespaceName, FileStem | snake | ✅ | ✅ | |
| Python | ConstantName, MacroName | snake | ✅ | ✅ | ✅ |

A C or C++ file stem takes no keyword escape because a file name is not an identifier, and a C or C++
macro takes none because a macro token is not in the language's namespace and always carries its type
name as a prefix.

The machine-checked form of this table is
[`test/unit/golden/naming-roles.txt`](https://github.com/OpenCyphal-Garage/llvm-dsdl/blob/main/test/unit/golden/naming-roles.txt),
regenerated from the call sites that produce each name. A diff there is an ABI change.

### What shares a scope

This decides whether two definitions can collide, and how much a scope can repair.

| Language | A definition's type lives in | Two definitions in one namespace |
|---|---|---|
| C | one global scope, namespace flattened into the identifier | share it |
| C++ | a namespace per DSDL namespace | share it |
| Go | a package per DSDL namespace | share it |
| Rust | a module per definition and version | separate |
| TypeScript | a module per definition and version | separate |
| Python | a module per definition and version | separate |

Where a namespace is one scope, two versions of one type reach one identifier unless the type name
carries its version. Rust, TypeScript and Python are unaffected, because each version has a module of
its own.

### Case folding

Go, TypeScript and Python fold case, so `fooBar` and `foo_bar` are one identifier in those three and
three identifiers in C, C++ and Rust. The scope repairs it either way: in C, C++ and Rust the keyword
and claimed-name escapes are many-to-one where case folding is not.

## Where it is decided in code

[`include/llvmdsdl/Support/NamingPolicy.h`](https://github.com/OpenCyphal-Garage/llvm-dsdl/blob/main/include/llvmdsdl/Support/NamingPolicy.h)
holds the role table and the pipeline; `NamingScope` in the same header holds the per-region
uniqueness.
[`include/llvmdsdl/Support/DefinitionNaming.h`](https://github.com/OpenCyphal-Garage/llvm-dsdl/blob/main/include/llvmdsdl/Support/DefinitionNaming.h)
composes the names built from a definition's identity — its type name, file stem, include guard and
linkage symbol. Discovery rejects the corpora above.

The design record, including why each policy is what it is, is
[Identifier naming](../../development/identifier-stropping.md) under Development.
