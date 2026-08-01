# Build Recipes

The [showroom](README.md) shows you what dsdlc produces from the `lanyard` namespace. This is the
other half: **what do I paste into my build to get it?**

Each *recipe* is one (language, build system) pair, complete and self-contained, and every one
builds the same twenty-four definitions you can browse on the [overview](README.md). Browsing a type
and then seeing it compiled into a library is the point of the two halves living together.

A recipe generates with `dsdlc`, compiles the output, and runs a round-trip program that serialises
a value, deserialises it, and checks the result. CI runs all of them, so a recipe on this page is a
recipe that worked the last time anyone looked -- which is the one thing the browsing half
deliberately does not promise, since it generates and stops.

## The one fact that makes this easy

**Generated output is self-contained.** The language runtime is embedded in `dsdlc` and written into
your output directory alongside the generated types. There is no runtime package to add to your
manifest, no version to keep in step, and no second thing to install. Point `dsdlc` at a namespace,
compile what comes out.

For Rust, Go, TypeScript, and Python, `dsdlc` goes further and writes a native manifest too --
`Cargo.toml`, `go.mod`, `package.json`, `pyproject.toml`. C and C++ get `dsdl_runtime.h` /
`dsdl_runtime.hpp` and no manifest, because C and C++ have no such thing to write.

## Two idioms

Every recipe is one of two shapes. Which one you get is decided by your build system, not by taste.

**Idiom A -- the generated tree *is* the package.** Your build depends on it by path: a cargo path
dependency, a `go.mod` `replace` directive, `pip install -e`. The manifest `dsdlc` wrote is the one
that gets used. Shortest path when it is available, which for Rust and Go is a single line.

**Idiom B -- your project owns the manifest; the generated tree is sources.** You point your
existing build at the output directory and compile it as part of your own target. Forced on you
when:

- **C and C++** -- no manifest is emitted at all, because neither language has one to emit.
- **TypeScript** -- a `package.json` *is* emitted, but it declares no `main`, `exports`, or `types`
and the tree contains only `.ts` files. `npm install file:generated` succeeds and then fails at run
time, because Node falls back to looking for `index.js` and finds `index.ts`. Generated TypeScript
is source, and joins your program as source.
- **Every Python build backend except setuptools** -- the emitted `pyproject.toml` declares
  `setuptools.build_meta`, and a project cannot have two build backends. Pointing another backend at
  the generated package is one line of configuration; the generated tree is an ordinary Python
  package, and dsdlc's own manifest just sits there unused.

Each recipe page states which idiom it is and why that build system left no choice.

## Keeping generated code fresh

The second decision every recipe makes, independent of the idiom: how does the build know to
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

## Splitting generation up

You are not obliged to treat a namespace as one indivisible run. dsdlc will tell you what it would
emit for any subset of its work, which means a build can give each subset its own rule, its own
outputs, and its own lifetime.

| Selector | Selects |
|---|---|
| `--generate-support only` | Only code *not* derived from a definition: runtime headers and modules, package manifests, scaffolding. Needs no positional target. |
| `--generate-support never` | Only code derived from definitions. |
| `--omit-dependencies` | Only the definitions named, not the ones they refer to. |
| `+uavcan.node`, `+uavcan.node.Heartbeat.1.0` | Standard types from the catalog compiled into `dsdlc`, with no checkout of `public_regulated_data_types`. |
| `<root>:<relative/Type.1.0.dsdl>` | One definition, with output paths anchored at `<root>`. |

Combining them gives a build as much granularity as it wants -- per namespace, per tranche, or per
definition. The reason to bother is that these things change at different rates: the runtime header
and the standard types move only when the compiler does, while your own definitions change all day.
Split along that line and editing a definition rebuilds one archive instead of all of them, and the
standard types become a library that several components share.

**Past one namespace this stops being an optimisation and becomes a requirement.** Two runs that
both default to `--generate-support as-needed` into the same output directory both emit the runtime
header, and two rules producing one file is an error Ninja refuses to build at all. The same goes
for a standard type two namespaces both refer to. So a project with several namespaces gives support
one owner, the shared standard types another, and passes `--omit-dependencies` to the rest.

The [GNU Make recipe](recipes/c-make.md) is built this way, in three tranches. `dsdlc_generate()`
takes `SUPPORT`, `BUILTIN`, and `OMIT_DEPENDENCIES` for the same purpose, and refuses at configure
time -- naming both claimants -- if two calls would produce the same file.

The requirement is about *sharing an output directory*, not about having several namespaces. A build
that gives each namespace its own output tree never collides and never needs the split -- which is
what the [Bazel](recipes/c-bazel.md) fetch-time path does, one external repository per namespace.
The split is what you need when several runs write into one tree.

### Prebuilt archives

