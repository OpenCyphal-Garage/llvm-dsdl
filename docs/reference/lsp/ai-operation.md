# LSP AI Operator Guide

This guide describes how to operate AI-assisted features in `dsdld` safely.

## 1. Scope

AI support in `dsdld` is optional and policy-gated.
The default provider is deterministic and offline (`OfflineAiProvider`).

For **what data enters the AI surface, what leaves it, and what is retained** — including the bounds on
how much source can reach the provider and why nothing is transmitted off the machine — see
[`ai-data-flow.md`](ai-data-flow.md).

Relevant code:

1. `include/llvmdsdl/LSP/AI.h`
2. `lib/LSP/AI.cpp`
3. `include/llvmdsdl/LSP/ServerConfig.h`
4. `lib/LSP/Server.cpp`

## 2. AI Modes

Supported modes:

1. `off`: AI disabled.
2. `suggest`: non-edit suggestions and explanations.
3. `assist`: richer suggestions and optional confirmation-required edit proposals.
4. `apply_with_confirmation`: allows edit materialization only with explicit confirmation.

Recommended default for shared repos:

1. `off` for conservative posture.
2. `suggest` for low-risk advisory usage.

## 3. Safety Controls

Implemented controls:

1. Mode gate (`AiPolicyGate`) determines allowed behaviors.
2. Confirmed edits only in `apply_with_confirmation`.
3. Tool-use allow-list blocks unapproved operations.
4. Context packing is bounded in size.
5. Audit records are redacted before storage.

Allowed tool-use names today:

1. `analysis.stats`
2. `workspace.symbols`
3. `document.symbols`
4. `document.diagnostics`

## 4. Configuration

Via `workspace/didChangeConfiguration`:

```json
{
  "settings": {
    "ai": {
      "mode": "suggest"
    }
  }
}
```

VS Code equivalents:

1. `dsdld.aiMode`

## 5. Operational Playbooks

### 5.1 Conservative default

1. Set `aiMode` to `off` or `suggest`.
2. Keep lint enabled for deterministic static checks.
3. Review all code actions before applying.

### 5.2 Controlled edit application

1. Set `aiMode` to `apply_with_confirmation`.
2. Require explicit user confirmation for each AI edit resolution.
3. Keep normal PR/code-review gates unchanged.

### 5.3 Incident response

If suspicious AI behavior is observed:

1. Set `aiMode` to `off` immediately.
2. Capture `dsdld/debug/aiAuditLog` output.
3. Reproduce with minimal fixture and add a unit test.
4. File fix and document behavior change.

## 6. Auditing

Audit retrieval endpoint:

1. `dsdld/debug/aiAuditLog`

Redaction currently masks common secret patterns including:

1. `password`
2. `token`
3. `secret`
4. `api_key` / `api-key`
5. `Bearer <token>`

Audit logs are bounded in memory (`MaxRecords`).

## 7. Current Limitations

1. Provider is offline/deterministic and does not call external model APIs yet.
2. No persistent on-disk audit sink is implemented yet.
3. Tool-use surface is intentionally narrow.

## 8. Release Gate Expectations

Before release, verify:

1. Mode gating tests pass.
2. Confirmation gate behavior passes.
3. Tool allow-list rejects unsupported tools.
4. Redaction tests pass.
5. `docs/reference/lsp/ai-operation.md` matches current behavior.
