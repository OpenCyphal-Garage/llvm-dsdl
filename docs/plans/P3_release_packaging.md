# P3 — Release packaging and distribution

How `dsdlc` / `dsdl-opt` / `dsdld` get from a git tag to a user's machine, across package
managers and CPU architectures.

**Pilot scope:** a 2×2 matrix — {apt, brew} × {amd64, arm64}. Snap, Windows, and RPM are
designed for here but built later, and the pilot's job is to prove the *factoring* is right so
those are additive.

**Status:** [cmake/Packaging.cmake](../../cmake/Packaging.cmake) is in place — CPack emits the
distribution tarball and, on Linux, the `llvm-dsdl` / `llvm-dsdl-dev` `.deb` pair. Both packages
build `lintian`-clean, install into a clean container, put all three tools on `PATH`, and carry
man pages; dpkg enforces the `-dev` version pin.
[packaging/verify/check_deb_config.py](../../packaging/verify/check_deb_config.py) reproduces all
of that, and both it and its verdict logic run under ctest (see §6). No workflow, apt repo, or
tap exists.

---

## 0. Build-side prerequisites

Two gaps on the build side have to close before any packaging job can produce something
installable. Neither is large; both are on the critical path.

1. **No tag/VERSION consistency check.** `VERSION` says `0.1.0`; nothing stops a `v0.2.0` tag
   from shipping binaries that self-report `0.1.0`. [cmake/Packaging.cmake](../../cmake/Packaging.cmake)
   already takes the package version from the `VERSION` file, so the check has one value to
   assert the tag against.
2. **The self-contained bundle is flat.** `bundle-tools-self-contained`
   ([cmake/BundleSelfContainedTools.cmake](../../cmake/BundleSelfContainedTools.cmake)) already
   does the hard part — `patchelf` / `install_name_tool` rewrites, `@loader_path`, ad-hoc
   codesign — but emits binaries and libraries into one flat directory, and only covers `dsdlc`
   and `dsdl-opt` (not `dsdld`). Packaging needs a `bin/` + `lib/` split and all three tools.
   This is what the vendored private-libdir layout in §3 waits on; CPack currently packages the
   standard `/usr/bin` + `/usr/lib` layout.

Each backend writes its own runtime support scaffold into the generated output tree, so a
package ships no runtime sources of its own and the generated code is self-sufficient wherever
it lands.

---

## 1. What ships

| Component | Contents | Package |
|---|---|---|
| `bin` | `dsdlc`, `dsdl-opt`, `dsdld`, SBOM, licences | `llvm-dsdl` |
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
- **`build`** — configure with a new `release` preset, build `RelWithDebInfo`, run the
  release-blocking gates, run the bundle target, then `cpack` to emit
  `llvm-dsdl-<version>-<triple>.tar.gz` (plus its SHA-256) and, on Linux, the two `.deb`s.
  **One tarball per platform, consumed by every packaging job for that platform.**
- **`package`** — downloads the dist tarball, emits one package format. Pure metadata + repack;
  no compiler runs. Fast, and trivially parallel across formats.

  `cpack` re-runs the install rules, so it needs the build tree — never a compiler, but the tree.
  Shipping hundreds of megabytes of objects between jobs to preserve a stage boundary is a bad
  trade, so the `.deb` is emitted in `build` alongside the tarball. The goal that matters is
  intact: RPM becomes another generator in
  [cmake/Packaging.cmake](../../cmake/Packaging.cmake), and snap / WinGet / brew become `package`
  jobs consuming the tarball. Neither adds a build job.
- **`verify`** — §6. The centerpiece.
- **`publish`** — gated on all verifies passing; creates the GitHub release, pushes to the apt
  repo and the brew tap, and attaches provenance attestations.

### Files

```
.github/workflows/release.yml            # the pipeline above
.github/workflows/packaging-smoke.yml    # PR-time: build+verify the amd64 .deb only
.github/actions/build-dist/action.yml    # composite: configure → build → gates → bundle → cpack
packaging/
  dist/triples.json                      # single source of truth for the matrix
  deb/                                   # copyright, lintian overrides
  brew/llvm-dsdl.rb.in                   # formula template (version/sha256 substituted)
  verify/smoke.py                        # ONE script, run against every installed package
  verify/check_deb_config.py             # .deb control/lintian check, no LLVM build needed
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
/usr/share/llvm-dsdl/llvm-dsdl-sbom.cdx.json
/usr/share/doc/llvm-dsdl/copyright      MIT + Apache-2.0-WITH-LLVM-exception
```

`llvm-dsdl-dev` adds `/usr/include/llvmdsdl/**` and `/usr/lib/*.a`, and `Depends: llvm-dsdl (= ${binary:Version})`.

### Generation

CPack's DEB generator, driven by the existing install components, in
[cmake/Packaging.cmake](../../cmake/Packaging.cmake). `CPACK_DEB_COMPONENT_INSTALL` maps
`bin`/`dev` onto the two packages, and `llvm-dsdl-dev` pins `llvm-dsdl (= <version>)` so headers
and static libraries can never be paired with a different build.

`CPACK_DEBIAN_PACKAGE_SHLIBDEPS` is **off**: it resolves each linked library against the local
dpkg database, and the vendored `libLLVM` would either make it fail or map to a wrong system
package. `Depends` is therefore explicit, via the `LLVMDSDL_DEB_DEPENDS` cache variable. Its
default (`libc6, libstdc++6`) is a baseline, **not** the answer — the real list is derived on the
target distribution with `objdump -p` over the staged binaries, minus anything in the private
libdir, through `dpkg -S`. A wrong `Depends` is exactly what the clean-container verify job
catches, so it needs to be correct, not clever.

