# Contributing to llvm-dsdl

How to build, test and change this repository. [AGENTS.md](./AGENTS.md) governs every line of prose
written here and holds the rule that generated output takes its shape from approved goldens;
read it first.

## 1. Repository layout

- [`include/llvmdsdl/`](./include/llvmdsdl/) and [`lib/`](./lib/): the frontend, the semantics
  and the support library
- [`test/unit/`](./test/unit/): tier 0, the unit, exactness and fuzz suites
- [`spec/dafny/`](./spec/dafny/): the machine-checked models
- [`runtime/`](./runtime/): the C runtime primitives
- [`tools/`](./tools/): scripts driven by custom targets and tests
- [`cmake/`](./cmake/): scripts the custom targets run
- [`packaging/`](./packaging/): the toolchain build, the release Dockerfiles, Debian metadata
- [`examples/showroom/dsdl/`](./examples/showroom/dsdl/): the `lanyard` namespace, a corpus
- [`CMakePresets.json`](./CMakePresets.json): the configure, build, test and workflow presets

## 2. Prerequisites

- CMake 3.25 or newer, Ninja, Git
- A C++23 compiler
- LLVM and MLIR CMake packages at the locked major, 22
- Python 3
- `clang-format` and `clang-tidy` at the major the lint job runs, for the two check targets
- MkDocs, installed into its own virtualenv from [`docs/requirements.txt`](./docs/requirements.txt),
  to build the documentation site

## 3. Clone and configure

```bash
git clone <repo-url> llvm-dsdl
cd llvm-dsdl
git submodule update --init --recursive
cmake --preset ci
```

`cmake --list-presets=all` lists every preset. `dev-homebrew` configures against Homebrew's LLVM on
macOS; `dev-llvm-env` reads `LLVM_DIR` and `MLIR_DIR` from the environment; `ci-asan` instruments
every target with ASan and UBSan and needs Clang.

Every configure preset uses the Ninja Multi-Config generator with Debug, RelWithDebInfo and
Release. The build presets for `ci` and `ci-asan` build Debug; the `dev-*` build presets build
RelWithDebInfo unless `--config` says otherwise. Every test preset runs Debug, so build Debug before
running one.

## 4. Build and test

```bash
cmake --build --preset build-ci
ctest --preset test-ci-t0
ctest --preset test-ci-full
```

Tests carry a tier label. `t0` runs on every build and every pull request; `test-ci-full` runs
every tier. The workflow presets `matrix-ci`, `matrix-dev-homebrew` and `matrix-dev-llvm-env`
run configure, build, tier 0 and then every tier.

### Warnings are errors

Every build of this project's own C and C++ sources promotes warnings to errors, unconditionally.
LLVM's `HandleLLVMOptions` supplies the warning set and the top-level `CMakeLists.txt` sets
`CMAKE_COMPILE_WARNING_AS_ERROR`. CI builds with GCC on Linux and with Clang on macOS, so a
diagnostic one compiler never shows still fails the other.

## 5. Format and tidy

```bash
cmake --build build/matrix/ci --config Debug --target check-format
cmake --build build/matrix/ci --config Debug --target check-clang-tidy
cmake --build build/matrix/ci --config Debug --target format-source
```

The lint job runs the first two with the toolshed image's clang-format and clang-tidy. A local
clang-format of another patch level can disagree on alignment; formatting the touched files from
the image settles it:

```bash
docker run --rm -v "$PWD":/w -w /w ghcr.io/opencyphal/toolshed:ts26.4.5 clang-format -i <files>
```

## 6. Install

The `dev` component installs the libraries, the headers, the licences and the Debian changelog:

```bash
cmake --build build/matrix/ci --config Release --target install-dev
```

Each configure preset installs under its own build directory; pass `CMAKE_INSTALL_PREFIX` at
configure time to change that.

## 7. Documentation site

```bash
python3 -m venv .venv-docs
.venv-docs/bin/pip install -r docs/requirements.txt
cmake --build build/matrix/ci --config Debug --target docs-generate
.venv-docs/bin/mkdocs build --strict
python3 tools/docs/check_rendered_mermaid.py --docs-dir docs --site-dir site
```

`docs-generate` writes `llms.txt` and `llms-full.txt` from the navigation. `--strict` fails on a
page that is in neither the navigation nor its exclusion list, on a broken link and on a nav entry
with no page behind it. `.venv-docs/bin/mkdocs serve` previews the site.

The site is built on every push to `main` and published only when a release is published, so what
is served matches a version a reader can download.

## 8. Pull requests

A branch rebases on `main`; it is never brought up to date with a merge commit, and `main` is
squash-only. A pull request carries:

- the configure, build and test commands run, and which preset
- before-and-after generated code where output changes
- the `shape-approved` label, applied by the project owner, where a file under `test/golden/`
  changes; CI fails the pull request without it
- for a change to a dialect, a pass or a backend interface, the answers to the generality
  checklist in `RESET.md` section 5.7

A pull request is under 1,000 lines of hand-written change or it is split.

## 9. Toolchain and release

This project builds the LLVM it links against. [`packaging/toolchain/llvm.pin`](./packaging/toolchain/llvm.pin)
names the revision; the Toolchain workflow builds it and publishes the result under names derived
from that line; CI restores it per job. [Release packaging](./docs/development/release-packaging.md)
describes the release pipeline. No release is cut from the `reset/hourglass` branch; the tools it
packages return with the rebuild.
