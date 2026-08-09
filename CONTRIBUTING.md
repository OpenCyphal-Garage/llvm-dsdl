# Contributing to llvm-dsdl

This is a developer-facing guide for building, testing, and contributing to
`llvm-dsdl`.

## 1. Scope

Use this guide to:

- set up a local development environment
- run project workflows and tests
- use the two install modes (`bin` and `dev`)
- ship changes with the expected validation and review quality

## 2. Repository Layout (Developer View)

- [`include/llvmdsdl/`](./include/llvmdsdl/): public headers for frontend, IR, semantics, transforms,
  codegen, and LSP
- [`lib/`](./lib/): implementation libraries
- [`tools/`](./tools/): user-facing binaries plus their build/report helpers
  - `dsdlc`, `dsdl-opt`, `dsdld`: installed binaries
  - `convergence/`, `determinism/`, `runtime/`, `man/`: scripts driven by custom
    targets and tests, not installed as tools
- [`test/`](./test/): `unit`, `lit`, `integration`, `benchmark`, `fuzz`, and `lint` suites
- [`runtime/`](./runtime/): language runtime scaffolds used by generators
- [`cmake/`](./cmake/): utility scripts used by custom targets and tests
- [`CMakePresets.json`](./CMakePresets.json): canonical configure/build/test/workflow automation

## 3. Prerequisites

Required:

- CMake `>= 3.25` (the project itself declares `cmake_minimum_required(3.24)`, but
  [`CMakePresets.json`](./CMakePresets.json) is schema version 6 and declares 3.25, so preset-driven
  development needs 3.25)
- Ninja
- C++20-capable compiler toolchain
- LLVM + MLIR CMake packages (`LLVMConfig.cmake`, `MLIRConfig.cmake`)
- Git

Common optional tools (enable more checks/lanes):

- Python 3
- `llvm-lit` or `lit` Python module. LLVM tester
- `clang-format`
- `clang-tidy`
- `include-what-you-use`
- `cargo`/`rustc`, `go`, Node/TypeScript (`tsc`) for language-specific
  integration lanes
- MkDocs, to preview the documentation site — installed into its own virtualenv from
  [`docs/requirements.txt`](./docs/requirements.txt), not needed by the CMake build; see section 11

## 4. Clone and Initialize

```bash
git clone <repo-url> llvm-dsdl
cd llvm-dsdl
git submodule update --init --recursive
```

[`submodules/public_regulated_data_types`](./submodules/public_regulated_data_types) is needed for full integration and
`uavcan` generation paths.

## 5. Preset-First Development Workflow

List all presets:

```bash
cmake --list-presets=all
```

### 5.1 Configure

Use one configure preset:

```bash
cmake --preset dev-homebrew
# or
cmake --preset dev-llvm-env
# or
cmake --preset ci
# or
cmake --preset ci-asan
```

All configure presets use the `Ninja Multi-Config` generator with
`CMAKE_CONFIGURATION_TYPES=Debug;RelWithDebInfo;Release` and
`CMAKE_DEFAULT_BUILD_TYPE=RelWithDebInfo`. `ci-asan` additionally compiles the
generated native decoders under ASan+UBSan for the sanitizer lanes.

If using `dev-llvm-env`, set:

```bash
export LLVM_DIR=/path/to/llvm/lib/cmake/llvm
export MLIR_DIR=/path/to/llvm/lib/cmake/mlir
```

### 5.2 Full Matrix Workflows

```bash
cmake --workflow --preset matrix-dev-homebrew
cmake --workflow --preset matrix-dev-llvm-env
cmake --workflow --preset matrix-ci
```

Each workflow runs configure, then build, then a fixed sequence of test lanes:

- `matrix-dev-homebrew` / `matrix-dev-llvm-env`: smoke, release-blocking, full
- `matrix-ci`: smoke, release-blocking, python-accel-required, full

The `ci-asan` preset has no matrix workflow; drive it with the build and test
presets directly.

## 6. Install Modes

`llvm-dsdl` supports two install components:

- `bin`: install tools only (`dsdlc`, `dsdl-opt`, `dsdld`)
- `dev`: install development artifacts (libraries + headers)

The CMake custom targets are:

- `install-bin`
- `install-dev`

### 6.1 Manual Install Invocations

From an already-configured build tree. Both targets install the configuration
named by `--config`, so that configuration must already be built:

