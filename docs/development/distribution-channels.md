# Distribution channels (backlog)

Distribution channels that do not exist yet, and the decisions still open about them.

The pipeline that **does** ship today — what the `.deb` and macOS tarball contain, the build
environments, LLVM vendoring, Debian and macOS packaging, the release workflow, verification,
and why cross-compiling is closed — is documented as built in
[docs/development/release-packaging.md](release-packaging.md). A bare section reference
below (§2, §3, §5, §6, §7, §8) points *there*; a reference to a section of this document says
"above".

Everything here depends on §1 below. The toolchain is ordered first because the Homebrew,
Windows, and 32-bit ARM channels are not independent items — each is a consequence of owning
the LLVM build.

---

## 1. The self-built LLVM toolchain

Four constraints trace back to *whose* LLVM 22 we build against:

- The glibc floor is apt.llvm.org's choice of oldest suite, not ours.
- The macOS build follows Homebrew's lifecycle.
- Windows has no prebuilt LLVM with MLIR at all.
- The vendored `libLLVM` and its dependency tail exist only because we consume someone else's
  build of it.

Building LLVM/MLIR 22 ourselves once per target, caching it as a GHCR image or release artifact
keyed by `llvm-rev + triple + stdlib`, and restoring it in the build job dissolves all four. It
runs when the pin changes, not per release.

This is not a prerequisite for anything shipping today. The four-stage workflow factoring (§6)
exists so the toolchain source is an implementation detail of the build job: it can swap in
later without touching the package, verify, or publish stages.

### Static linking is not reachable against a distribution's LLVM

Measured against the Homebrew `llvm` 22 keg:

| Fact | Location |
|---|---|
| `add_library(LLVM SHARED IMPORTED)` | `lib/cmake/llvm/LLVMExports.cmake:1742` |
| 450 references to that target | `lib/cmake/mlir/MLIRTargets.cmake` |
| `set(LLVM_LINK_LLVM_DYLIB ON)` | `lib/cmake/llvm/LLVMConfig.cmake:30` |
| `set(LLVM_WITH_Z3 1)` | `lib/cmake/llvm/LLVMConfig.cmake:316` |
| `llvm-config --link-static --system-libs` → `-lm /opt/homebrew/lib/libz3.dylib -lz -lzstd -lxml2` | — |

Component archives are present; a monolithic `libLLVM.a` is not. `-DLLVM_LINK_LLVM_DYLIB=OFF`
does not dislodge the dylib, because MLIR's exported targets name a target that is *declared*
shared. Even the fully static path hard-codes `libz3.dylib` by absolute path.

The same reasoning closes the mirror-image option of building against libc++ on Linux
(§2 of release-packaging.md): a distribution's `libLLVM` fixes the standard library, and we
cannot cross that boundary without owning the build.

### The build is smaller than it appears

`dsdlc` lowers DSDL to EmitC and emits source text. It generates no machine code, and the
project references no target backend — no `LLVM_TARGETS_TO_BUILD`, `TargetMachine`, or
`InitializeNative*`. The toolchain builds with `LLVM_TARGETS_TO_BUILD=""`, which removes the
bulk of an LLVM build.

The shipped dependency tail (§3) is likewise optional feature selection rather than anything we
require. `LLVM_ENABLE_Z3_SOLVER`, `LLVM_ENABLE_TERMINFO`, `LLVM_ENABLE_LIBEDIT`,
`LLVM_ENABLE_LIBXML2`, `LLVM_ENABLE_ZLIB`, `LLVM_ENABLE_ZSTD`, `LLVM_ENABLE_FFI` and
`LLVM_ENABLE_PLUGINS` set to `OFF` retire `libbsd0`, `libedit2`, `libffi8`, `libicu70`,
`libmd0`, `libxml2`, `libz3-4` and `libzstd1` — the whole derived `Depends` list but for
`libc6` and `libstdc++6`, which static linking (§2 above) retires instead.

### What it does not fix

