# Reference

Everything the tools promise, arranged for lookup rather than for reading through.

| Group | What you look up here |
|---|---|
| [Commands](commands/dsdlc.md) | Switch names, accepted values, and what each tool does with them. |
| [Code Generation](codegen/backends.md) | What each target language emits, and the ordering contract the emitters hold to. |
| [Language Server](lsp/lint-rules.md) | `dsdld`'s rule catalog, index schema, ranking, logging, and AI surface. |
| [Guarantees](guarantees/parity-matrix.md) | What holds across languages, what happens on malformed input, what is reproducible, and what you can verify about a downloaded artifact. |
| [Showroom](../showroom/index.md) | Real definitions compiled into every language and profile, with their wire layout. |

## Commands

- [`dsdlc`](commands/dsdlc.md) — the compiler and codegen driver
- [`dsdl-opt`](commands/dsdl-opt.md) — dialect and pass-pipeline work
- [`dsdld`](commands/dsdld.md) — the language server

## Code generation

- [Backends](codegen/backends.md) — the target languages and what each produces
- [Object Backend](codegen/object.md) — compiled `.o`/`.a` output, ABI, and endianness
- [Emit Order](codegen/emit-order.md) — the canonical serialize/deserialize step order every backend renders

## Language server

- [Lint Rules](lsp/lint-rules.md) — the rule catalog and suppression schema
- [Index Schema](lsp/index-schema.md) — what the workspace index stores
- [Ranking Model](lsp/ranking-model.md) — how completions and symbol results are ordered
- [Logging](lsp/logging.md) — the structured log channel
- [AI Operation](lsp/ai-operation.md) — modes, policy gates, and configuration
- [AI Data Flow](lsp/ai-data-flow.md) — what enters the AI surface, what leaves it, what is retained

## Guarantees

- [Cross-Language Parity](guarantees/parity-matrix.md) — which backends are covered for which behaviours
- [Malformed Input](guarantees/malformed-input.md) — what each backend does with input it should reject
- [Determinism](guarantees/determinism.md) — what is byte-reproducible, and across which axes
- [Supply Chain](guarantees/supply-chain.md) — the LLVM version lock, SBOM, and what you can verify about a release

### The guarantee pages are generated

`parity-matrix`, `malformed-input`, and `determinism`, along with the [Consistency
Lint](../development/convergence-scorecard.md) under Development, are written by the report
generators under `tools/convergence/` and rebuilt whenever the site is published.

Each page carries a **gating mode** banner.
