# Distribution channels (backlog)

Distribution channels that do not exist yet, and the decisions still open about them.

The pipeline that **does** ship today — what the `.deb` and macOS tarball contain, the build
environments, LLVM vendoring, Debian and macOS packaging, the release workflow, verification,
and why cross-compiling is closed — is documented as built in
[docs/development/release-packaging.md](release-packaging.md). A bare section reference
below (§3, §6, §8) points *there*; a reference to a section of this document says "above".

---

## 1. Homebrew

A tap (`OpenCyphal-Garage/homebrew-llvm-dsdl`) needing a PAT held as a secret here. The formula
builds from source, and CI additionally produces bottles so the common path is a binary download:
`brew install --build-bottle`, `brew bottle --json --root-url=<release asset URL>`, attach the
bottle to the release, `brew bottle --merge --write`, commit to the tap.

Bottles are keyed by macOS version, not just architecture — a bottle built on `macos-15` is
`arm64_sequoia`, and a Sonoma user silently builds from source instead. Build the newest two the
runners offer and let the rest compile. Run `brew style` and `brew audit --strict --online` in the
package job.

**The formula should vendor**, matching the `.deb`, via `depends_on "llvm@22" => :build` plus the
install-name rewriting §3 already does. `depends_on "llvm@22"` at runtime is the idiomatic answer
and the one to reach for by default, but the measured cost is lopsided: a 218 MB vendored bundle
against the 1.7 GB `llvm` keg a user would otherwise install to run a ~12 MB compiler. The usual
objection — that vendoring forecloses homebrew-core — does not bite here, because homebrew-core is
blocked by the LLVM pin below regardless, and a formula is a few dozen lines to rewrite if that
ever changes.

For reference when writing the formula: Homebrew's current `llvm` is 22.1.8, our locked major, and
`llvm@22` is an alias for it. Real versioned formulae exist for `llvm@14` through `llvm@21`, so
each major gains one as it is superseded — `depends_on "llvm@22"` should keep resolving across the
rollover. A scheduled canary asserting that `llvm@22` resolves *and* reports major 22 turns a
break into a red check rather than a user's bug report.

## 2. Upstream homebrew-core

Blocked by the **LLVM major lock**, not by packaging. homebrew-core rebuilds dependents when
`llvm` bumps, and `LLVMDSDL_REQUIRED_LLVM_MAJOR` hard-fails configure against any other major — by
design, because EmitC output varies across MLIR majors and this project pins byte-reproducibility
to one (see [supply-chain.md](../reference/guarantees/supply-chain.md)). A formula that refuses to build the day `llvm`
rolls is one homebrew-core will not carry.

| Option | Cost |
|---|---|
| Track LLVM majors promptly, bumping the lock as each lands | Ongoing maintenance tied to LLVM's cadence |
| Accept a range of majors, reproducibility guaranteed per-major | Weakens the single-major guarantee |
| Stay in our own tap | No upstream, no compromise |

The tap costs nothing and forecloses nothing. This should be decided on its own merits rather than
under a packaging deadline. (homebrew-core also has a notability bar, which is a matter of
adoption rather than engineering.)

## 3. apt repository

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

The DEP-5 `copyright` already covers the vendored library, so nothing about the repo changes the
packaging.

## 4. Other platforms

- **Intel macOS.** Requires either an x86_64 runner or `-arch x86_64` on the arm64 runner with
  verification under Rosetta 2. The bundle is architecture-specific, so a universal binary would
  mean building both halves.
- **snap.** A natural fit — snaps bundle everything, so the vendoring question disappears. `base:
  core24`, `confinement: strict`, plugs `home` and `removable-media`.
- **Windows.** WinGet accepts a plain zip with `NestedInstallerType: portable`, and Scoop is nearly
  free on top of the same zip. The packaging is easy; the *build* is not, because there is no
  prebuilt LLVM 22 with MLIR for Windows. It is blocked on the cached-toolchain pipeline below.
- **RPM.** CPack's RPM generator plus a Fedora COPR project — the cheapest format to add, since the
  component split and vendoring are already settled.

## 5. Cached LLVM toolchain

Several constraints trace back to *whose* LLVM 22 we build against: the glibc floor is
apt.llvm.org's choice, the brew formula follows Homebrew's lifecycle, and Windows has no prebuilt
LLVM with MLIR at all. Building LLVM/MLIR 22 ourselves once per target, caching it as a GHCR image
or release artifact keyed by `llvm-rev + triple + stdlib`, and restoring it in the build job
dissolves all three. It runs when the pin changes, not per release.

This is deliberately not a prerequisite for anything shipping today. The four-stage workflow
factoring (§6) exists so the toolchain source is an implementation detail of the build job: it can
swap in later without touching the package, verify, or publish stages.

Landing it would also reopen cross-compilation, which is closed today for reasons recorded in §8:
blockers 1 and 2 disappear, 3 is mechanical, and only "verification requires execution" remains.

---

## 6. Open decisions

| # | Decision | Current answer |
|---|---|---|
| D1 | Who owns the tap and apt repo | `OpenCyphal-Garage`, matching `upstream` |
| D2 | Is Intel macOS supported | Not built; `-arch x86_64` plus Rosetta verification is the likely route if wanted |
| D3 | Does `llvm-dsdl-dev` publish | Built and verified; published to apt when the repo exists |
| D4 | Glibc floor once we build our own LLVM | Undecided — a supported-distro policy question, not a technical one |
| D5 | The LLVM major lock vs upstream homebrew-core | Undecided; see §2 above |
