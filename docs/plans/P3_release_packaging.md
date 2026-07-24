# P3 — Release packaging and distribution

How `dsdlc` / `dsdl-opt` / `dsdld` get from a git tag to a user's machine, across package
managers and CPU architectures.

**Pilot scope:** a 2×2 matrix — {apt, brew} × {amd64, arm64}. Snap, Windows, and RPM are
designed for here but built later, and the pilot's job is to prove the *factoring* is right so
those are additive.

**Status:** plan only. Nothing in this document is implemented yet.

---

## 0. The load-bearing constraint (read this first)

**Five of the six backends cannot run from an installed binary today.** The C, C++, Rust, Go,
and Python emitters read their runtime support sources from a path baked into the executable at
compile time:

| Backend | Site | Reads |
|---|---|---|
| C | [CEmitter.cpp:604](../../lib/CodeGen/CEmitter.cpp) | `${LLVMDSDL_SOURCE_DIR}/runtime/dsdl_runtime.h` |
| C++ | [CppEmitter.cpp:1897](../../lib/CodeGen/CppEmitter.cpp) | same, plus `:1918` |
| Rust | [RustEmitter.cpp:1604](../../lib/CodeGen/RustEmitter.cpp) | `${LLVMDSDL_SOURCE_DIR}/runtime/rust/…` |
| Go | [GoEmitter.cpp:1458](../../lib/CodeGen/GoEmitter.cpp) | `${LLVMDSDL_SOURCE_DIR}/runtime/go/dsdl_runtime.go` |
| Python | [PythonEmitter.cpp:1542](../../lib/CodeGen/PythonEmitter.cpp) | `${LLVMDSDL_SOURCE_DIR}/runtime/python/…` |

`LLVMDSDL_SOURCE_DIR` is a compile definition set at
[lib/CodeGen/CMakeLists.txt:72](../../lib/CodeGen/CMakeLists.txt) to the **build machine's**
absolute source path. In a package built by GitHub Actions that is
`/home/runner/work/llvm-dsdl/llvm-dsdl` — a directory that does not exist on the user's machine.
`loadRuntimeHeader()` then falls back to the *cwd-relative* `runtime/dsdl_runtime.h`, which only
resolves if the user happens to be standing in a source checkout. Otherwise: `failed to read
runtime header`.

Nothing else in this plan matters until this is fixed. It is **Phase 0, item 1**, and §6 exists
largely so a regression here fails a release rather than reaching a user.

Two candidate fixes:

- **(A) Embed the runtime sources in the binary** as a generated `.inc`, exactly like the
  existing 545 KB `lib/CodeGen/UavcanEmbeddedMlir.inc` catalog. Precedent in-tree, no path
  resolution at runtime, no packaging split, works identically from a build tree, a `.deb`, a
  bottle, and a snap. The runtime sources are small (~3.6k LOC total across five languages).
- **(B) Install to `${CMAKE_INSTALL_DATADIR}/llvm-dsdl/runtime/` and resolve relative to
  `argv[0]`** (via `llvm::sys::fs::getMainExecutable`), with `LLVMDSDL_SOURCE_DIR` kept as a
  dev-tree fallback. More conventional, but adds a relocation concern to every package format
  and a new failure mode per format.

**Recommendation: (A).** Ship the `.inc` generator with a checksum assertion, and — learning
from G8's stale-catalog finding in the
[review](../PROJECT_REPORT_AND_RELEASE_ROADMAP.md) — have CMake *invoke* the generator rather
than trusting a committed artifact, or add a build-time test that regenerates and diffs.

### Other Phase-0 gaps found

2. **Runtime sources are not installed at all.** The `install()` rules in
   [CMakeLists.txt:522-585](../../CMakeLists.txt) cover the three tools, eight static libraries,
   `include/llvmdsdl`, the generated dialect headers, `Version.h`, and the SBOM. `runtime/` is
   absent. Fix (A) above makes this moot for codegen, but consumers compiling generated C still
   want `dsdl_runtime.h` on disk — install it under `${CMAKE_INSTALL_DATADIR}/llvm-dsdl/runtime/`
   in the `bin` component regardless.
