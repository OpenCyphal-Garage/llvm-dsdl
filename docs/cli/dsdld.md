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

- [LSP AI Operator Guide](../LSP_AI_OPERATOR_GUIDE.md)
- [LSP Index Schema](../LSP_INDEX_SCHEMA.md)
- [LSP Lint Rules](../LSP_LINT_RULES.md)
