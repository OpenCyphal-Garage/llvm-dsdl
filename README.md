# llvm-dsdl

![llvm-dsdl logo](./llvm-dsdl.png)

[![Release](https://img.shields.io/github/v/release/OpenCyphal-Garage/llvm-dsdl?display_name=tag)](https://github.com/OpenCyphal-Garage/llvm-dsdl/releases)

`llvm-dsdl` provides command-line tooling for Cyphal DSDL:

- `dsdlc`: DSDL compiler and multi-language code generator
- `dsdld`: DSDL language server (LSP over stdio JSON-RPC)

If you are contributing to the project itself (build system, internals, tests,
release process), use [CONTRIBUTING.md](./CONTRIBUTING.md).

## Install

### Option 1: Download a Release

Download the latest release artifacts from:

- <https://github.com/OpenCyphal-Garage/llvm-dsdl/releases>

Then place the binaries you need on your `PATH` (`dsdlc`, `dsdld`).

### Option 2: Build and install binaries from source

```bash
git clone https://github.com/OpenCyphal-Garage/llvm-dsdl.git
cd llvm-dsdl
git submodule update --init --recursive

cmake --workflow --preset install-bin-release-ci
```

Installed binaries are placed under:

- `build/matrix/ci/install/bin`

## `dsdlc` Quick Start

Show help:

```bash
dsdlc --help
```

Show version:

```bash
dsdlc --version
```

### Common usage pattern

```bash
dsdlc --target-language <lang> [options] <root_namespace_or_files...>
```

`<lang>` can be:

- `ast`
- `mlir`
- `c`
- `cpp`
- `rust`
- `go`
- `ts`
- `python`
- `obj`

### See what it generates

[`examples/showroom`](examples/showroom) is a namespace of plausible vendor-specific drone datatypes
— sealed types packed into a classic-CAN frame, delimited types that grow across minor versions,
unions, services, and a mission plan sized for a UDP datagram — generated into every language and
profile:

```bash
cmake --build <build-dir> --target showroom
```

The rendered pages, pairing each definition with its wire-layout facts and the generated declaration
in each language, are published at
[the Showroom section of the user manual](https://opencyphal-garage.github.io/llvm-dsdl/showroom/). They
are generated from the compiler's own output rather than committed; `showroom-docs` produces them
locally, into `docs/showroom/`.

### Practical examples

AST output:

```bash
dsdlc --target-language ast path/to/root_namespace
```

MLIR output:

```bash
dsdlc --target-language mlir path/to/root_namespace
```

Generate C output:

```bash
dsdlc --target-language c path/to/root_namespace --outdir out/c
```

Generate C++ output (`std`, `pmr`, `autosar`, or `both` where `both` means `std` + `pmr`):

```bash
dsdlc --target-language cpp path/to/root_namespace --cpp-profile both --outdir out/cpp
```

Generate AUTOSAR-oriented C++14 output:

```bash
dsdlc --target-language cpp path/to/root_namespace --cpp-profile autosar --outdir out/cpp-autosar
```

Generate Rust output:

```bash
dsdlc --target-language rust path/to/root_namespace \
  --rust-crate-name my_dsdl_types \
  --rust-profile std \
  --outdir out/rust
```

Generate Go output:

```bash
dsdlc --target-language go path/to/root_namespace \
  --go-module example.com/my/dsdl \
  --outdir out/go
```

Generate TypeScript output:

```bash
dsdlc --target-language ts path/to/root_namespace \
  --ts-module my_dsdl_types \
  --outdir out/ts
```

Generate Python output:

```bash
dsdlc --target-language python path/to/root_namespace \
  --py-package my_dsdl_types \
  --outdir out/python
```

Generate object/archive output:

```bash
dsdlc --target-language obj path/to/root_namespace \
  --target-endianness little \
  --target-triple x86_64-unknown-linux-gnu \
  --obj-archive-name my_dsdl_objects \
  --outdir out/obj
```

Generate profile-agnostic canonical C++ ABI object/archive output with C shim exports:

```bash
dsdlc --target-language obj path/to/root_namespace \
  --obj-abi-language cpp \
  --target-endianness little \
  --obj-archive-name my_dsdl_cppabi_objects \
  --outdir out/obj-cpp
```

### Dependency lookup

Use `--lookup-dir` (repeatable) when your definitions import other namespaces:

```bash
dsdlc --target-language c path/to/root_namespace \
  --lookup-dir path/to/lookup_a \
  --lookup-dir path/to/lookup_b \
  --outdir out/c
```

`dsdlc` also reads `DSDL_INCLUDE_PATH` and `CYPHAL_PATH`.

For the standard `uavcan.*` namespace, `dsdlc` ships an embedded catalog for
`mlir` and codegen targets (`c`, `cpp`, `rust`, `go`, `ts`, `python`, `obj`). Types
that reference core `uavcan` definitions resolve without needing external
`uavcan` source roots. Use `--no-embedded-uavcan` to disable this behavior.

A `+` target names that catalog directly, generating standard types without a
`public_regulated_data_types` checkout:

```bash
dsdlc --target-language c --outdir out/c +uavcan.node.Heartbeat.1.0
```

Run `dsdlc --help` for the full option set.

## `dsdld` Quick Start

`dsdld` is an LSP server over stdio. It currently does not expose a CLI flag
surface; the editor/client drives configuration through LSP settings.

Run it directly:

```bash
dsdld
```

### Neovim (`nvim-lspconfig`) example

```lua
local lspconfig = require("lspconfig")

lspconfig.dsdld.setup({
  cmd = { "/absolute/path/to/dsdld" },
  filetypes = { "dsdl" },
  settings = {
    roots = { "/absolute/path/to/root_namespace" },
    lookupDirs = { "/absolute/path/to/lookup_namespace" },
    lint = { enabled = true },
    ai = { enabled = false, mode = "off" },
    trace = "basic",
  },
})
```

## Troubleshooting

`dsdlc: unknown argument`:

- run `dsdlc --help` and verify spelling/order of options

Import resolution failures:

- add `--lookup-dir` entries
- verify namespace roots and file naming follow DSDL conventions

`dsdld` not responding in editor:

- verify editor points to the correct `dsdld` binary path
- verify workspace `roots`/`lookupDirs` settings

## Developer docs

For build internals, workflows, tests, and contribution standards, use:

- [CONTRIBUTING.md](./CONTRIBUTING.md)
- Runtime semantic-wrapper exception allowlist: [`runtime/semantic_wrapper_allowlist.json`](./runtime/semantic_wrapper_allowlist.json)
- Runtime semantic-wrapper generator: [`tools/runtime/generate_runtime_semantic_wrappers.py`](./tools/runtime/generate_runtime_semantic_wrappers.py)

## User manual (MkDocs)

This repository ships a MkDocs documentation site under [`docs/`](./docs), organised into four
sections: **Get Started** (first run), **Reference** (what the tools promise), **Development**
(changing this repository), and **For Agents** (the machine-readable index at
[`/llms.txt`](https://opencyphal-garage.github.io/llvm-dsdl/llms.txt)).

Several pages are generated rather than written — the showroom, the guarantee matrices, and the
agent index. Produce them before previewing, or `--strict` will fail on the nav entries with nothing
behind them:

```bash
cmake --build <build-dir> --target docs-generate
```

Local preview:

```bash
python -m pip install -r docs/requirements.txt
mkdocs serve
```

Build the static site:

```bash
mkdocs build --strict
```

GitHub Pages deployment is handled by:

- [`.github/workflows/docs.yml`](./.github/workflows/docs.yml)
