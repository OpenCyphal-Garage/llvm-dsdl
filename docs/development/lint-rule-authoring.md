# LSP Lint Rule Authoring Guide

This guide explains how to add, test, and ship lint rules for `dsdld`.
For baseline rule catalog and suppression schema, see `docs/reference/lsp/lint-rules.md`.

## 1. Architecture

Primary interfaces:

1. `include/llvmdsdl/LSP/Lint.h`
2. `lib/LSP/Lint.cpp`
3. `test/unit/LspLintTests.cpp`

Key types:

1. `LintRule`: one deterministic rule unit.
2. `LintFinding`: one emitted finding with optional fix edits.
3. `LintRegistry`: rule registration and plugin loading.
4. `LintEngine`: execution and suppression filtering.

## 2. Rule Design Checklist

Before coding, define:

1. Stable rule ID (for suppression/config compatibility).
2. Severity (`Info`, `Warning`, `Error`).
3. Deterministic ordering behaviour.
4. Fix safety (if emitting autofix edits).
5. Scope (statement-level, file-level, namespace-level).

## 3. Adding A Built-In Rule

### 3.1 Implement the rule class

Add a `LintRule` subclass in `lib/LSP/Lint.cpp`:

```cpp
class ExampleRule final : public LintRule
{
public:
    [[nodiscard]] std::string id() const override
    {
        return "example.rule_id";
    }

    [[nodiscard]] std::string title() const override
    {
        return "Example lint rule";
    }

    void run(const LintDocument& document, std::vector<LintFinding>& findings) const override
    {
        // Analyze document.ast and/or document.sourceText.
        // Append deterministic findings.
    }
};
```

### 3.2 Register factory

In `registerBuiltinRules(LintRegistry&)`, add:

```cpp
registry.registerRuleFactory([]() { return std::make_unique<ExampleRule>(); });
```

### 3.3 Keep output deterministic

Use stable iteration order and stable location sorting behaviour.
Avoid non-deterministic containers as final emit order without sorting.

## 4. Autofix Rules

Set these on a finding when fix is available:

1. `finding.hasFix = true`
2. `finding.preferredFix = true` when this should be first quick-fix choice
3. Populate `finding.fixes` with `LintFixEdit` entries

Each edit is `(line, character, length, newText)` in zero-based coordinates.

Guidelines:

1. Emit minimal edits.
2. Avoid overlapping edits in one finding.
3. Keep fix idempotent where practical.
4. Do not alter semantics unless rule is explicitly semantic.

## 5. Suppression Compatibility

Your rule ID participates in all suppression surfaces:

1. Workspace settings: `lint.disabledRules`
2. Per-file settings: `lint.fileSuppressions`
3. Source comment: `# dsdld-lint-disable: <rule-id>`

Use a stable ID because users may persist suppressions in repositories.

## 6. Testing Requirements

Update or add tests in `test/unit/LspLintTests.cpp`:

1. Rule fires on expected fixture.
2. Rule does not fire on conformant fixture.
3. Autofix payload (if any) matches expected edits.
4. Suppression behaviour works.
5. Determinism check remains stable.

If diagnostics goldens are affected, update:

- `test/unit/lint/golden/lint_fixture_diagnostics.golden`

Run:

```bash
cmake --build build/matrix/dev-homebrew --config RelWithDebInfo --target llvmdsdl-unit-tests
build/matrix/dev-homebrew/test/unit/RelWithDebInfo/llvmdsdl-unit-tests
```

## 7. Plugin Rule Packs

`LintRegistry` supports dynamic rule loading. Plugin library must export:

```cpp
extern "C" void llvmdsdlRegisterLintRules(llvmdsdl::lsp::LintRegistry& registry);
```

Inside that function, register one or more factories.

Runtime configuration path:

1. Set `lint.pluginLibraries` in `workspace/didChangeConfiguration`.
2. `LintEngine` loads the libraries during construction.

Current implementation uses `dlopen`/`dlsym` (POSIX dynamic loading).

## 8. Documentation Requirements For New Rules

When adding/removing/changing rules:

1. Update `docs/reference/lsp/lint-rules.md` baseline list and behaviour notes.
2. Mention autofix behaviour if applicable.
3. Add migration notes if rule IDs changed.

## 9. Review Gate

A lint-rule change is ready when:

1. Unit tests pass.
2. Diagnostics remain deterministic.
3. Suppression surfaces behave as documented.
4. Rule documentation is updated.
