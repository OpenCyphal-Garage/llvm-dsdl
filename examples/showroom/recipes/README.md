# Recipes

One directory per (language, build system) pair. A directory becomes a recipe when it contains a
`recipe.json`; anything else here is ignored.

`recipe.json` is read by two consumers -- `run_recipe.py`, which executes the steps, and
`tools/showroom/recipes_index.py`, which renders them onto the recipe's
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
    {"command": "cmake", "reason": "configures and builds the recipe"}
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

Steps run in order from the staged recipe directory, and `{dsdlc}` and `{python}` are substituted into
the argv. A non-zero exit fails the recipe at that step.

Remember the invariant: build wiring only, no round-trip source. `run_recipe.py --check-invariant`
enforces it.
