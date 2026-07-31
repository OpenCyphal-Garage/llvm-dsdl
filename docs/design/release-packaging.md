# Release packaging and distribution

How `dsdlc` / `dsdl-opt` / `dsdld` get from a git tag to a user's machine. This documents the
pipeline **as it is built today**; distribution channels that do not exist yet (Homebrew, an apt
repository, other platforms) are backlog and live in
[P3_release_packaging.md](../plans/P3_release_packaging.md).

**Today:** a tag pushes packages to a draft GitHub release — `.deb` for Ubuntu on amd64 and
arm64, and a self-contained tarball for macOS on Apple silicon. Each carries the LLVM 22 runtime
it needs, so nothing has to be added to the user's machine first.

---

## 1. What ships

| Component | Contents | Debian package | macOS |
|---|---|---|---|
| `bin` | `dsdlc`, `dsdl-opt`, `dsdld`, vendored LLVM runtime, SBOM, licences | `llvm-dsdl` | `bin/` + `lib/` in the tarball |
| `dev` | 8 static libraries, `include/llvmdsdl`, generated dialect headers, `Version.h` | `llvm-dsdl-dev` | included in the tarball |

`llvm-dsdl-dev` pins `llvm-dsdl (= <version>)`: the static libraries and headers are only
coherent against the exact build they shipped with, and dpkg refuses the pairing otherwise.