3. **No CPack configuration.** There is no `include(CPack)`, no `CPACK_*`. The install
   *components* (`bin`, `dev`) already exist and map 1:1 onto `llvm-dsdl` / `llvm-dsdl-dev`
   Debian packages, which is the good news.
4. **No tag/VERSION consistency check.** `VERSION` says `0.1.0`; nothing stops a `v0.2.0` tag
   from shipping binaries that self-report `0.1.0`.
5. **The self-contained bundle is flat.** `bundle-tools-self-contained`
   ([cmake/BundleSelfContainedTools.cmake](../../cmake/BundleSelfContainedTools.cmake)) already
   does the hard part — `patchelf` / `install_name_tool` rewrites, `@loader_path`, ad-hoc
   codesign — but emits binaries and libraries into one flat directory, and only covers `dsdlc`
   and `dsdl-opt` (not `dsdld`). Packaging needs a `bin/` + `lib/` split and all three tools.

---

## 1. What ships

| Component | Contents | Package |
|---|---|---|
| `bin` | `dsdlc`, `dsdl-opt`, `dsdld`, SBOM, runtime sources, licences | `llvm-dsdl` |
| `dev` | 8 static libs, `include/llvmdsdl`, generated dialect headers, `Version.h` | `llvm-dsdl-dev` |

The `dev` component is only interesting to someone linking the libraries. For the pilot, build
it but publish only `bin` to brew (Homebrew has no split-package concept worth using here);
apt gets both.

### The LLVM linkage question

The tools link MLIR component libraries (static on both Debian and Homebrew LLVM builds) plus
`libLLVM` **shared** (`libLLVM.so.22.1` / `libLLVM.dylib`). Combined with the
[LLVM 22 major-version lock](../SUPPLY_CHAIN.md), that gives three options:

| Option | apt | brew |
|---|---|---|
| Depend on the distro's LLVM | needs the user to add `apt.llvm.org` — unacceptable UX | `depends_on "llvm@22"` — plausible |
| Vendor `libLLVM` into a private libdir | **recommended** | not idiomatic |
| Static-link LLVM | huge binaries, and MLIR's static story is painful | same |

**Recommendation:**

- **apt: vendor.** Ship `/usr/lib/llvm-dsdl/lib/libLLVM.so.22.1` with `RPATH=$ORIGIN/../lib`,
  reusing the bundle target. This is *our* apt repo, not Debian proper, so Debian's
  no-vendoring policy does not bind us. LLVM is Apache-2.0-WITH-LLVM-exception — redistribution
  is fine with attribution; extend [THIRD_PARTY_NOTICES.md](../../THIRD_PARTY_NOTICES.md) and
  ship it as `/usr/share/doc/llvm-dsdl/copyright`. The package then depends only on `libc6`,
  `libstdc++6`, and a short list of small system libraries.
- **brew: `depends_on "llvm@22"`.** Idiomatic, and Homebrew users already have large LLVM
  installs. **But this couples us to Homebrew's formula lifecycle:** when brew's `llvm` rolls to
  23 and `llvm@22` ages out, our formula breaks. Add a scheduled CI job that asserts `llvm@22`
  still exists and still reports major 22 — a canary, so the break is a red check rather than a
  user's bug report. (CI already asserts the LLVM major on the macOS lane at
  [ci.yml:295](../../.github/workflows/ci.yml); reuse the idea.)

### glibc floor

The CI Linux lane builds inside `ghcr.io/opencyphal/toolshed:ts26.4.1` on `ubuntu-26.04`. A
`.deb` built there links a very new glibc and will refuse to install on anything older.
**Release builds must not reuse the CI container.** Build release Linux artifacts on the oldest
still-supported Ubuntu that has LLVM 22 available from `apt.llvm.org` — verify which that is as
part of the Phase-1 spike; assume 22.04 (jammy) until proven otherwise. The verify job (§6)
pins the floor honestly by installing into a pristine container of that release.