### Policy metadata

Debian keys both files off the *binary package* name, so each needs its own copy —
`CMAKE_INSTALL_DOCDIR` only covers `llvm-dsdl`, and the `llvm-dsdl-dev` destination is spelled
out explicitly in [CMakeLists.txt](../../CMakeLists.txt).

- **`packaging/deb/copyright`** — DEP-5. Carries the project's MIT terms and a staged
  Apache-2.0-with-LLVM-exception paragraph for the `libLLVM` that vendoring will put in the
  package; it needs a `Files:` stanza once that lands.
- **`packaging/deb/changelog`** — CPack has no changelog support, so it is gzipped (`-n`, for a
  byte-identical result across builds) and installed by hand. Its top entry restates the version,
  and a package whose changelog disagrees with its control file is malformed, so configure fails
  on the mismatch rather than letting a release ship with it.

- **`packaging/deb/lintian-overrides/llvm-dsdl`** — the tools ship unstripped, and neither
  stripped nor split into `-dbgsym`. For a compiler of an avionics-adjacent wire format, the
  symbols needed to read a backtrace should already be on the machine that produced it. The
  override records that as a decision where lintian reads it.

**Both packages are lintian-clean.**

### Man pages

Generated from each tool's own `--help` by
[tools/man/generate_manpage.py](../../tools/man/generate_manpage.py), so the two cannot drift —
a newly documented flag reaches the man page at the next build, with no second copy to forget.
Two help dialects are parsed: the `NAME`/`SYNOPSIS` sections that `dsdlc` and `dsdld` print, and
LLVM's `cl::opt` format that `dsdl-opt` inherits. Anything not structurally mappable is emitted
verbatim in a preformatted block rather than guessed at.

The page date comes from the changelog's release trailer, not the clock, so rebuilding the same
source yields byte-identical pages.

Note this runs each tool to document it, so it needs host-executable binaries. Native builds are
fine; a cross-build (§10) would have to generate the pages host-side or ship them prebuilt.

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

**`packaging/verify/smoke.py`** — run inside a clean `ubuntu:22.04` container (`apt install
./llvm-dsdl_*.deb`) and on a clean macOS runner (`brew install`):

1. `dsdlc --version` matches the released version exactly.
2. `dsdld --version` starts and responds to an LSP `initialize` over stdio.
3. **Generate C from a real DSDL type and compile the result with a stock `cc`.** Exercises the
   whole emit path from an installed binary — codegen, the runtime scaffold it writes beside the
   generated code, and whether the result is actually compilable on a machine that has never
   seen the source tree.
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

### What runs under ctest today

| Test | Needs | Labels |
|---|---|---|
| `llvmdsdl-packaging-deb-config-selftest` | nothing beyond python3 | `integration;packaging` |
| `llvmdsdl-packaging-deb-config` | Docker | `integration;packaging;slow` |

The split is deliberate. The checker's verdict logic — control-field assertions, the shared-synopsis
trap, the version-pin check — is exercised against recorded container output by the self-test, so it
stays covered on hosts that cannot build a `.deb` at all. That is exactly where a silent regression
in those assertions would otherwise survive.

The containerised check registers only when a `docker` binary exists, and exits `77` when the daemon
is unreachable, which `SKIP_RETURN_CODE` turns into a ctest **skip** rather than a pass. Both paths
matter: the CI Linux lane runs inside a container and has no Docker of its own, so without this it
would either fail to configure or report a green test that never ran. Pass `--require-docker` to turn
an unusable daemon into a hard failure — the release lane should, since a skip there would let an
unverified package through.

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

**Phase 0 — build-side prerequisites (blocking; no packaging work is meaningful before these).**
1. Extend `bundle-tools-self-contained` to cover `dsdld` and emit `bin/` + `lib/`, then move
   CPack onto the vendored private-libdir layout of §3.
2. Add a `release` configure preset (RelWithDebInfo, install prefix, bundle on).
3. Add the tag ↔ `VERSION` consistency check.

**Phase 1 — the 2×2 pilot.**
4. Spike: confirm runner availability (Intel macOS, `ubuntu-22.04-arm`) and which Ubuntu release
   `apt.llvm.org` still ships LLVM 22 for. **This determines the matrix; do it first.**
5. Derive the real `Depends` on a Linux host with a real build (`objdump -p` over the staged
   binaries, through `dpkg -S`) and set `LLVMDSDL_DEB_DEPENDS` from it — the configured default
   is a baseline, not an answer.
6. `build-dist` composite action + the four-cell build matrix → dist tarballs and `.deb`s.
7. `package` jobs: 2 bottles.
8. `packaging/verify/smoke.py` + the four verify jobs. **Do not skip to publishing.**
9. Draft GitHub release with all artifacts + checksums + attestations. Dry-run mode by default.

**Phase 1b — cached LLVM toolchain (parallel track, not blocking the pilot).**
10. `llvm-toolchain.yml`: build LLVM/MLIR 22 at a pinned revision per target, publish as a GHCR
    image (Linux) / release artifact (macOS), keyed by `llvm-rev + triple + stdlib`. Runs on pin
    change, not per release.
11. A restore step for the `build` job, behind a flag so the pilot can switch over one cell at a
    time.

**Phase 2 — real distribution.**
12. Swap `build` onto the cached toolchain; lower the glibc floor deliberately and record it.
13. apt repo on `gh-pages` with GPG-signed `InRelease`, additive publishing.
14. Homebrew tap repo + automated formula/bottle commit.
15. `packaging-smoke.yml` on PRs.
16. `llvm@22` availability canary — becomes moot once 1b lands and the formula builds against
    our own toolchain, but needed until then.
17. Install instructions in [README.md](../../README.md).

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
