# `dsdld`

`dsdld` is the DSDL language server (LSP over stdio JSON-RPC).

It is editor/client configured (settings-driven), not primarily CLI-flag driven.

## Run

```bash
dsdld
```

## Typical client settings

- `roots`
- `lookupDirs`
- lint enablement
- AI feature mode and trace settings

## Editor integration

- Neovim: configure via `nvim-lspconfig`

## Related operator docs

- [AI Operation](../lsp/ai-operation.md)
- [Index Schema](../lsp/index-schema.md)
- [Lint Rules](../lsp/lint-rules.md)