Note that with vendoring, **the floor is set by the prebuilt `libLLVM` we ship, not by our own
code** — so it cannot be lowered by build flags alone. Choosing the floor freely requires
building LLVM ourselves; see below.

### Where LLVM 22 comes from — the cached-toolchain pipeline

Every hard constraint in this document traces back to *whose* LLVM 22 we build against:

| Constraint | Root cause |
|---|---|
| glibc floor on the `.deb` | apt.llvm.org's build, not ours |
| brew formula breaks when `llvm@22` ages out | Homebrew's formula lifecycle |
| No Windows target at all (§5) | no prebuilt LLVM **with MLIR** for Windows exists |
| Cross-compilation is impractical (§10) | no target-built LLVM/MLIR to link against |
| macOS/Linux stdlib mismatch (libc++ vs libstdc++) | two different vendors' builds |

All five dissolve with one piece of infrastructure: **build LLVM/MLIR 22 once per target, cache
the result, and restore it in the build job.** A separate workflow builds LLVM at a pinned
revision for each target triple, publishes the install tree as a GHCR image (Linux) or a release
artifact (macOS/Windows), keyed by `llvm-rev + triple + stdlib`. It runs when the pin changes,
not per release. The `build` job then restores a toolchain instead of `apt install`-ing one.

This is the highest-leverage item in the plan. It is **not, however, a prerequisite for the
pilot** — and deliberately so. The four-stage factoring in §2 exists precisely so the toolchain
source is an implementation detail of the `build` job: the pilot proves the pipeline shape using
distro and Homebrew LLVM, and the cached toolchain swaps in later **without touching the
package, verify, or publish stages**. Building it first would delay the pilot by the cost of
four multi-hour LLVM builds to de-risk something the pilot doesn't exercise.

Sequencing: Phase 1 pilots on distro/brew LLVM; Phase 1b builds the cached toolchain as a
parallel track; Phase 2 swaps the build job onto it and lowers the glibc floor deliberately.
Windows (Phase 3) cannot start until 1b lands.

---

## 2. Workflow architecture

The single most important structural decision: **separate build from package from verify from
publish.** Adding snap or WinGet later must add a *packaging* job, not another *build* job.

```
tag v* ──▶ stage ──▶ build (os × arch) ──▶ package (format × arch) ──▶ verify (format × arch) ──▶ publish
             │            │                      │                          │                       │
        version/tag   dist-<triple>.tar.gz   .deb / bottle             clean-container         GH release,
        consistency   (component staging      + checksums              install + smoke         apt repo,
                       tree, relocatable)                                                       brew tap
```

- **`stage`** — derive the version, assert `git tag == VERSION`, emit a matrix JSON so the
  build/package/verify matrices are defined in exactly one place, and decide dry-run vs publish.
- **`build`** — package-format-agnostic. Configure with a new `release` preset, build
  `RelWithDebInfo`, run the release-blocking gates, `cmake --install` each component into a
  `DESTDIR` staging tree, run the bundle target, and upload `dist-<os>-<arch>.tar.gz`. **One
  artifact per platform, consumed by every packaging job for that platform.**
- **`package`** — downloads a dist artifact, emits one package format. Pure metadata + repack;
  no compiler runs. Fast, and trivially parallel across formats.
- **`verify`** — §6. The centerpiece.
- **`publish`** — gated on all verifies passing; creates the GitHub release, pushes to the apt
  repo and the brew tap, and attaches provenance attestations.

### Files

```
.github/workflows/release.yml            # the pipeline above
.github/workflows/packaging-smoke.yml    # PR-time: build+verify the amd64 .deb only
.github/actions/build-dist/action.yml    # composite: configure → build → gates → install → bundle → tar
packaging/
  dist/triples.json                      # single source of truth for the matrix
  deb/                                   # control template, copyright, lintian overrides
  brew/llvm-dsdl.rb.in                   # formula template (version/sha256 substituted)
  verify/smoke.sh                        # ONE script, run against every installed package
cmake/Packaging.cmake                    # CPack config, included from CMakeLists.txt
```

### Triples

