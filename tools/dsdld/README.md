# dsdld

`dsdld` is the DSDL Language Server Protocol (LSP) server for `llvm-dsdl`.
It runs over stdio JSON-RPC and reuses the same frontend/AST/semantic pipeline as `dsdlc`.

## Build

From repository root:

```bash
cmake --build build/matrix/dev-homebrew --config RelWithDebInfo --target dsdld
```

Binary path example:

- `build/matrix/dev-homebrew/tools/dsdld/RelWithDebInfo/dsdld`

## Runtime Model

`dsdld` does not use command-line flags currently.
Clients configure behavior via LSP `workspace/didChangeConfiguration` settings.

Supported high-level features:

1. Diagnostics (`textDocument/publishDiagnostics`)
2. Semantic tokens (`textDocument/semanticTokens/full`)
3. Hover, definition, references, completion
4. Rename (`prepareRename` + `rename`) and code actions
5. Workspace symbol index with ranking and warm cache
6. Lint engine with suppressions and plugin rule packs
7. Optional policy-gated AI-assisted actions and tool use

## Neovim Setup

Example with `nvim-lspconfig`:

```lua
local lspconfig = require("lspconfig")

lspconfig.dsdld = {
  default_config = {
    cmd = { "/absolute/path/to/dsdld" },
    filetypes = { "dsdl" },
    root_dir = function(fname)
      return lspconfig.util.find_git_ancestor(fname) or vim.fn.getcwd()
    end,
  },
}

lspconfig.dsdld.setup({
  settings = {
    roots = { "/absolute/path/to/your/root_namespace" },
    lookupDirs = { "/absolute/path/to/lookup_namespace" },
    lint = { enabled = true },
    ai = { enabled = false, mode = "off" },
    trace = "basic",
  },
})
```

Optional filetype detection if needed:

```lua
vim.filetype.add({ extension = { dsdl = "dsdl" } })
```

## Development Verification

From repository root:

```bash
cmake --build build/matrix/dev-homebrew --config RelWithDebInfo --target llvmdsdl-unit-tests dsdld
build/matrix/dev-homebrew/test/unit/RelWithDebInfo/llvmdsdl-unit-tests
```

Performance/stress harness targets:

```bash
cmake --build build/matrix/dev-homebrew --config RelWithDebInfo --target benchmark-lsp-replay
cmake --build build/matrix/dev-homebrew --config RelWithDebInfo --target benchmark-lsp-index-cold-warm
```