```bash
cmake --build build/matrix/ci --config Release --target install-bin
cmake --build build/matrix/ci --config Release --target install-dev
```

Default install prefix for each configure preset is under its matrix build dir:

- `build/matrix/ci/install`
- `build/matrix/ci-asan/install`
- `build/matrix/dev-homebrew/install`
- `build/matrix/dev-llvm-env/install`

To change the install prefix, re-run configure with an explicit
`CMAKE_INSTALL_PREFIX`:

```bash
cmake --preset ci -DCMAKE_INSTALL_PREFIX=$PWD/out/install-custom
cmake --build build/matrix/ci --config Release --target install-bin
cmake --build build/matrix/ci --config Debug --target install-dev
```

## 7. Build and Test Commands

### 7.1 Build presets

Because the generator is multi-config, every build and test invocation selects a
configuration, and the presets do *not* all select the same one:

| Preset | Configuration built/tested |
| --- | --- |
| `build-dev-homebrew`, `build-dev-llvm-env` | `RelWithDebInfo` (from `CMAKE_DEFAULT_BUILD_TYPE`) |
| `build-ci`, `build-ci-asan` | `Debug` (pinned by the preset) |
| every `test-*` preset | `Debug` (pinned by the hidden `test-base` preset) |

Pass `--config <cfg>` to override a build preset:

```bash
cmake --build --preset build-dev-homebrew                        # RelWithDebInfo
cmake --build --preset build-dev-homebrew --config Debug
cmake --build --preset build-dev-homebrew --config Release
```

Since the `test-*` presets run `Debug`, a bare
`cmake --build --preset build-dev-homebrew` does **not** build what
`ctest --preset test-dev-homebrew-*` will try to run. When driving the dev
presets by hand, either build `Debug` explicitly before running a test preset,
or use `ctest --test-dir ... -C RelWithDebInfo` as in section 7.3.

### 7.2 Test presets

Smoke tests (exclude integration-labeled tests):

```bash
ctest --preset test-dev-homebrew-smoke
ctest --preset test-ci-smoke
```

Full suite (preset-defined full lane). `test-ci-full` excludes
`bench`-labeled tests; the dev full lanes do not filter at all:

```bash
ctest --preset test-dev-homebrew-full
ctest --preset test-ci-full
```

Benchmark-only lane (the `bench` label that `test-ci-full` excludes):

```bash
ctest --preset test-ci-bench
```

Release-blocking architecture gates:

```bash
ctest --preset test-dev-homebrew-release-blocking
ctest --preset test-ci-release-blocking
```

These lanes include runtime benchmark threshold gates by default.
They also include execution-engine boundary validation (shared emitter orchestration guards).
They also include hard-cut integrity validation (no-shim canonical-path guard).
Threshold policy is 5% regression budget vs in-repo baselines in:
- [`test/integration/python_runtime_bench_thresholds.json`](./test/integration/python_runtime_bench_thresholds.json)
- [`test/integration/rust_runtime_bench_thresholds.json`](./test/integration/rust_runtime_bench_thresholds.json)
If an intentional performance shift is accepted, update these files in the same PR.

Convergence/runtime validator self-tests:

```bash
ctest --preset test-dev-homebrew-full -R 'llvmdsdl-(convergence-scorecard-selftest|convergence-matrix-reports-selftest|runtime-semantic-wrapper-allowlist-selftest|execution-engine-boundary-guard-selftest|hard-cut-integrity-guard-selftest)'
```

Convergence scorecards include helper-binding completeness classification
alongside verifier-first, contract-v2, and fallback-free dimensions.

CI accelerator-required lane:

```bash
ctest --preset test-ci-python-accel-required
```

Sanitizer lanes (require the `ci-asan` configure preset):

```bash
ctest --preset test-ci-asan-sanitizer
ctest --preset test-ci-asan-full
```

### 7.3 Direct targeted test runs

```bash
ctest --test-dir build/matrix/dev-homebrew -C RelWithDebInfo --output-on-failure -R llvmdsdl-unit-tests
ctest --test-dir build/matrix/dev-homebrew -C RelWithDebInfo --output-on-failure -R llvmdsdl-lit
```

## 8. Generation and Tooling Targets

Generate all `uavcan` backends:

```bash
cmake --build --preset build-dev-homebrew --config RelWithDebInfo --target generate-uavcan-all
```

