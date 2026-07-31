# Cells

One directory per (language, build system) pair. A directory becomes a cell when it contains a
`cell.json`; anything else here is ignored.

`cell.json` is read by two consumers -- `run_cell.py`, which executes the steps, and
`tools/build_integration/build_integration_index.py`, which renders them onto the cell's
documentation page. There is one copy of the commands and both consumers read it, so the manual
cannot describe a recipe that differs from the one CI runs.

```json
{
  "name": "c-cmake",
  "title": "CMake",
  "row": "C / C++",
  "language": "c",
  "build_system": "CMake",
  "idiom": "B",
  "dep_strategy": "list-outputs",
  "summary": "One paragraph, rendered as the page's opening.",
  "notes": "Optional. Gotchas specific to this build system.",
  "generated_dir": "generated",
  "requires": [
    {"command": "cmake", "reason": "configures and builds the cell"}
  ],
  "steps": [
    {"name": "configure", "run": ["cmake", "-S", ".", "-B", "build"]}
  ]
}
```

`idiom` is `A` (the generated tree is the package) or `B` (your project owns the manifest).
`dep_strategy` is `list-outputs`, `depfile`, `stamp`, or `none`. `name` must equal the directory
name. Unknown keys are an error rather than a warning -- a typo that silently dropped a field would
produce a page that misdescribes what ran.

Steps run in order from the staged cell directory, and `{dsdlc}` and `{python}` are substituted into
the argv. A non-zero exit fails the cell at that step.

Remember the invariant: build wiring only, no round-trip source. `run_cell.py --check-invariant`
enforces it.
