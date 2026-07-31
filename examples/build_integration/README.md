# Build system integration

You already have a build. This example answers the only question that matters next: **what do I paste
into it?**

Each *cell* below is one (language, build system) pair, complete and self-contained. Every cell
generates code with `dsdlc`, compiles it, and runs a round-trip program that serialises a value,
deserialises it, and checks the result. CI runs all of them, so a recipe on this page is a recipe
that worked the last time anyone looked.

This is not [the showroom](../showroom/README.md). That example shows you what the compiler
*produces*, and deliberately never compiles anything. This one never shows you generated code and
does nothing but compile it.

## The one fact that makes this easy

**Generated output is self-contained.** The language runtime is embedded in `dsdlc` and written into
your output directory alongside the generated types. There is no runtime package to add to your
manifest, no version to keep in step, and no second thing to install. Point `dsdlc` at a namespace,
compile what comes out.

For Rust, Go, TypeScript, and Python, `dsdlc` goes further and writes a native manifest too --
`Cargo.toml`, `go.mod`, `package.json`, `pyproject.toml`. C and C++ get `dsdl_runtime.h` /
`dsdl_runtime.hpp` and no manifest, because C and C++ have no such thing to write.

## Two idioms

Every cell is one of two shapes. Which one you get is decided by your build system, not by taste.

**Idiom A -- the generated tree *is* the package.** Your build depends on it by path: a cargo path
dependency, a `go.mod` `replace` directive, `pip install -e`. The manifest `dsdlc` wrote is the one
that gets used. Shortest path when it is available, which for Rust and Go is a single line.

**Idiom B -- your project owns the manifest; the generated tree is sources.** You point your existing
build at the output directory and compile it as part of your own target. Forced on you when:

- **C and C++** -- no manifest is emitted at all, because neither language has one to emit.
- **TypeScript** -- a `package.json` *is* emitted, but it declares no `main`, `exports`, or `types`
  and the tree contains only `.ts` files. `npm install file:generated` succeeds and then fails at run
  time, because Node falls back to looking for `index.js` and finds `index.ts`. Generated TypeScript
  is source, and joins your program as source.
- **Every Python build backend except setuptools** -- the emitted `pyproject.toml` declares
  `setuptools.build_meta`, and a project cannot have two build backends.

Each cell page states which idiom it is and why that build system left no choice.

## Keeping generated code fresh

The second decision every cell makes, independent of the idiom: how does the build know to
regenerate? Getting this wrong is how a build serves stale generated code after a schema edit.

| Strategy | Mechanism | Precision | Suits |
|---|---|---|---|
| Configure-time query | `--list-outputs`, `--list-inputs` | Exact | CMake, Make |
| Build-time depfile | `-MD` | Exact, incremental | Make, Ninja, Bazel |
| Stamp file | One sentinel output | Coarse | Fallback |

`--list-outputs` and `--list-inputs` print a semicolon-separated list and imply `--dry-run`, so a
build can ask what *would* be produced before producing it. `-MD` writes make-style `.d` files next
to the generated output. Between them, an exact incremental integration is available in any build
system that can run a command at configure time or read a depfile -- which is all of them.

## Why the showroom does it differently

<!-- build-integration: skip -->

If you read `examples/showroom/CMakeLists.txt` and wondered why it uses a stamp file instead: there,
`dsdlc` is a target of the very build that wants to query it, so it does not exist at configure time
and `--list-outputs` cannot be run. That constraint is peculiar to building the compiler inside its
own tree. Every cell here consumes an *installed* `dsdlc` and is free of it, which is why these
recipes are better than that one and should be preferred as models.

## The matrix

<!-- build-integration: matrix-table -->

## Running a cell yourself

Every cell directory is independently runnable. Copy the cell you want plus `dsdl/` and `src/`
alongside it, and run the commands on its page -- there is nothing else to fetch. That is not a claim
made in prose: the CI runner stages exactly that copy for every cell on every run, so a cell that
reached back into this repository would fail immediately.

To run them from a checkout:

```bash
python3 examples/build_integration/run_cell.py --dsdlc /path/to/dsdlc
```

Add cell names to run a subset, `--list` to see what is registered, and `--verbose` to watch the
commands.

## What you need installed

Each cell page lists the tools that cell needs, and `run_cell.py` skips a cell whose tools are absent
rather than failing it. No cell installs anything into your system: the Python cells each build a
virtual environment and fetch their own build backend, which is both correct isolation and the only
thing that works on a distribution that marks its interpreter externally managed (PEP 668).

## Toolchain availability in CI

<!-- build-integration: skip -->

Measured against the toolshed image (`ts26.4.2`), which is what CI runs in:

| Tool | In the image | How a cell gets it |
|---|---|---|
| `make`, `cmake`, `ninja` | yes | -- |
| `go` 1.26, `cargo` 1.93 | yes | -- |
| `node` 22, `npm`, `tsc` | yes | -- |
| `python3` 3.14, `pip`, `venv` | yes | -- |
| `pnpm` | no | `corepack enable pnpm` -- `corepack` is in the image |
| `setuptools`, `uv`, `poetry`, `flit`, `hatchling` | no | per-cell virtual environment |
| `bazel` | **no** | not yet resolved; the Bazel cell is gated on it |

Only Bazel is a genuine gap in the image. Every other cell runs on `ts26.4.2` unchanged.

## The cell invariant

**Cells contain build wiring and nothing else.** The round-trip programs live in `src/`, one per
language, shared by every cell in that row; the schema lives in `dsdl/`, shared by all of them.

So the difference between the poetry cell and the hatchling cell is *only* the build files -- which
is exactly the comparison you came here to make. `run_cell.py --check-invariant` enforces it, and CI
runs that check.

## What is in `dsdl/`

Four definitions in a `kitbag` namespace, deliberately small -- you are here to read build files, not
schemas. Between them they force a real integration:

| Type | What it proves |
|---|---|
| `Mode.1.0` | Constants and padding render in every language |
| `Reading.1.0` | A nested composite is compiled and linked, not just the top-level type |
| `6300.SensorFrame.1.0` | A standard `uavcan` type resolves from the embedded catalog; arrays of composites work; the delimited-header path is exercised |
| `300.Configure.1.0` | Both halves of a service generate, with distinct names |

An integration that compiles `SensorFrame` but not the `Reading` it embeds is broken, and this
namespace is shaped to fail loudly when that happens.