Common quality targets:

```bash
cmake --build --preset build-dev-homebrew --config RelWithDebInfo --target check-format
cmake --build --preset build-dev-homebrew --config RelWithDebInfo --target check-iwyu
cmake --build --preset build-dev-homebrew --config RelWithDebInfo --target check-clang-tidy
```

Formatting rewrite:

```bash
cmake --build --preset build-dev-homebrew --config RelWithDebInfo --target format-source
```

Convergence/parity/contract report targets. Each writes two things: the JSON the gate consumes, into
the build tree, and the markdown *page* it produces, into `docs/` — `parity-matrix.md`,
`malformed-input.md`, and `determinism.md` under `docs/reference/guarantees/`, and
`docs/development/convergence-scorecard.md`. Those pages are generated, gitignored, and never edited
by hand; running a target overwrites them.

```bash
cmake --build --preset build-dev-homebrew --config RelWithDebInfo --target convergence-report
cmake --build --preset build-dev-homebrew --config RelWithDebInfo --target parity-matrix-report
cmake --build --preset build-dev-homebrew --config RelWithDebInfo --target malformed-contract-report
cmake --build --preset build-dev-homebrew --config RelWithDebInfo --target determinism-matrix-report
cmake --build --preset build-dev-homebrew --config RelWithDebInfo --target release-blocking-report-gates
```

Showroom targets. `showroom` regenerates the example namespace into every language and profile under
`<build-dir>/showroom/`; `showroom-docs` renders that tree into `docs/showroom/`.

```bash
cmake --build --preset build-dev-homebrew --config RelWithDebInfo --target showroom
cmake --build --preset build-dev-homebrew --config RelWithDebInfo --target showroom-docs
```

Documentation targets. Three kinds of page under `docs/` are produced rather than written — the
showroom (compiler output), the guarantee matrices (report-generator output), and
`llms.txt`/`llms-full.txt` (derived from the nav, for agents and documentation crawlers). None are
committed: the Docs workflow builds dsdlc in the toolshed container and produces all of them at
publish time. Nothing to regenerate and commit, then, but you do need `docs-generate` before
previewing the site locally, or mkdocs will fail on a nav entry pointing at a page that is not there.

```bash
cmake --build --preset build-dev-homebrew --config RelWithDebInfo --target docs-generate
```

`docs-llms` also enforces the coverage rule that keeps the nav honest: every page under `docs/` is
either in the mkdocs nav or listed in `not_in_nav:`, and a page that is neither fails the build.
Adding a page means adding it to the nav in `mkdocs.yml`.

One check needs the built site rather than the source tree, so it runs after `mkdocs build` in both
workflows rather than from CMake — mermaid diagrams fail in the browser, not at build time, so
`--strict` cannot see them:

```bash
python3 tools/docs/check_rendered_mermaid.py --docs-dir docs --site-dir site
```

## 9. Where This Project Lives

`project-identity.json` at the repository root is the only place that names the owner. Everything
else that mentions it — documentation links, the README badge, `mkdocs.yml`, the packaging homepage,
the manpage bug address — is derived from it:

```bash
python3 tools/repo_identity.py          # report anything that disagrees (CI runs this)
python3 tools/repo_identity.py --fix    # rewrite it
```

The repository has moved before and moves again at v1.0, so treat a hand-edited URL as a defect: put
the new value in `project-identity.json` and run `--fix`. The same check rejects a committed
`/Users/<somebody>/…` path, which is wrong for every reader but its author.

`runtime/go/go.mod` is deliberately outside this: its module path is `opencyphal.org/llvm-dsdl/…`,
which encodes no GitHub owner and therefore survives the moves untouched.

## 10. Optional Coverage Workflow

Enable coverage at configure time:

```bash
cmake -S . -B build/coverage -G "Ninja Multi-Config" \
  -DLLVM_DIR=/path/to/llvm/lib/cmake/llvm \
  -DMLIR_DIR=/path/to/llvm/lib/cmake/mlir \
  -DLLVMDSDL_ENABLE_LLVM_COVERAGE=ON
```

Run coverage pipeline:

```bash
cmake --build build/coverage --config RelWithDebInfo --target coverage-report -j1
```

Outputs land in `build/coverage/coverage/RelWithDebInfo/`:

- `summary.txt`
- `coverage.lcov`
- `html/`