The generated output is self-sufficient. Each backend writes its own runtime support scaffold
into the output tree, and those scaffolds are compiled into the binary
([tools/runtime/generate_embedded_runtime.py](https://github.com/thirtytwobits/llvm-dsdl/blob/main/tools/runtime/generate_embedded_runtime.py)),
so a packaged `dsdlc` emits code that compiles on a machine which has never seen this source
tree.

---

## 2. Build environments

### Linux: Ubuntu 22.04 (jammy)

Built in [packaging/docker/Dockerfile.ubuntu-release](https://github.com/thirtytwobits/llvm-dsdl/blob/main/packaging/docker/Dockerfile.ubuntu-release).
22.04 is the oldest release apt.llvm.org publishes LLVM 22 for, which puts the glibc floor at
2.35 and lets one package install on 22.04, 24.04 and 26.04 alike. The image adds two third-party
apt sources because Ubuntu 22.04 ships neither: apt.llvm.org for LLVM/MLIR 22, and apt.kitware.com
for CMake ≥ 3.24 (jammy has 3.22).

**The CI toolshed image is not reused for releases.** It is Ubuntu 26.04, and a `.deb` built
there links a glibc that refuses to install on anything older — which would silently cost every
24.04 user, the largest group.

Reaching back to 22.04 cost one code change. `std::flat_set` reached libstdc++ only in GCC 15
(Ubuntu 26.04), so [include/llvmdsdl/Support/FlatSet.h](https://github.com/thirtytwobits/llvm-dsdl/blob/main/include/llvmdsdl/Support/FlatSet.h)
provides it instead, on every platform. Building against libc++, which does have `flat_set`, is
the obvious-looking alternative and is closed: the distro `libLLVM` links libstdc++, and putting
two C++ runtimes either side of MLIR's `std::string` boundary is an ABI hazard that yields
undefined symbols or a quietly ODR-broken binary.

### macOS: `macos-15`, Apple silicon

Homebrew LLVM at build time, vendored into the artifact. Intel macOS is not built; see the
[backlog](../plans/P3_release_packaging.md).

---

## 3. Vendoring the LLVM runtime

Neither Ubuntu nor a stock macOS carries LLVM 22, and a downloaded file has no dependency
resolver behind it. Without vendoring, a package installs cleanly and dies on first run with a
missing shared object, with nothing to fall back on.

`LLVMDSDL_VENDOR_LLVM` (default off — an ordinary `cmake --install` should not copy ~150 MB into
the install tree) means "self-contained" on both platforms, by platform-appropriate means:

- **Linux** — `libLLVM.so.22.1` installs to `/usr/lib/<multiarch>/llvm-dsdl/`, with CMake's
  `INSTALL_RPATH` putting `$ORIGIN/../<libdir>/llvm-dsdl` on the three tools. A private directory,
  not `${CMAKE_INSTALL_LIBDIR}`: this is our copy of someone else's library and must satisfy
  nothing but our own tools.
- **macOS** — [cmake/BundleSelfContainedTools.cmake](https://github.com/thirtytwobits/llvm-dsdl/blob/main/cmake/BundleSelfContainedTools.cmake)
  copies the tools and their dylibs into `bin/` + `lib/`, rewrites Mach-O install names to
  `@executable_path/../lib` and `@loader_path`, and ad-hoc codesigns. When vendoring is on, the
  install rules install that bundle rather than the raw targets.

LLVM is Apache-2.0-WITH-LLVM-exception, so redistribution is fine with attribution;
[packaging/deb/copyright](https://github.com/thirtytwobits/llvm-dsdl/blob/main/packaging/deb/copyright) carries the stanza covering the shipped
library.

### Dependencies are derived, never written by hand

`CPACK_DEBIAN_PACKAGE_SHLIBDEPS` is off. `dpkg-shlibdeps` resolves every linked library against
the dpkg database, and the vendored `libLLVM` belongs to no installed package, so it either fails
or invents a wrong dependency.

The list is instead derived from the built binaries at package time by
[packaging/deb/derive_depends.py](https://github.com/thirtytwobits/llvm-dsdl/blob/main/packaging/deb/derive_depends.py): walk what the tools
*and the vendored library* link, drop what ships inside the package, map the rest through
`dpkg -S`. It runs from a CPack project-config script, because the binaries do not exist at
configure time.

The vendored library's own dependencies are the ones that bite. The current list is `libbsd0`,
`libc6`, `libedit2`, `libffi8`, `libicu70`, `libmd0`, `libstdc++6`, `libxml2`, `libz3-4`,
`libzstd1` — all from the stock Ubuntu archive, and all but two contributed by `libLLVM` rather
than by our own code.

---

## 4. Debian packaging

`.deb` generation is CPack's DEB generator driven by the existing install components
([cmake/Packaging.cmake](https://github.com/thirtytwobits/llvm-dsdl/blob/main/cmake/Packaging.cmake)). `CPACK_DEB_COMPONENT_INSTALL` maps
`bin`/`dev` onto the two packages.

Compression is **xz**, not CPack's default gzip: the package is dominated by a vendored library
that compresses well, and this is a file people download directly — 74 MB → 46 MB measured.

### Policy metadata

Debian keys these off the *binary package* name, so each package needs its own copy.
`CMAKE_INSTALL_DOCDIR` only covers `llvm-dsdl`; the `llvm-dsdl-dev` destination is spelled out.

- **[packaging/deb/copyright](https://github.com/thirtytwobits/llvm-dsdl/blob/main/packaging/deb/copyright)** — DEP-5, covering the project's MIT
  terms and the Apache-2.0-with-LLVM-exception of the shipped `libLLVM`.
- **[packaging/deb/changelog](https://github.com/thirtytwobits/llvm-dsdl/blob/main/packaging/deb/changelog)** — CPack has no changelog support, so
  it is gzipped (`-n`, for a byte-identical result across builds) and installed by hand. Its top
  entry restates the version; a package whose changelog disagrees with its control file is
  malformed, so configure fails on the mismatch.
- **[packaging/deb/lintian-overrides/llvm-dsdl](https://github.com/thirtytwobits/llvm-dsdl/blob/main/packaging/deb/lintian-overrides/llvm-dsdl)** —
  the tools ship unstripped, neither stripped nor split into `-dbgsym`. For a compiler of an
  avionics-adjacent wire format, the symbols needed to read a backtrace should already be on the
  machine that produced the core. The override carries no file hint: lintian releases disagree on
  how hints are rendered, and a hint that does not match the running version is silently inert.

Both packages are lintian-clean.

### Man pages

Generated from each tool's own `--help` by
[tools/man/generate_manpage.py](https://github.com/thirtytwobits/llvm-dsdl/blob/main/tools/man/generate_manpage.py), so the two cannot drift.
Two help dialects are parsed: the `NAME`/`SYNOPSIS` sections `dsdlc` and `dsdld` print, and LLVM's
`cl::opt` format `dsdl-opt` inherits. Anything not structurally mappable is emitted verbatim in a
preformatted block rather than guessed at. The page date comes from the changelog's release
trailer, not the clock, so rebuilds are byte-identical.

This runs each tool to document it, so it needs host-executable binaries — fine natively, but a
cross-build would have to generate the pages host-side.

---

## 5. macOS packaging

The tarball is the bundle described in §3: `bin/`, `lib/`, licences, and a manifest. It is
relocatable — extract anywhere, add `bin/` to `PATH`.

### Gatekeeper

The binaries are ad-hoc signed rather than notarized, and `spctl -a -t exec` rejects them. That
matters only for files carrying `com.apple.quarantine`, which depends entirely on how the archive
is opened:

| Path | Quarantine on extracted files | Result |
|---|:--:|---|
| `curl` + `tar -xzf` | no | runs |
| Browser download + `tar -xzf` | no | runs |
| Browser download + Finder double-click | yes | **blocked** |
| `brew install` | no — brew strips it | runs |

`tar` does not propagate quarantine; Archive Utility does. So the tarball needs no Developer ID
and no notarization, **provided the install instructions say `tar -xzf`** — which the release
notes do. Notarization is the usual answer for shipping macOS binaries and would cost a paid
certificate, `notarytool`, and secrets; it buys only the Finder row.

---

## 6. The release workflow

[.github/workflows/release.yml](https://github.com/thirtytwobits/llvm-dsdl/blob/main/.github/workflows/release.yml), triggered by a `v*` tag or
by `workflow_dispatch` (which defaults to a dry run that builds and verifies without publishing).

```
tag v* ──▶ stage ──▶ build   (ubuntu-22.04-arm, ubuntu-22.04) ──▶ publish
             │       macos   (macos-15)                            │
        version/tag      each: build → package → verify        draft release,
        consistency                                            checksums, attestation
```

- **stage** — derives the version from `VERSION`, fails if a tag disagrees with it, and checks the
  changelog's top entry matches. A tag that disagreed would ship binaries misreporting themselves.
- **build** / **macos** — each runs on a native runner for its architecture; CI never emulates.
  Emulation is only how a developer on one architecture cross-checks the other locally, which is
  what `smoke.py --platform` exists for. The legs are independent (`fail-fast: false`): one
  architecture's package is installable without the other.
- **publish** — downloads each artifact into its own subdirectory (every build job writes a
  `SHA256SUMS`, so merging would leave one overwriting the others), rebuilds one checksum file
  from what is actually attached, attests provenance, and creates a **draft** release.

Reference point for capacity planning: the arm64 Linux leg, including building the release
container image from scratch, completed in 5m30s on GitHub's arm64 runner against a 90-minute
timeout.

---

## 7. Verification

A package that only works where it was built is the failure mode all of this is built to catch,
and with no repository behind a downloaded file, nothing else would notice.

| Check | What it proves | Where |
|---|---|---|
| [smoke.py](https://github.com/thirtytwobits/llvm-dsdl/blob/main/packaging/verify/smoke.py) | The `.deb` installs on **pristine** Ubuntu with no apt.llvm.org, all three tools run, `libLLVM` resolves through RPATH, generated C compiles with stock `cc`, a non-C backend emits | release workflow |
| [smoke_macos.py](https://github.com/thirtytwobits/llvm-dsdl/blob/main/packaging/verify/smoke_macos.py) | Every non-system reference in every binary and dylib resolves **inside the bundle**, tools run, generated C compiles | release workflow |
| [check_deb_config.py](https://github.com/thirtytwobits/llvm-dsdl/blob/main/packaging/verify/check_deb_config.py) | Control fields, component split, version pin, lintian verdict — against real packages built in a container | ctest (`llvmdsdl-packaging-deb-config`) |
| [test_check_deb_config.py](https://github.com/thirtytwobits/llvm-dsdl/blob/main/packaging/verify/test_check_deb_config.py) | The verdict logic itself, against recorded output — no Docker needed | ctest (`llvmdsdl-packaging-deb-config-selftest`) |

Two design points worth keeping:

**macOS asserts linkage, not just behaviour.** There is no pristine container to install into, and
the build runner is the worst possible judge — it has Homebrew LLVM at exactly the paths a
non-relocated binary would reference, so a tarball that only works there would pass every
behavioural test.

**The verdict logic is tested without Docker.** The containerised check registers only when a
`docker` binary exists and exits 77 — which `SKIP_RETURN_CODE` turns into a ctest *skip* — when
the daemon is unreachable. The CI Linux lane runs inside a container and has no Docker of its own,
so without this it would either fail to configure or report a green test that never ran. Pass
`--require-docker` to make an unusable daemon a hard failure; the release lane should, since a
silent skip there would let an unverified package through.

---

## 8. Why not cross-compile

Building every target from one Linux runner is the obvious way to avoid a native runner per
architecture, and `zig cc` is the obvious tool. It does not work here, and the reason is worth
recording because the idea will resurface.

**This is a dependency problem, not a compiler problem.** Zig supplies a clang that emits aarch64
or Mach-O code, libc headers, and glibc version stubs. It does not supply `libLLVM` for the target
or the MLIR archives we link.

1. **No target-built LLVM/MLIR.** You would have to cross-build LLVM first — which is the cached
   toolchain in the [backlog](../plans/P3_release_packaging.md), at which point native is simpler
   — or scrape prebuilts into ad-hoc sysroots.
2. **C++ ABI mismatch if you scrape.** apt.llvm.org builds against libstdc++, Homebrew against
   libc++, and zig bundles its own libc++ with no libstdc++ at all. MLIR exports a great deal of
   templated C++; mixing standard libraries across that boundary yields undefined symbols or a
   quietly ODR-broken binary.
3. **TableGen must run on the build host.** [include/llvmdsdl/IR/CMakeLists.txt](https://github.com/thirtytwobits/llvm-dsdl/blob/main/include/llvmdsdl/IR/CMakeLists.txt)
   invokes `mlir_tablegen()` eight times; pointing CMake at a target-arch MLIR yields a
   `mlir-tblgen` that cannot execute on the builder.
4. **Verification requires execution anyway.** Every check in §7 runs the binary. Cross-compiling
   removes the build runner and leaves the test runner — the one that matters. On a public
   repository the native runners are free.

The intuition it comes from is sound: zig's real strength is decoupling the glibc floor from the
build host. But with vendoring, that floor is set by the prebuilt `libLLVM` we ship rather than by
our own objects, so the cure for it is owning our LLVM toolchain.

Worth revisiting if the cached toolchain lands *and* someone wants to collapse the build matrix
further: blockers 1 and 2 disappear, 3 is mechanical, and only 4 remains — capping the win at
"fewer build runners, same number of test runners".
