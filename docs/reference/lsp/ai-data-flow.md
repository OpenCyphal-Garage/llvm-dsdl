# LSP AI surface: data flow

What data enters the `dsdld` AI surface, what happens to it, what leaves, and what is retained. This is
the *data-flow* companion to `ai-operation.md` (which covers modes, configuration, and
playbooks); read this one to answer "does my DSDL source leave my machine?".

Every claim below names the governing constant or call site.

## The short answer

**Nothing leaves the process.** The only implementation of the `AiProvider` interface is
`OfflineAiProvider` (`lib/LSP/AI.cpp`), which runs in-process and computes suggestions locally; the
server hard-wires it at construction (`Server.cpp`, `aiProvider_ = std::make_unique<OfflineAiProvider>()`).
There is no network code anywhere in the AI surface — no HTTP client, no socket, no outbound
request of any kind. Document content reaches a bounded, in-memory packer and a local provider, and
suggestions return to the editor over the same JSON-RPC connection the editor opened.

**And by default nothing runs at all:** `ServerConfig::aiMode` defaults to `AiMode::Off`, and
`AiPolicyGate::isEnabled` is false for `Off`, so the surface is inert until an operator turns it on.

## Entry points

Only four LSP methods reach the AI surface, each behind a policy gate:

| Method | Gate | Reaches |
| --- | --- | --- |
| `textDocument/codeAction` | `AiPolicyGate::canSuggest` | `appendAiCodeActions` → packer → provider |
| `codeAction/resolve` | `canSuggest` | suggestion resolution |
| `dsdld/ai/resolveEdit` | `canApplyConfirmedEdits` — **only** `ApplyWithConfirmation` | edit materialization |
| `dsdld/ai/toolUse` | `isEnabled` **and** `isToolAllowed` | read-only introspection tools |

Nothing else in the server consults `aiProvider_`.

## What enters (and how much)

On a code-action request the server reads the document from its **in-memory overlay**
(`documents_.lookup(uri)` — the editor's live buffer, not the file on disk) and hands it to
`AiContextPacker::buildCodeActionContext`, which packs a deliberately bounded `AiCodeActionContext`:

| Field | Content | Bound (`lib/LSP/AI.cpp`) |
| --- | --- | --- |
| `selectionSnippet` | **Actual document text** around the selection — the only raw source that enters | `MaxSnippetBytes = 640` (truncated) |
| `diagnostics` | Diagnostic *messages* for the range | `MaxDiagnosticMessages = 8` |
| `symbolHints` | Symbol names from `analysis_.documentSymbols(uri)` | `MaxSymbolHints = 24` |
| `uri`, selection range | File URI and start/end line·character | — |
| `documentFacts` | Structural only: `lastLine`, `lastLineLength`, `endsWithNewline`, `hasSealedDirective` | — |

So the maximum document content that can reach the provider on any single request is **640 bytes of
source**, plus symbol names and diagnostic text. The full buffer is read to *locate* the selection, but
is not forwarded.

## What can come back, and what it can do

The provider returns `AiCodeActionSuggestion` values (`id`, `title`, `kind`, `explanation`,
`diagnosticMessage`, `hasEdit`, `requiresConfirmation`). These become code actions in the editor.

A suggestion **cannot silently modify your files**. `requiresConfirmation` defaults to `true`, and edit
materialization is gated by `AiPolicyGate::canApplyConfirmedEdits`, which is true for exactly one mode —
`ApplyWithConfirmation`. In `Suggest` and `Assist`, edits can be described but never materialized.

### Tool surface

`dsdld/ai/toolUse` is restricted to a four-entry allow-list (`AiPolicyGate::isToolAllowed`):
`analysis.stats`, `workspace.symbols`, `document.symbols`, `document.diagnostics`. All four are
**read-only introspection** of state the server already computed. There is no file-write, no shell, and
no network tool.

## What is retained

`AiAuditLogger` keeps records of policy/tool events **in memory only** — there is no filesystem or
network persistence in the AI surface, so the log is lost on restart and never lands on disk. It is
doubly bounded (`include/llvmdsdl/LSP/AI.h`):

- `MaxRecords = 256` — a ring; older records are dropped.
- `MaxDetailBytes = 4096` — per-record detail cap, applied **after** redaction.

Together these bound audit memory at roughly 1 MiB regardless of request size.

### Redaction

`AiAuditLogger::redactSensitive` masks common secret shapes before a record is stored
(case-insensitive):

- JSON members: `"password" | "token" | "secret" | "api_key" | "api-key" : "…"`
- Bare assignments: `password|token|secret|api[_-]?key` followed by `:` or `=`
- `Bearer <token>`

This is pattern-based masking of *known* shapes, not a proof of absence — a secret in an unrecognised
form embedded in DSDL source could still reach an audit record. Treat the audit log as sensitive.

Redaction runs **before** the 4096-byte cap, so truncation cannot leave half a secret unmasked.

## Summary for an operator

- **Default:** off. Nothing is read, computed, or retained.
- **Enabled:** at most 640 bytes of source per request reaches a **local, in-process** provider. Nothing
  is transmitted anywhere.
- **Edits:** never applied without `ApplyWithConfirmation` plus explicit confirmation.
- **Tools:** four read-only introspection calls, closed allow-list.
- **Retention:** in-memory, redacted, ~1 MiB bounded, gone on restart.

See `ai-operation.md` for turning modes on, configuration keys, and incident-response
playbooks, and `logging.md` for the server's separate structured log (which carries no payloads at
all).