`linux-amd64`, `linux-arm64`, `darwin-arm64`, `darwin-amd64`. Artifacts are named
`llvm-dsdl-<version>-<triple>.tar.gz`.

### Runners

| Cell | Runner | Notes |
|---|---|---|
| linux-amd64 | `ubuntu-22.04` | container: release image, not the CI toolshed image |
| linux-arm64 | `ubuntu-22.04-arm` | free for public repos; this repo is public |
| darwin-arm64 | `macos-15` | native arm64 |
| darwin-amd64 | `macos-13` **if it still exists** | see risk below |

**Risk — Intel macOS.** GitHub has been retiring Intel macOS runners. If no x86_64 macOS runner
is available at implementation time, there are two fallbacks, in preference order:

1. **Build `-arch x86_64` on the arm64 runner and verify under Rosetta 2.** Apple clang
   cross-compiles to x86_64 natively (no zig needed), and an arm64 macOS runner can *execute*
   x86_64 binaries under Rosetta — so the §6 smoke test still runs for real, which is the part
   that matters. Two things need a spike: whether Rosetta 2 is actually present on GitHub's
   arm64 images, and whether an x86_64 `llvm@22` bottle can be fetched and linked against
   (Homebrew bottles are per-arch, not universal, so this means pulling the x86_64 bottle
   manually rather than via `brew install`).
2. **Declare Intel macOS unsupported** and make the brew axis (`arm64_sequoia`,
   `arm64_sonoma`) instead of (arm64, x86_64). Still two bottle targets, still a 2×2.

Confirming runner availability is the first Phase-1 task because it changes the matrix
definition.

**Fallback for Linux arm64** if native runners are ever unavailable: `docker/setup-qemu-action`
plus a `linux/arm64` container. Drop-in, since the Linux build already runs in a container;
roughly 5–10× slower, which is tolerable for a tag-triggered pipeline.

---

## 3. apt

### Layout

```
/usr/bin/dsdlc                          → symlink to ../lib/llvm-dsdl/bin/dsdlc
/usr/bin/dsdl-opt, /usr/bin/dsdld       → likewise
/usr/lib/llvm-dsdl/bin/                 RPATH=$ORIGIN/../lib
/usr/lib/llvm-dsdl/lib/libLLVM.so.22.1  vendored
/usr/share/llvm-dsdl/runtime/           dsdl_runtime.h + per-language runtime sources
/usr/share/llvm-dsdl/llvm-dsdl-sbom.cdx.json
/usr/share/doc/llvm-dsdl/copyright      MIT + Apache-2.0-WITH-LLVM-exception
```

`llvm-dsdl-dev` adds `/usr/include/llvmdsdl/**` and `/usr/lib/*.a`, and `Depends: llvm-dsdl (= ${binary:Version})`.

### Generation

Use **CPack's DEB generator** driven by the existing install components — packaging metadata
lives beside the build, and `CPACK_DEB_COMPONENT_INSTALL` maps `bin`/`dev` onto the two
packages directly.

Turn `CPACK_DEBIAN_PACKAGE_SHLIBDEPS` **off**: it would try to resolve the vendored `libLLVM`
against the dpkg database and either fail or generate a wrong dependency. Write `Depends`
explicitly, and derive the correct list mechanically (`objdump -p` over the staged binaries,
minus anything in the private libdir, mapped through `dpkg -S`) rather than guessing. A wrong
`Depends` is precisely what the clean-container verify job catches, so this needs to be correct,
not clever.

### Publishing

An apt repo is a static file tree, so host it on GitHub Pages from a `gh-pages` branch:

```
dists/stable/{InRelease,Release,Release.gpg}
dists/stable/main/binary-{amd64,arm64}/Packages{,.gz}
pool/main/l/llvm-dsdl/*.deb
```

Generate with `apt-ftparchive`, sign `InRelease` with a release GPG key held in repository
secrets. **Sign from the start** — the alternative is teaching users `[trusted=yes]`, which is a
bad habit to hand out. Install instructions become the standard
`signed-by=/usr/share/keyrings/llvm-dsdl.gpg` three-liner.

