# LSP Lint Rules

## Overview

The `dsdld` lint engine runs deterministic rule checks over parsed DSDL ASTs and
emits diagnostics with optional autofix edits.

Rule IDs are stable and can be suppressed via configuration or source comments.

For rule implementation workflow, see `docs/development/lint-rule-authoring.md`.

## Baseline Rules (v1)

1. `naming.type_pascal_case`
2. `naming.field_snake_case` (autofix)
3. `naming.constant_upper_snake_case` (autofix)
4. `naming.namespace_lowercase`
5. `style.no_tabs` (autofix)
6. `style.trailing_whitespace` (autofix)
7. `style.single_trailing_newline` (autofix)
8. `complexity.max_fields_per_type`
9. `complexity.max_constants_per_type`
10. `complexity.max_directives_per_type`
11. `arrays.large_fixed_bound`
12. `arrays.large_variable_bound`

## Configuration Schema

`workspace/didChangeConfiguration` accepts:

```json
{
  "settings": {
    "lint": {
      "enabled": true,
      "disabledRules": ["arrays.large_fixed_bound"],
      "fileSuppressions": {
        "file:///abs/path/demo/TypeA.1.0.dsdl": ["style.no_tabs"],
        "/abs/path/demo/TypeB.1.0.dsdl": ["*"]
      },
      "pluginLibraries": ["/abs/path/libcustom_lints.dylib"]
    }
  }
}
```

## Suppression Model

1. Workspace-level suppression: `lint.disabledRules`
2. Per-file suppression: `lint.fileSuppressions[uri-or-path]`
3. In-source suppression comment:

```text
# dsdld-lint-disable: rule.id.one, rule.id.two
```

Use `*` to suppress all lint rules in a scope.

## Code Actions and Autofix

Lint findings with fixes are exposed as `quickfix` code actions.
Fixes are explicit LSP workspace edits.

## Plugin Extension Path

Shared libraries can provide additional rules by exporting:

```cpp
extern "C" void llvmdsdlRegisterLintRules(llvmdsdl::lsp::LintRegistry& registry);
```

Each plugin registers one or more `LintRuleFactory` instances.
