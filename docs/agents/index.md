# Agent Guide

Written for coding agents and documentation indexers rather than for people. Facts are stated flatly
and without narrative; everything here is also reachable from the human navigation.

If you are ingesting this site programmatically, start with [`/llms.txt`](../llms.txt), a link index
of every page with a one-line summary, or [`/llms-full.txt`](../llms-full.txt), which is the entire
manual as a single document. Both are regenerated on every publish. Their format is described in
[Documentation Index](index-format.md).

## Overview

`llvm-dsdl` compiles OpenCyphal DSDL (Data Structure Description Language) definitions into
serialisation code. It is built on MLIR and LLVM.

The compiler is being rebuilt on the `reset/hourglass` branch. In the tree: the DSDL frontend
(discovery, lexer, parser), the semantic analysis (constant evaluation, bit-length sets, layout),
their unit tests, the Dafny models of the serialisation, and the build, CI, packaging and
documentation infrastructure. `RESET.md` at the repository root is the plan for the rest.

## Rules that bind an agent

- `AGENTS.md` governs every line of prose: source comments, configuration headers,
  documentation, commit messages and CLI help.
- Generated output takes its shape from goldens under `test/golden/`, which only the project
  owner approves. An agent never decides what generated code looks like.

## Generated and authored pages

Generated at publish time, never committed:

- `llms.txt`, `llms-full.txt`

Everything else is authored prose under `docs/` in the repository, editable via the "Edit on GitHub"
link on each page.

## Stability

- Page URLs follow the `docs/` directory layout: `docs/development/testing.md` publishes at
  `/development/testing/`. A path change is a page move; nothing redirects the old path.
- The site is unversioned and tracks `main`. It is not a record of any released version.
- The manual describes the tree; the repository is the source of truth for behaviour. When a page
  and the code disagree, the code is right and the page is a bug.