**Gatekeeper.** A quarantined, ad-hoc signed executable linking nothing but `libSystem` is
still killed on execution, and `spctl -a -t exec` still rejects it. Gatekeeper judges the
signature, not the dependency graph. Eliminating dylibs reduces the number of Mach-O files
needing a real signature from six to one; it does not remove the requirement. The remedies are
notarisation, or a channel that does not set the quarantine attribute (§3 above).

**Plugin loading.** [lib/LSP/Lint.cpp](https://github.com/OpenCyphal-Garage/llvm-dsdl/blob/main/lib/LSP/Lint.cpp) loads lint rules with
`dlopen`/`dlsym`. musl's static libc provides a `dlopen` that always fails, and static glibc's
is unsupported. A fully static `dsdld` cannot load lint plugins. See D6 below.

## 2. Cross-compilation targets

Two of the four blockers recorded in §8 dissolve with §1 above: there is a target-built
LLVM/MLIR, and we own the C++ ABI on both sides. TableGen (blocker 3) is mechanical — build a
host-native `mlir-tblgen` and point the cross build at it. **Verification still requires
execution** (blocker 4), and that is the binding constraint on the matrix.

| Target | Fully static | Verification |
|---|:--:|---|
| `x86_64-linux-musl` | ✅ | native |
| `aarch64-linux-musl` | ✅ | native arm64 runner |
| `armv7-linux-musleabihf` | ✅ | qemu-user |
| `riscv64-linux-musl` | ✅ | qemu-user |
| `x86_64-pc-windows-gnu` | ✅ except OS DLLs | `windows-latest` runner; wine as a pre-check |
| `aarch64-pc-windows-gnu` | ✅ | unresolved — see D7 |
| `*-windows-msvc` | ✅ | not pursued; the MSVC SDK carries the same licensing problem as the macOS SDK |
| `*-apple-darwin` | ❌ | not cross-compiled — see below |

A static musl binary has no glibc floor, which retires D4 rather than deciding it. One `.deb`
per architecture then installs on Debian bullseye through trixie, Ubuntu 20.04 through 26.04,
and Raspberry Pi OS alike, and `derive_depends.py` (§3) has nothing left to derive.

💡 musl's allocator is markedly slower than glibc's under allocation-heavy C++, which describes
MLIR exactly. Measure `dsdlc` against the corpus before committing, and link mimalloc or
jemalloc if the difference is material.

### macOS is not a cross-compilation target

Apple does not support statically linking `libSystem`, so "fully static" is unreachable on
Darwin by construction; the ceiling is static except `libSystem`. Cross-compiling additionally
requires Apple's SDK, which is licensed for use on Apple hardware. Signing and notarisation can
be performed from Linux, so those are not the obstacle. The native runner stays.

### Verification splits in two

The checks in §7 conflate two questions that scale differently:

- **Is the generated output correct** — that the emitted C compiles, that a non-C backend
  emits. The output is portable text, so this is architecture-independent and runs once, on the
  host.
- **Does the binary run on the target** — this genuinely needs the target.

A fully static CLI binary is the ideal `qemu-user` case: no dynamic loader, no sysroot. Running
corpus generation under emulation and comparing the corpus hash against the host's — the gate
that already exists for cross-stdlib determinism — turns "it launched" into "it produced
exactly the right bytes" for every target, on one machine.

## 3. Homebrew

A tap (`OpenCyphal-Garage/homebrew-llvm-dsdl`) needing a PAT held as a secret here, carrying a
**binary formula**: `url` points at the release tarball from §5, and `install` copies `bin/`
into the prefix. With §1 above there is no `depends_on "llvm"` of any kind, so Homebrew never
builds LLVM, never installs a 1.7 GB keg alongside a ~12 MB compiler, and never rebuilds us
when `llvm` bumps.

Bottles are not needed. They cache *source* builds, and there is no source build. This also
retires the `arm64_sequoia` keying problem, in which a bottle built on `macos-15` silently
leaves a Sonoma user compiling from source.

Homebrew fetches over curl, which does not set `com.apple.quarantine` (§5). Gatekeeper
therefore never engages on this path, and `brew install` works without notarisation. That makes
the tap the recommended macOS channel, and demotes notarisation to covering only the direct
download of the tarball.

Run `brew style` and `brew audit --strict --online` in the package job.

## 4. Upstream homebrew-core

Foreclosed by the binary formula in §3 above. homebrew-core builds every formula from source in
its own CI and does not accept formulae that install prebuilt binaries.

The LLVM major lock is **not** what closes this, and the previous reasoning here was wrong.
Under static linking the dependency would be `depends_on "llvm@22" => :build` — build-time
only, so no runtime keg and no rebuild cascade when `llvm` bumps. The residual would have been
a policy matter: homebrew-core discourages new formulae pinned to versioned LLVM, and prunes
old `llvm@N` eventually.

What the tap costs is discoverability — `brew install llvm-dsdl` requires `brew tap` first.
Weighed against a notability bar this project does not yet clear, and against deleting the
entire source-build path, the tap wins. D5 resolves accordingly.

## 5. apt repository

A static file tree on GitHub Pages from a `gh-pages` branch:

```
dists/stable/{InRelease,Release,Release.gpg}
dists/stable/main/binary-{amd64,arm64}/Packages{,.gz}
pool/main/l/llvm-dsdl/*.deb
```

Generated with `apt-ftparchive`, `InRelease` signed with a release key held in secrets. Sign from
the start — the alternative is teaching users `[trusted=yes]`. Publishing must be **additive**:
download the existing index, add the new pool entries, regenerate. Regenerating from only the
current release silently deletes every prior version.

Once packages are dependency-free (§2 above), the repository is for upgrades and discovery
rather than dependency resolution. Nothing about the repo changes the packaging. The DEP-5
`copyright` stanza covering LLVM stays required either way — static linking redistributes that
code as surely as vendoring the shared object did.

## 6. Other platforms

- **Windows.** Unblocked by §1 and §2 above. WinGet accepts a plain zip with
  `NestedInstallerType: portable`, and Scoop is nearly free on top of the same zip. Standard
  `windows-latest` runners are free on a public repository, so verification hardware is not a
  constraint.
- **32-bit ARM.** Reachable as `armv7-linux-musleabihf`. This covers Raspberry Pi OS 32-bit;
  true Raspbian targets ARMv6+VFP2, which is a further triple rather than a further problem.
- **Intel macOS.** Not supported. See D2 below.
- **RPM.** CPack's RPM generator plus a Fedora COPR project — the cheapest format to add, and
  cheaper still once the package carries no dependencies.
- **snap.** Snaps bundle everything, which is the problem a static binary has already solved.
  Reconsider only if confinement or the Snap Store's reach is wanted for its own sake.

---

## 7. Open decisions

| # | Decision | Current answer |
|---|---|---|
| D1 | Who owns the tap and apt repo | `OpenCyphal-Garage`, matching `upstream` |
| D2 | Is Intel macOS supported | No. Neither `bin` nor `dev` |
| D3 | What `llvm-dsdl-dev` targets | macOS arm64 and Linux only; no Windows, no Intel macOS |
| D4 | Glibc floor | Dissolved for `bin` by static musl. **Open for `dev`** — see below |
| D5 | The LLVM major lock vs upstream homebrew-core | Resolved: stay in our own tap |
| D6 | Lint plugins vs a static `dsdld` | Undecided — feature-gate with a loud diagnostic, ship `dsdld` dynamically, or move plugins out-of-process |
| D7 | Windows on ARM | Undecided; gated on verification, not on the build |
| D8 | macOS deployment-target floor | Undecided — ours to set once we own the build, same shape as D4 |

**D4 splits by component.** A musl + libc++ static build serves `bin`, but the `dev`
component's static libraries carry that C++ ABI, and a consumer compiling with glibc and
libstdc++ cannot link them. On macOS arm64 this is harmless, as libc++ is the platform default.
On Linux it means `bin` and `dev` want different toolchain configurations — musl for reach,
glibc and libstdc++ to match what consumers actually compile with.