Publishing must be **additive**: download the existing `Packages` index, add the new pool
entries, regenerate. Never regenerate the repo from only the current release's artifacts, or
every publish silently deletes every prior version.

---

## 4. brew

Tap repo: `OpenCyphal-Garage/homebrew-llvm-dsdl` (`brew install opencyphal-garage/llvm-dsdl/llvm-dsdl`).
Requires a PAT with write access to the tap repo, stored as a secret in this repo.

The formula **builds from source** (`depends_on "llvm@22"`, `depends_on "cmake" => :build`,
`ninja`, `python@3.12`) — that keeps the tap honest and gives an escape hatch on unbottled
platforms. CI additionally produces **bottles** so the common path is a binary download:

1. `brew install --build-bottle --formula ./llvm-dsdl.rb`
2. `brew bottle --json --root-url=<release asset URL>`
3. Attach the `.bottle.tar.gz` to the GitHub release; `brew bottle --merge --write` updates the
   `bottle do` block; commit to the tap.

**Bottles are keyed by macOS version, not just arch** — a bottle built on macos-15 is
`arm64_sequoia` and will not be used by a Sonoma user, who silently builds from source instead
(slow, but correct). The real brew axis is therefore (macOS version × arch); the pilot should
build the newest two the runners offer and let everything else compile.

Also run `brew style` and `brew audit --strict --online` in the package job — cheap, and catches
formula rot before the tap does.

---

## 5. Later formats (designed for, not built in the pilot)

**snap.** Natural fit: snaps bundle everything, so the vendoring question disappears. `base:
core24`, `confinement: strict`, plugs `home` + `removable-media` (dsdlc reads DSDL trees and
writes generated output; `dsdld` speaks LSP over stdio, which needs nothing). Build with
`snapcore/action-build`, publish with `snapcraft/action-publish` and a
`SNAPCRAFT_STORE_CREDENTIALS` secret; arm64 via the arm runner or Launchpad remote-build.

**Windows.** The honest state of "what-the-fuck-ever-windows-is-doing-these-days" is: **WinGet**
is the answer, and it accepts a plain zip with `NestedInstallerType: portable` — no MSI needed.
**Scoop** is nearly free on top of the same zip (a JSON manifest). MSI (WiX v5) only if someone
with enterprise deployment asks.

The hard part is not the packaging, it is the *build*: there is no prebuilt LLVM 22 **with
MLIR** for Windows. Chocolatey's `llvm` has no MLIR; `vcpkg install llvm[mlir]` builds from
source and takes hours. **Windows is therefore blocked on the cached-toolchain pipeline in §1**
— it is that pipeline's most demanding consumer, and the reason the pipeline should exist
whether or not we ever cross-compile. Sequence: zip + Scoop → WinGet portable → MSI if demanded.

**RPM.** CPack's RPM generator plus a Fedora COPR project is a small increment once the
component split and vendoring approach are settled — probably the cheapest format to add after
the pilot.

---

## 6. Verification — the part that makes this real

This repository's own architectural review is scathing about assurance metrics that measure
*presence* rather than *behavior* (G4: "a backend scores 100 by mentioning the shared helpers").
Packaging has exactly the same failure mode: a green pipeline that only proves a `.deb` *was
produced*. So the pilot's centerpiece is one script run against every installed package on a
pristine machine:

**`packaging/verify/smoke.sh`** — run inside a clean `ubuntu:22.04` container (`apt install
./llvm-dsdl_*.deb`) and on a clean macOS runner (`brew install`):

1. `dsdlc --version` matches the released version exactly.
2. `dsdld --version` starts and responds to an LSP `initialize` over stdio.
3. **Generate C from a real DSDL type and compile the result with a stock `cc`.** This is the
   test that catches §0 — it fails loudly on any packaged build whose runtime-source resolution
   is broken.
4. Same for at least one non-C backend (Go is cheapest — `go build` the generated package).
5. **Re-run the determinism corpus hash** with
   [tools/determinism/cross_stdlib_corpus_hash.py](../../tools/determinism/cross_stdlib_corpus_hash.py)
   and assert it equals the hash the build job recorded. This connects the shipped artifact to
   the existing cross-stdlib determinism gate: the binary a user installs demonstrably generates
   the same bytes as the binary CI tested.