This mirrors [`.github/workflows/coverage.yml`](./.github/workflows/coverage.yml); keep the two in sync.

## 11. Documentation Site Workflow

The user manual under [`docs/`](./docs/) is a MkDocs site. This mirrors
[`.github/workflows/docs.yml`](./.github/workflows/docs.yml); keep the two in sync.

### 11.1 One-time setup

The docs toolchain is independent of the CMake build — it needs no compiler and no LLVM. A virtualenv
at `.venv-docs/` is the repository convention (it is gitignored, as is the generated `site/`). To skip
the host install entirely, use the container in section 11.4 instead.

```bash
python3 -m venv .venv-docs
```

```bash
.venv-docs/bin/pip install -r docs/requirements.txt
```

Install from [`docs/requirements.txt`](./docs/requirements.txt) rather than by name: the versions there
are pinned as a set, and the file explains why the Pygments ceiling in particular cannot be raised on
its own.

### 11.2 Live preview

```bash
.venv-docs/bin/mkdocs serve
```

**The site is served under `/llvm-dsdl/`, not at the root**, because `site_url` in
[`mkdocs.yml`](./mkdocs.yml) carries the GitHub Pages base path. Opening
<http://127.0.0.1:8000/> redirects there, but a deep link typed by hand needs the prefix — for example
<http://127.0.0.1:8000/llvm-dsdl/showroom/>. Edits to any page reload the browser automatically;
changes to `mkdocs.yml` need a restart.

### 11.3 Build exactly what CI builds

```bash
.venv-docs/bin/mkdocs build --strict
```

`--strict` promotes warnings to errors and is what the Docs workflow runs, so a page that builds
locally without it can still fail CI — a broken internal link is the usual cause. Output lands in
`site/`. Pages that exist but are absent from the `nav` in `mkdocs.yml` are reported as INFO and do not
fail the build; the per-type showroom pages are intentionally in that category.

### 11.4 Containerised toolchain

If you would rather not keep a Python environment on the host — or want the same toolchain on a second
machine, a fresh clone, or a colleague's laptop — use the container. The image holds the toolchain and
nothing else; the project being rendered is bind-mounted at run time, so one image serves any
directory containing an `mkdocs.yml`.

```bash
tools/docs/docs-container.sh build
```

```bash
tools/docs/docs-container.sh serve
```

```bash
tools/docs/docs-container.sh check
```

`serve` and `check` take an optional directory argument and default to this repository, so the same
image renders a worktree or an unrelated checkout without rebuilding:

```bash
tools/docs/docs-container.sh serve ~/src/some-other-worktree
```

Behaviour worth knowing:

- The image is built if it is missing, so `build` is only needed to pick up a dependency change.
- `serve` mounts read-only and live-reloads on edit, exactly like a host `mkdocs serve`; the same
  `/llvm-dsdl/` base path applies.
- `check` needs a writeable mount because it renders into `site/`, and runs the container as your own
  UID/GID so the output is not left root-owned.
- Overrides: `LLVMDSDL_DOCS_PORT` (default 8000), `LLVMDSDL_DOCS_IMAGE`, and `DOCKER` (set it to
  `podman` if that is what you run).

The image pins Python 3.14 to match [`.github/workflows/docs.yml`](./.github/workflows/docs.yml), and
installs from the same [`docs/requirements.txt`](./docs/requirements.txt) as a local `.venv-docs`, so
`check` inside the container fails for the same reasons CI fails. See
[`packaging/docker/Dockerfile.docs`](./packaging/docker/Dockerfile.docs).

### 11.5 Generated pages

`docs/showroom/` is generated from the compiler's own output and is not committed — it is gitignored.
The Docs workflow runs inside the toolshed container, builds `dsdlc`, and produces those pages as
part of publishing, so the repository never carries a copy that can go stale.

The consequence for you is local: a fresh clone has no `docs/showroom/`, and `mkdocs build --strict`
fails on the `Showroom / Types` nav entry until you make one. Run the `showroom-docs` target from
section 8 before previewing or `check`ing the site. Regenerate it again after changing anything under
`examples/showroom/` or anything that alters generated code, since the pages carry backend output
verbatim.

### 11.6 Run the whole Docs workflow before pushing

