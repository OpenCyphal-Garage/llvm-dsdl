# Distribution channels (backlog)

Packages, channels and targets that do not ship yet, and the decisions still open about them.

What **does** ship — the toolchain, the `.deb` for amd64 and arm64, the macOS tarball, the
release workflow and its verification — is documented as built in
[release-packaging.md](release-packaging.md). A bare section reference below (§5, §7, §8) points
*there*; a reference to a section of this document says "above".

---

## 1. Fully static packages

The shipped `.deb` links glibc and `libstdc++` dynamically and declares `libc6, libstdc++6`. A
package that declares nothing at all is reachable and proven: `LLVMDSDL_STATIC_BINARIES=ON`
against the musl toolchain, built in
[packaging/docker/Dockerfile.alpine-release](https://github.com/OpenCyphal-Garage/llvm-dsdl/blob/main/packaging/docker/Dockerfile.alpine-release),
yields `static-pie` executables and a `.deb` with **no `Depends` field**. It installs and
generates compilable code on Debian bullseye (glibc 2.31, below the floor the shipped package
requires), bookworm, Ubuntu 22.04 and 26.04.

Alpine carries `dpkg-dev`, so CPack's DEB generator runs there directly and no second image is
needed. `lintian` has no Alpine package, so that check stays in the Ubuntu verification lane.

Two costs decide whether the release switches to it:

- **Size.** 38.7 MB against 21.4 MB. `libstdc++` and musl live inside the package rather than
  being resolved from the system.
- **D6.** A static `dsdld` cannot load lint plugins. `LLVMDSDL_STATIC_BINARIES` is opt-in, so
  nothing is lost while the release does not use it.

💡 musl's allocator is markedly slower than glibc's under allocation-heavy C++, which describes
MLIR exactly. Measure `dsdlc` against the corpus before switching, and link mimalloc or jemalloc
if the difference is material.

## 2. Architectures without a runner

Owning the toolchain settles the dependency side of cross-compilation (§8): there is a
target-built LLVM/MLIR, the C++ ABI is ours on both sides, and TableGen is a host-native
`mlir-tblgen` pointed at the cross build. What remains is that verification requires execution,
and that is the binding constraint on the matrix.

| Target | Fully static | Verification |
|---|:--:|---|
| `armv7-linux-musleabihf` | ✅ | qemu-user |
| `riscv64-linux-musl` | ✅ | qemu-user |
| `x86_64-pc-windows-gnu` | ✅ except OS DLLs | `windows-latest` runner; wine as a pre-check |
| `aarch64-pc-windows-gnu` | ✅ | unresolved — see D7 |
| `*-windows-msvc` | ✅ | the MSVC SDK carries the same licensing problem as the macOS SDK |

### Verification splits in two

The checks in §7 conflate two questions that scale differently:

- **Is the generated output correct** — that the emitted C compiles, that a non-C backend emits.
  The output is portable text, so this is architecture-independent and runs once, on the host.
- **Does the binary run on the target** — this genuinely needs the target.

A fully static CLI binary is the ideal `qemu-user` case: no dynamic loader, no sysroot. Running
corpus generation under emulation and comparing the hash against the host's — the comparison
[corpus_determinism.py](https://github.com/OpenCyphal-Garage/llvm-dsdl/blob/main/tools/determinism/corpus_determinism.py)
already performs across amd64 and arm64 — turns "it launched" into "it produced exactly the right
bytes" for every target, on one machine.

## 3. Homebrew

A tap (`OpenCyphal-Garage/homebrew-llvm-dsdl`) needing a PAT held as a secret here, carrying a
**binary formula**: `url` points at the release tarball from §5, and `install` copies `bin/` into
the prefix. There is no `depends_on "llvm"` of any kind, so Homebrew never builds LLVM, never
installs a 1.7 GB keg alongside a ~14 MB compiler, and never rebuilds us when `llvm` bumps.

Bottles cache *source* builds, and there is no source build, so none are needed. That also avoids
the `arm64_sequoia` keying problem, in which a bottle built on `macos-15` silently leaves a
Sonoma user compiling from source.

Homebrew fetches over curl, which does not set `com.apple.quarantine` (§5). Gatekeeper therefore
never engages on this path, and `brew install` works without notarisation. That makes the tap the
recommended macOS channel, and confines notarisation to the direct tarball download.

Run `brew style` and `brew audit --strict --online` in the package job.

## 4. Upstream homebrew-core

Foreclosed by the binary formula in §3 above. homebrew-core builds every formula from source in
its own CI and does not accept formulae that install prebuilt binaries.

The LLVM major lock does not close this, though it looks like it should:
under static linking the dependency is `depends_on "llvm@22" => :build` — build-time only, so no
runtime keg and no rebuild cascade when `llvm` bumps. The residual is a policy matter, since
homebrew-core discourages new formulae pinned to versioned LLVM and prunes old `llvm@N`.

What the tap costs is discoverability — `brew install llvm-dsdl` requires `brew tap` first.
Weighed against a notability bar this project does not yet clear, and against carrying a
source-build path, the tap wins. D5 resolves accordingly.

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

With a two-package dependency list, the repository is for upgrades and discovery rather than
dependency resolution. Nothing about it changes the packaging.

## 6. Other platforms

- **Windows.** WinGet accepts a plain zip with `NestedInstallerType: portable`, and Scoop is
  nearly free on top of the same zip. Standard `windows-latest` runners are free on a public
  repository, so verification hardware is not a constraint; §2 above is.
- **32-bit ARM.** `armv7-linux-musleabihf` covers Raspberry Pi OS 32-bit. True Raspbian targets
  ARMv6+VFP2, which is a further triple rather than a further problem.
- **Intel macOS.** Not supported. See D2 below.
- **RPM.** CPack's RPM generator plus a Fedora COPR project — the cheapest format to add.
- **snap.** Snaps bundle everything, which is the problem §1 above solves more directly.
  Reconsider only if confinement or the Snap Store's reach is wanted for its own sake.

---

## 7. Open decisions

| # | Decision | Current answer |
|---|---|---|
| D1 | Who owns the tap and apt repo | `OpenCyphal-Garage`, matching `upstream` |
| D2 | Is Intel macOS supported | No. Neither `bin` nor `dev` |
| D3 | What `llvm-dsdl-dev` targets | macOS arm64 and Linux only; no Windows, no Intel macOS |
| D4 | Glibc floor | Resolved: two toolchain flavours — glibc 2.35 for the release, `dev` and CI; musl for §1 above |
| D5 | The LLVM major lock vs upstream homebrew-core | Resolved: stay in our own tap |
| D6 | Lint plugins vs a static `dsdld` | Open, and the gate on §1 above. Feature-gate it, ship `dsdld` dynamically, or move plugins out-of-process. The runtime error is already accurate: musl's static `dlopen` sets `dlerror()` to "Dynamic loading not supported", which `loadPluginLibrary` propagates |
| D7 | Windows on ARM | Open; gated on verification, not on the build |
| D8 | macOS deployment-target floor | Open — ours to set, same shape as D4 |
