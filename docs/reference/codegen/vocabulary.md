# Vocabulary

Generated code needs a library type for some of what it does: a span over the bytes a field
accessor reads. The backend states the **concept** -- the operations it performs on the type -- and
a **binding** names the library type that fills it. Bindings are data, read from files passed with
`--vocabulary`, so which library the output uses is the consumer's choice.

## Concepts

| Concept | Bound in | Fixed in |
| --- | --- | --- |
| `span` | C++ (`std`, `pmr`, `autosar`) | Rust, Go, TypeScript and Python, whose byte view is the language's own slice; C, which takes a pointer and a size |

### span

A view over a run of bytes: what a C++ field accessor takes its buffer in, and what a composite
field's getter answers the nested type's bytes in. The type spelling takes one placeholder,
`{element}`: `const std::uint8_t` for a buffer read, `std::uint8_t` for one written.

| Operation | Placeholders | Standard spelling | Where generated code uses it |
| --- | --- | --- | --- |
| `data` | `self` | `{self}.data()` | the pointer a runtime read or write takes |
| `size` | `self` | `{self}.size()` | the count of bytes the accessor may read |
| `subspan` | `self`, `offset` | `{self}.subspan({offset})` | a nested type's bytes from its field's offset, which the plan clamps to the size |

## Files

```yaml
vocabulary: 1
language: cpp
profiles: [autosar]
bindings:
  span:
    type: "cetl::pf20::span<{element}>"
    include: ['"cetl/pf20/span.hpp"']
```

| Key | |
| --- | --- |
| `vocabulary` | The format version: `1`. |
| `language` | The target language the file binds, which must be the run's. |
| `profiles` | The profiles the bindings apply to; every profile of the language when absent. |
| `bindings` | One entry per concept, named as the table above names it. |
| `type` | The type, with the concept's placeholders in braces. |
| `include` | The operands of the `#include` directives the type needs: `<header>` or `"header"`. |
| `operations` | The operations the library spells differently from the standard library, by name. A library with the standard shape names none. |

A binding with the standard shape and one that overrides every operation:

```yaml
bindings:
  span:
    type: "acme::ByteView<{element}>"
    include: ['"acme/byte_view.hpp"']
    operations:
      data: "{self}.ptr()"
      size: "{self}.length()"
      subspan: "{self}.tail({offset})"
```

A file fails the run, naming the line, when it has an unknown key, concept, language, profile or
placeholder; a spelling that omits a placeholder the concept requires; an include that is not an
`#include` operand; a concept the language fixes; or no binding at all. A file for another language
than the run's is refused, and so is any file for a language that binds no concept.

## Resolution

The built-in bindings apply first, then each `--vocabulary` file in command-line order. A later
binding of a concept replaces an earlier one for the profiles its file names, and an operation the
binding omits keeps its standard spelling. Every concept the language binds must have a binding for
the profile being generated; a profile without one fails the run before any file is written:

```
the cpp autosar profile has no binding for span: pass --vocabulary <file> with one
```

The built-in bindings name the standard library alone: `std::span` from `<span>` for the C++ `std`
and `pmr` profiles, which are C++20. The `autosar` profile is C++14 and has no built-in binding;
[`examples/vocabulary/cpp-autosar-cetl.yaml`](https://github.com/OpenCyphal-Garage/llvm-dsdl/blob/main/examples/vocabulary/cpp-autosar-cetl.yaml)
binds [CETL](https://github.com/OpenCyphal/CETL)'s span, the C++14 polyfill of `std::span`.

## Build integration

A vocabulary file is an input of the run: `--list-inputs` lists it beside the definitions, and every
depfile `-MD` writes names it, so a build that takes its dependencies from either regenerates when a
binding changes. `dsdlc_generate()` takes the files under `VOCABULARY`, and `rules_dsdl`'s namespace
rule and tag under `vocabulary`. The `cpp-cmake` recipe in the
[showroom](../../showroom/recipes/index.md) generates the `autosar` profile against CETL this way.