`-l obj` is the other shape: it runs the C backend and then compiles it, handing back objects and a
`.a`. Use it when the generated code should be built once -- for a target triple that is not the
host, or by a party who ships the archive rather than the schema. The archive then only links into a
build using that same toolchain.

The headers are published beside the archive, in the same layout the `c` backend uses, so one
invocation gives a complete interface -- add the output directory to your include path and link the
archive. The [CMake](recipes/c-cmake.md) and [Bazel](recipes/c-bazel.md) recipes both build one.

### Deleting a definition

Generation adds files; it does not remove them. Delete a definition and its generated header stays
in the output directory, still on the include path, so code naming a type nobody defines any more
keeps compiling.

`--prune-manifest <file>` fixes that: the run records what it produced and, next time, deletes what
it no longer produces. One manifest per invocation, never one per directory -- a tranche owns only
the files it emits, and one that swept the output directory would delete its siblings' work.
`dsdlc_generate()` passes one automatically.

**If your build has to reshape what dsdlc emits, that is a bug worth reporting rather than a recipe
worth copying.** Nothing on these pages moves, renames, or post-processes a generated file, and no
recipe should need to.

## Why the showroom does it differently

<!-- showroom-recipes: skip -->

If you read `examples/showroom/CMakeLists.txt` and wondered why it uses a stamp file instead: there,
`dsdlc` is a target of the very build that wants to query it, so it does not exist at configure time
and `--list-outputs` cannot be run. That constraint is peculiar to building the compiler inside its
own tree. Every recipe here consumes an *installed* `dsdlc` and is free of it, which is why these
recipes are better than that one and should be preferred as models.

## The matrix

<!-- showroom-recipes: matrix-table -->

## Running a recipe yourself

Every recipe directory is independently runnable. Copy the recipe you want plus `dsdl/` and `src/`
alongside it, and run the commands on its page. The CI runner stages exactly that copy for every
recipe on every run.

To run them from a checkout:

```bash
python3 examples/showroom/run_recipe.py --dsdlc /path/to/dsdlc
```

Add recipe names to run a subset, `--list` to see what is registered, and `--verbose` to watch the
commands.

## What you need installed

Each recipe page lists the tools that recipe needs, and `run_recipe.py` skips a recipe whose tools
are absent rather than failing it. No recipe installs anything into your system: the Python recipes
each build a virtual environment and fetch their own build backend, which is both correct isolation
and the only thing that works on a distribution that marks its interpreter externally managed (PEP
668).

## Toolchain availability in CI

<!-- showroom-recipes: skip -->

Measured against the toolshed image (`ts26.4.3`), which is what CI runs in:

| Tool | In the image | How a recipe gets it |
|---|---|---|
| `make`, `cmake`, `ninja` | yes | -- |
| `go` 1.26, `cargo` 1.93 | yes | -- |
| `node` 22, `npm`, `tsc` | yes | -- |
| `python3` 3.14, `pip`, `venv` | yes | -- |
| `pnpm` | no | `corepack enable pnpm` -- `corepack` is in the image |
| `setuptools`, `uv`, `poetry`, `flit`, `hatchling` | no | per-recipe virtual environment |
| `bazel` 9.2.0 | yes | -- |

Every recipe's toolchain now comes from the image. Bazel arrived in `ts26.4.3`, which installs
bazelisk and pre-warms Bazel 9.2.0; the Bazel recipe pins that same version in its `.bazelversion`,
so it neither downloads a compiler nor drifts off the one the image carries. `pnpm` is the only tool
still provisioned in the job, by `corepack`, which is why `ts-pnpm` is the one recipe absent from
the `--require` list.

## The recipe invariant

**Recipes contain build wiring and nothing else.** The round-trip programs live in `src/`, one per
language, shared by every recipe in that row; the schema lives in `dsdl/`, shared by all of them.

So the difference between the poetry recipe and the hatchling recipe is *only* the build files --
which is exactly the comparison you came here to make. `run_recipe.py --check-invariant` enforces
it, and CI runs that check.

## What the recipes build

Every recipe builds the whole `lanyard` namespace: thirty-six translation units in C once the
standard types it reaches are counted, and it costs about four seconds across the whole matrix.
Measured rather than assumed: generation is ~70 ms per backend, and the compile cost is what
dominates, only where a recipe builds serially.

Between them the definitions force a real integration:

| What | Why it matters |
|---|---|
| Twelve standard `uavcan` types, reached transitively | They resolve from the catalog compiled into dsdlc -- no checkout of `public_regulated_data_types` anywhere in the matrix |
| Three different `Vector3` types in sibling namespaces | Same short name, different namespace: the case that breaks a naive import |
| Two `@deprecated` definitions | A namespace containing them still has to compile clean |
| Services, unions, multiple versions of one type | Each generates more than one type from one file |

An integration that compiles `SystemHealth` but not the `SubsystemReport` it embeds is broken, and
this namespace is shaped to fail loudly when that happens.