6. `ldd` / `otool -L` shows no reference to any path outside the package and the declared system
   dependencies — a direct check that vendoring and RPATH rewriting worked.

Steps 3 and 5 are the ones with teeth. Step 5 in particular is a genuinely strong claim that
most projects cannot make, and this repo already has the tooling for it.

**`packaging-smoke.yml`** runs steps 1–4 for linux-amd64 on every PR that touches `packaging/`,
`cmake/Packaging.cmake`, or the release workflow — so control-file rot is caught at review time
rather than at 2am on a tag.

---

## 7. Provenance and signing

- `actions/attest-build-provenance` on every dist artifact and every package — SLSA-style
  provenance, verifiable with `gh attestation verify`. Cheap, and it complements the existing
  CycloneDX SBOM: the SBOM says what is inside, the attestation says where it came from.
- The SBOM is already generated as part of `ALL` and installed with the `bin` component, so it
  ships automatically. Add an assertion in `verify` that the installed SBOM's `TOOL_VERSION`
  matches the release tag.
- apt: GPG-signed `InRelease` (§3).
- macOS: bottles installed through brew do not need notarization (brew clears quarantine), and
  the bundle target already ad-hoc-codesigns. **Do not ship standalone macOS `.tar.gz`/`.dmg`
  downloads in the pilot** — those *would* need a Developer ID and `notarytool`, which is a
  separate chunk of secrets and process. brew only.

---

## 8. Sequencing

**Phase 0 — prerequisites (blocking; no packaging work is meaningful before these).**
1. Fix runtime-source resolution — embed via generated `.inc` (§0). Add a test that runs `dsdlc`
   from a directory containing no source tree and compiles the generated C.
2. Install runtime sources under `${CMAKE_INSTALL_DATADIR}/llvm-dsdl/runtime/`.
3. Extend `bundle-tools-self-contained` to cover `dsdld` and emit `bin/` + `lib/`.
4. Add a `release` configure preset (RelWithDebInfo, install prefix, bundle on).
5. Add the tag ↔ `VERSION` consistency check.
6. `cmake/Packaging.cmake` with CPack component config.

**Phase 1 — the 2×2 pilot.**
7. Spike: confirm runner availability (Intel macOS, `ubuntu-22.04-arm`) and which Ubuntu release
   `apt.llvm.org` still ships LLVM 22 for. **This determines the matrix; do it first.**
8. `build-dist` composite action + the four-cell build matrix → dist tarballs.
9. `package` jobs: 2 `.deb` (amd64/arm64), 2 bottles.
10. `packaging/verify/smoke.sh` + the four verify jobs. **Do not skip to publishing.**
11. Draft GitHub release with all artifacts + checksums + attestations. Dry-run mode by default.

**Phase 1b — cached LLVM toolchain (parallel track, not blocking the pilot).**
12. `llvm-toolchain.yml`: build LLVM/MLIR 22 at a pinned revision per target, publish as a GHCR
    image (Linux) / release artifact (macOS), keyed by `llvm-rev + triple + stdlib`. Runs on pin
    change, not per release.
13. A restore step for the `build` job, behind a flag so the pilot can switch over one cell at a
    time.

**Phase 2 — real distribution.**
14. Swap `build` onto the cached toolchain; lower the glibc floor deliberately and record it.
15. apt repo on `gh-pages` with GPG-signed `InRelease`, additive publishing.
16. Homebrew tap repo + automated formula/bottle commit.
17. `packaging-smoke.yml` on PRs.
18. `llvm@22` availability canary — becomes moot once 1b lands and the formula builds against
    our own toolchain, but needed until then.
19. Install instructions in [README.md](../../README.md).

**Phase 3 — breadth.** snap; Windows (requires 1b, then zip + Scoop, then WinGet); RPM/COPR;
`llvm-dsdl-dev` promotion if anyone actually links the libraries.

