# llvm-dsdl

![llvm-dsdl logo](./llvm-dsdl.png)

[![CI](https://img.shields.io/github/actions/workflow/status/OpenCyphal-Garage/llvm-dsdl/ci.yml?branch=main&label=CI)](https://github.com/OpenCyphal-Garage/llvm-dsdl/actions/workflows/ci.yml)
[![Docs](https://img.shields.io/badge/docs-user%20manual-blue)](https://opencyphal-garage.github.io/llvm-dsdl/)
[![Release](https://img.shields.io/github/v/release/OpenCyphal-Garage/llvm-dsdl?display_name=tag&include_prereleases)](https://github.com/OpenCyphal-Garage/llvm-dsdl/releases)

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

Generate objects, assembled inside `dsdlc` for the host or for a named target, with the C headers
beside them:

```bash
dsdlc --target-language obj path/to/root_namespace \
  --target-triple riscv32-unknown-elf \
  --outdir out/obj
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
`uavcan` source roots. Use `--no-embedded-uavcan` to disable this behaviour.

A `+` target names that catalog directly, generating standard types without a
`public_regulated_data_types` checkout:

```bash
dsdlc --target-language c --outdir out/c +uavcan.node.Heartbeat.1.0
```

Run `dsdlc --help` for the full option set.

## `dsdld` Quick Start

`dsdld` is an LSP server over stdio. The editor or client drives configuration
through LSP settings.

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