Sections 11.3 and 11.4 build the site from a `docs/showroom/` you generated yourself. The published
site is generated by the workflow, so the question they cannot answer is whether *that* still works
end to end. [`act`](https://github.com/nektos/act) runs it:

```bash
act push -W .github/workflows/docs.yml
```

It takes about half a minute and needs no arguments, no Pages setup and no secrets. The steps that
talk to GitHub — `configure-pages`, `upload-pages-artifact`, and the deploy job's only step — are
guarded with `if: ${{ !env.ACT }}` and skip, which costs nothing: none of them affect what is built,
since `mkdocs` takes `site_url` from `mkdocs.yml`. Under `act push` the upload and the whole deploy
job are also outside their own event conditions (section 11.7), so a push simulation exercises
exactly the build. Everything that can actually fail still runs: the CMake configure, the
`docs-generate` build, the pinned pip install, `mkdocs build --strict`, and the rendered-mermaid
check.

It is a genuine from-scratch *build*, not a reuse of your build tree. `act` honours `.gitignore` when
it copies the repository into the container, so `build/` and `.venv-docs/` are left behind — about
90 MB goes in, and `dsdlc` is compiled inside the container from nothing. That is also why it cannot
be made faster by building first.

**Know what it cannot catch.** `act` copies your *working tree*; it does not clone. Anything that
differs between your checkout and a fresh one is invisible to it, and submodules are the trap that
has already been sprung once: your `submodules/public_regulated_data_types` is populated, so the 58
`llvmdsdl-uavcan-*` tests register and the guarantee matrices come out complete. A workflow that
checks out without submodules has none of that, and the failure is not subtle — a parity score of
12 against a minimum of 100, while `act` reports success on the same commit. Every workflow
therefore checks out with `submodules: recursive`; if you touch a `checkout` step, re-read this
paragraph before trusting a green local run.

The repository's [`.actrc`](./.actrc) already pins `--container-architecture` and `--pull=false`, so
the toolshed image has to be present locally; `docker pull ghcr.io/opencyphal/toolshed:ts26.4.3` once
if it is not.

### 11.7 How the site gets published

**What is served on GitHub Pages is the last release, not main.** A reader following the manual is
reading instructions for a version they can download; main routinely documents flags and behaviour
that no released binary has.

Building and publishing are separate events in
[`docs.yml`](.github/workflows/docs.yml):

| Event | Builds | Publishes | Built from |
|---|:--:|:--:|---|
| Push to `main` | yes | **no** | `main` |
| Release published | yes | yes | the release tag |
| `workflow_dispatch` | yes | only with `publish=true` | the ref you select |

The main-push build is a pure check, and it is the one that has to stay green: it is what stops
release day from being the first time anyone found out the site does not build. Prereleases are not
excluded — every release so far has been one — so publishing a prerelease does update the site.

#### Publishing a patch between releases

The manual path exists because a released doc page can be wrong, and waiting for the next release to
fix it is not always acceptable:

```bash
gh workflow run Docs --repo OpenCyphal-Garage/llvm-dsdl --ref <ref> -f publish=true
```

Watch it with `gh run watch`, or check the Actions tab; it takes the same half-minute-plus-build as
any other Docs run.

`--ref` decides what goes live, and it is the whole decision. **Selecting `main` publishes
unreleased documentation** — exactly what the split above exists to prevent — so do that only if you
have decided the site should lead the release, and know that the next release will overwrite it
anyway. The normal patch flow keeps the published site matching the released tools:

```bash
git switch --detach vX.Y.Z
git switch -c docs-patch-vX.Y.Z
git cherry-pick <commit-with-the-fix>     # or write the fix here
git push upstream docs-patch-vX.Y.Z
gh workflow run Docs --repo OpenCyphal-Garage/llvm-dsdl --ref docs-patch-vX.Y.Z -f publish=true
```

Two things to remember afterwards. The published site now differs from `vX.Y.Z` — the tag is not
moved, and moving it would invalidate the release attestation — so the branch is the only record of
what is live; keep it until the next release supersedes it. And the fix has to land on `main`
separately, or the next release republishes the broken page.

Dispatching with `publish=false` (the default) just builds the selected ref, which is a cheap way to
check that a tag or branch still produces a site before you publish it.

## 12. Development Expectations

### 12.1 Keep behaviour centralized

When touching backend code generation semantics, prefer shared planning and
helper layers in [`lib/CodeGen`](./lib/CodeGen) and avoid re-introducing backend-local duplicate
logic for core serdes semantics.
When touching runtime behaviour above primitive bit/number operations, keep
exceptions explicit in [`runtime/semantic_wrapper_allowlist.json`](./runtime/semantic_wrapper_allowlist.json)
with owner and rationale.
Generate runtime semantic wrappers from templates before commit:
`python3 tools/runtime/generate_runtime_semantic_wrappers.py --repo-root .`
Validate allowlist integrity with the release-blocking integration lane
`llvmdsdl-runtime-semantic-wrapper-allowlist` (includes semantic-wrapper generation drift checks).
Use `llvmdsdl-runtime-semantic-wrapper-allowlist-selftest` to regression-test
validator and generation-check mutation coverage.

### 12.2 Add tests with behaviour changes

For any semantic/codegen/runtime behaviour change, include:

- at least one focused unit test and/or integration test
- updates to affected golden expectations if applicable

### 12.3 Keep docs in sync

If you change CLI behaviour, targets, workflows, or runtime contracts, update:

- [`README.md`](./README.md)
- [`CONTRIBUTING.md`](./CONTRIBUTING.md)
- relevant docs under [`docs/`](./docs/)
- tool-specific docs (for example [`tools/dsdld/README.md`](./tools/dsdld/README.md))

Preview the rendered result before opening the PR, and build it the way CI does — see section 11.

## 13. Pull Request Checklist

Before opening a PR:

1. Rebase on latest target branch.
2. Run at least one full preset lane relevant to your change.
3. Run focused tests for touched areas.
4. Run formatting/lint checks when applicable.
5. Summarize exactly what was validated in the PR description.

In the PR description, include:

- configure/build/test commands used
- which preset/workflow was run
- any non-default options toggled
- risk areas and follow-up work (if any)

### 13.1 The `release-ci` label

CI compiles and tests on Linux. It builds no package, so a green PR says the
code works, not that it ships. The `release-ci` label adds the release build to
the run — both `.deb` architectures, the pristine-container install, both macOS
tarballs and their linkage assertions — as a dry run that publishes nothing.

Reviewers ask for the label when a PR touches:

- `packaging/`, including the toolchain pin and the release Dockerfiles
- `VERSION` or `packaging/deb/changelog`
- install rules, component assignment, or anything CPack reads
- `.github/workflows/release.yml`
- linkage: vendoring, static libc++, or what the tools link against

GitHub cannot make a label a merge requirement, so this is the reviewer's to
enforce. Applying it starts a run immediately; it costs about six minutes.

## 14. Cutting a Release

Three things carry the version and must agree: `VERSION`, the top entry of
`packaging/deb/changelog`, and the git tag. All three are checked before
anything is built.

### 14.1 Preconditions

This project builds the LLVM it links against rather than taking a
distribution's. `packaging/toolchain/llvm.pin` names the revision; the Toolchain
workflow builds it and publishes the result under names derived from that line.
The release pulls what Toolchain published — it never builds LLVM itself,
because that takes about forty minutes per architecture and once exceeded the
job's timeout. A missing artifact fails the job with
`No published toolchain for pin <ref>` rather than quietly rebuilding.

So:

- **Pin unchanged** — the artifacts already exist. Nothing to do.
- **Pin also being moved in this release** — push that change first and let the
  Toolchain workflow finish, then tag.

[`docs/development/release-packaging.md`](./docs/development/release-packaging.md) §2 describes
what the pin produces and which job consumes each form.

### 14.2 Prepare the version

[`tools/prepare_release.py`](./tools/prepare_release.py) does this step and
checks it against upstream:

```bash
python3 tools/prepare_release.py minor
```

It takes one of `major`, `minor`, `patch`, or `current` (use `VERSION` as-is),
bumps `VERSION`, prepends a changelog entry drafted from the commit subjects
since the last tag, and stages both files. `--dry-run` prints what it would do;
`--version` prints the project version rather than the script's.

Two checks run before anything is written.

**The working tree must be clean.** `git commit` commits the whole index, not
just what the script added, so an unrelated staged change would be swept into a
commit named after the release. A tracked change, staged or unstaged, aborts the
run; untracked files are reported but do not block, since nothing untracked can
reach the commit.

**No existing tag may be at or ahead of the new version.** Of the three places
the version lives, the tag is the one whose collision costs an unwind rather
than an edit, and it is otherwise not caught until after the tag is pushed. The
script asks GitHub for the tags that already exist. `--offline` skips the
question for a machine that cannot reach GitHub — it does not make the collision
safe.

The git flags differ in how much they switch off:

| Flag | Clean-tree check | Stages | Commits |
|---|:--:|:--:|:--:|
| *(default)* | yes | yes | no |
| `--commit` | yes | yes | yes |
| `--no-stage` | yes | no | no |
| `--no-git` | **no** | no | no |

`--no-git` issues no git commands at all, which is what disables the check;
`--no-stage` only declines to stage. `--commit` combines with neither. Under
`--dry-run` a dirty tree is a warning rather than an abort, and the rest of the
plan still prints — a dry run writes nothing, so there is nothing to protect.

The entry it drafts reads like a list of commit subjects, because that is what
it is. Edit it into prose before merging.

The rest of this section is what the script does, and what to do by hand
without it.

Set `VERSION` to `X.Y.Z`. It is the single source of truth: CPack takes the
package version from it, and the tools compile it into `--version`.

Add a new **top** entry to `packaging/deb/changelog`:

```
llvm-dsdl (X.Y.Z) unstable; urgency=medium

  * What changed.

 -- Your Name <you@example.com>  Tue, 05 Aug 2026 12:00:00 +0000
```

The trailer line is where the man pages take their date, so that rebuilding a
release produces byte-identical pages rather than pages stamped with whenever
the rebuild happened. Configure fails if it cannot parse a date from it.
`dch --newversion X.Y.Z` writes both the entry and the trailer correctly, if
you have devscripts.

Merge to `main`.

### 14.3 Dry run

```bash
gh workflow run Release --repo OpenCyphal-Garage/llvm-dsdl --ref main -f dry_run=true
```

This builds and verifies everything and publishes nothing: both `.deb`
architectures, both macOS tarballs, the pristine-container install check, and
the macOS linkage assertions. Worth doing before tagging, because a pushed tag that
fails is more annoying to unwind than a workflow run that does.

### 14.4 Tag

```bash
git tag vX.Y.Z && git push upstream vX.Y.Z
```

The tag triggers the release with `publish=true`. What you get is a **draft**
release with the packages, checksums and a provenance attestation attached.
Nothing is public until you publish it from the releases page.

Publishing that draft does one more thing: it fires the Docs workflow's
`release: published` trigger, which rebuilds the site from the tag and deploys
it to GitHub Pages. So the documentation goes live with the binaries it
describes, and not before — see section 11.7, including how to patch the
published site without cutting a new release.

### 14.5 What is enforced, and where

| Check | Where | Failure mode it prevents |
|---|---|---|
| Tag matches `VERSION` | `release.yml` `stage` | Binaries that misreport their own version |
| Changelog top entry matches `VERSION` | `release.yml` `stage`, and CMake at configure time | A Debian package whose changelog disagrees with its control file is malformed |
| Changelog trailer parses as a date | CMake at configure time | Man pages stamped with the build date, so no two builds agree |
| Toolchain image exists for the pin | `release.yml` `build` | A silent forty-minute inline rebuild, or a release built against the wrong LLVM |

## 15. Troubleshooting Quick Notes

### CMake cannot find LLVM/MLIR

- verify `LLVM_DIR` and `MLIR_DIR`
- use `dev-homebrew` preset on macOS/Homebrew
- rerun configure after changing environment variables

### lit tests are not present

- install `llvm-lit` or Python `lit`
- rerun configure

### workflow preset missing

- ensure CMake version is `>= 3.25`
- verify with `cmake --list-presets=all`

### integration lanes fail due external toolchains

- verify Python/Rust/Go/Node toolchains installed for impacted lanes
- use smoke presets first to validate core build health

## 16. Useful References

- [`README.md`](./README.md)
- [`DESIGN.md`](./DESIGN.md)
- [`AGENTS.md`](./AGENTS.md)
- [`docs/index.md`](./docs/index.md)
- [`mkdocs.yml`](./mkdocs.yml), [`docs/requirements.txt`](./docs/requirements.txt): documentation site
  configuration and pinned toolchain
- [`tools/dsdld/README.md`](./tools/dsdld/README.md)
- [`.github/workflows/`](./.github/workflows/): `ci.yml`, `coverage.yml`, `docs.yml`, `release.yml`
