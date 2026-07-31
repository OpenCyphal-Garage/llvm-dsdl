# LSP Ranking Model

How `dsdld` orders the results it returns — which completion comes first, which symbol match wins —
and which signals feed that ordering.

## Scope

This model is used by `dsdld` for:

1. `textDocument/completion` reranking
2. `workspace/symbol` reranking

It is implemented in:

- `include/llvmdsdl/LSP/Ranking.h`
- `lib/LSP/Ranking.cpp`

## Feature Set

Each candidate gets an explainable score breakdown:

1. `lexical_base`: retrieval-stage lexical score
2. `match_quality`: exact/prefix/contains boosts
3. `fuzzy_boost`: subsequence-match boost
4. `frequency_boost`: exposure/selection usage boost
5. `recency_boost`: exponential-decay recency boost
6. `kind_boost`: candidate-kind bias
7. `length_penalty`: small name-length penalty

Final score:

`total_score = lexical_base + match_quality + fuzzy_boost + frequency_boost + recency_boost + kind_boost + length_penalty`

## Adaptive Signals

Signals are tracked per ranking key:

- exposure count
- selection count
- last update tick

Persistence:

- JSON file in index cache directory: `ranking-signals.json`
- bounded size (default max entries: 4096)
- least-recent entries are pruned when full

## Explainability Endpoint

`dsdld/debug/scoreExplain`

Params:

- `kind`: `completion` or `workspaceSymbol`
- `query`: optional query text
- `limit`: optional row count
- for `completion`: `uri`, `line`, `character`

Returns a row list containing candidate identity + full score breakdown.