---

## 9. Open decisions

| # | Decision | Default assumed here |
|---|---|---|
| D1 | Which org owns the tap and apt repo — `OpenCyphal-Garage` or `thirtytwobits`? | `OpenCyphal-Garage` (matches `upstream`) |
| D2 | Is Intel macOS supported? | Yes via `-arch x86_64` + Rosetta verification if the spike holds; otherwise dropped for (sequoia, sonoma) on arm64 |
| D3 | Vendor `libLLVM` in the `.deb`, or require `apt.llvm.org`? | Vendor |
| D4 | Does `llvm-dsdl-dev` ship in the pilot? | Built and verified, published to apt only |
| D5 | Release trigger | tag `v*` push, plus `workflow_dispatch` with a dry-run input |
| D6 | Build our own LLVM 22 (Phase 1b) or stay on distro/brew builds indefinitely? | Build our own — it is the only path to Windows and to a chosen glibc floor |
| D7 | What glibc floor do we commit to once 1b lands? | Undecided; needs a supported-distro policy, not a technical answer |

---

## 10. Why not cross-compile (zig cc, clang sysroots, etc.)

Cross-compiling everything from one amd64 Linux runner is the obvious way to avoid a native
runner per cell, and `zig cc` is the obvious tool. It does not work for this project, and the
reason is worth recording so it does not get re-litigated.

**Cross-compiling this project is not a compiler problem, it is a dependency problem.** Zig
supplies a clang that emits aarch64 or Mach-O code, libc headers, and glibc version stubs. It
does not supply `libLLVM.so.22.1` for arm64 or the MLIR static archives we link. Four concrete
blockers:

1. **No target-built LLVM/MLIR.** You would have to cross-build LLVM 22 per target first — which
   is the Phase 1b pipeline, at which point you may as well build natively — or scrape prebuilts
   out of apt.llvm.org arm64 debs and Homebrew bottles into ad-hoc sysroots.
2. **C++ ABI mismatch if you scrape.** apt.llvm.org builds against **libstdc++**; Homebrew
   against Apple's **libc++**. Zig bundles its own libc++ and ships no libstdc++ at all. MLIR
   exports a great deal of templated C++; mixing libc++ versions across that boundary yields
   undefined symbols from inline-namespace differences, or — worse — a binary that links and is
   quietly ODR-broken. A project that pins an LLVM *major* because EmitC output drifts across
   them should not accept a sharper version hazard one layer down.
3. **TableGen must run on the build host.** [include/llvmdsdl/IR/CMakeLists.txt:9](../../include/llvmdsdl/IR/CMakeLists.txt)
   invokes `mlir_tablegen()` eight times; `MLIR_TABLEGEN_EXE` comes from the MLIRConfig package.
   Point CMake at a target-arch MLIR install and you get a target-arch `mlir-tblgen` that cannot
   execute on the builder. Solvable — provision host *and* target MLIR, override the exe — but
   it is another cross-build-only invariant to keep correct.
4. **Verification requires execution anyway.** §6 is the point of this whole plan, and every
   step of it runs the binary. A cross-built artifact cannot be verified without emulation, so
   cross-compiling removes the *build* runner and leaves the *test* runner — the one that
   matters. On a public repository, `ubuntu-22.04-arm` and the macOS runners are free; the cost
   of native runners is YAML, not money.

**What the intuition does get right:** zig's genuine strength is decoupling the glibc floor from
the build host, which *is* a real problem here. But with vendoring, the floor is set by the
prebuilt `libLLVM` we ship, not by our own objects — so zig only half-solves it, and closing the
other half means building LLVM ourselves. That is Phase 1b, and it addresses the same underlying
cause more completely. The one-line summary: **the answer to "we need too many native runners"
is not a cross-compiler, it is owning our LLVM toolchain.**

Worth revisiting if: Phase 1b lands and produces target-built LLVM/MLIR *and* someone wants to
collapse the build matrix further. At that point blockers 1 and 2 are gone, 3 is mechanical, and
only 4 remains — which caps the win at "fewer build runners, same number of test runners."
