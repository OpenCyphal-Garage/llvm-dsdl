# Addressing the embedded catalog (design)

`dsdlc` compiles the standard `uavcan` namespace into the binary as an MLIR catalog, but there is
no way to *name* it on the command line. This note specifies the `+` target sigil that closes that
gap, and the depfile correctness fix that has to land with it.

Status: implemented. The depfile fix (§4) landed first, then the `+` sigil (§2, §3). §8 records the
questions deliberately left open. This note is kept as the rationale record — the user-facing
description lives in [the `dsdlc` reference](../reference/commands/dsdlc.md).

---

## 1. The gap

The embedded catalog is reachable only by accident of reference. It is installed as
`analyzeOptions.externalSemanticCatalog` and merged into the MLIR module for whatever falls inside
the dependency closure of the explicit targets
([`main.cpp:1238`](https://github.com/OpenCyphal-Garage/llvm-dsdl/blob/main/tools/dsdlc/main.cpp)).
Nothing ever seeds `uavcan.*` *as* an explicit target, so:

```console
$ dsdlc --target-language c --list-outputs
files generated: 0
```

Two separate reasons produce that empty result. There are no positional targets, so
`resolveTargets` returns an empty `explicitTargetFiles` and `main.cpp:1204` short-circuits before
the catalog is even loaded. And even with a target, only referenced types materialize:

```console
$ dsdlc --target-language c --list-outputs test/lit/fixtures_embedded_uavcan/demo
…/demo/UsesHeartbeat_1_0.{c,h};…/dsdl_runtime.h;…/uavcan/node/{Health,Heartbeat,Mode}_1_0.{c,h}
```

Heartbeat and its two transitive dependencies — not the namespace.

The practical cost is that the embedded catalog only removes the `public_regulated_data_types`
checkout for *dependency resolution*. A project that wants the standard namespace generated —
vendoring it into a shared library, which is the common build-integration shape — still needs the
submodule it was supposed to make unnecessary.

## 2. Syntax

A positional target beginning with `+` names the embedded catalog rather than the filesystem.

```console
dsdlc --target-language c --list-outputs +uavcan
dsdlc --target-language c +uavcan.node --outdir out/c
dsdlc --target-language c +uavcan.node.Heartbeat.1.0 --outdir out/c
```

### Why `+`

Sigil candidates were tested for pass-through in `zsh` and `bash`:

| Sigil | Result |
| --- | --- |
| `=` | **Rejected.** zsh equals-expansion: `=uavcan` → `zsh: uavcan not found` |
| `~` | **Rejected.** Tilde expansion: `~uavcan` → `no such user or named directory` |
| `^` | **Rejected.** Passes POSIX shells; `cmd.exe` escape character |
| `%` | **Rejected.** Passes POSIX shells; `cmd.exe` variable sigil |
| `:` | Viable in shells; **rejected** on failure-mode grounds — see below |
| `@` | Viable in shells; **rejected** on collision grounds — see below |
| `,` | Viable; no advantage over `+` and reads as punctuation |
| `+` | **Chosen.** Clean in every shell tested, no collisions, safe degradation |

**`:` was rejected because its failure mode is silent.** The existing `root:path/Type.1.0.dsdl`
colon syntax ([`TargetResolution.cpp:264`](https://github.com/OpenCyphal-Garage/llvm-dsdl/blob/main/lib/Frontend/TargetResolution.cpp))
suggests spelling the builtin root as the empty root, `:uavcan/time/TimeSystem.0.1.dsdl`. But
consider a build system that means to emit `myroot:uavcan/time/TimeSystem.0.1.dsdl` — a project
deliberately *overriding* the standard type with its own — and whose `myroot` variable comes back
empty. Under empty-root-means-builtin, that bug resolves cleanly against the embedded catalog and
silently generates the original type. The build succeeds and ships the wrong definition.

`+` inverts that. Truncating `myroot+uavcan/time/TimeSystem.0.1.dsdl` yields
`uavcan/time/TimeSystem.0.1.dsdl`, a relative path that does not exist, which fails loudly. To
reach the bad outcome a build system would have to *insert* a sigil rather than drop a variable —
a far less likely defect. **Leading sigils degrade safely; infix separators do not.**

**`@` was rejected on two collisions.** In PowerShell, `@name` is splatting syntax (`@` replaces
`$` to splat a hashtable into a command's arguments), so `dsdlc @uavcan` would attempt to splat an
undefined `$uavcan` on a platform the toolchain supports. (Documented PowerShell behaviour; not
reproduced locally — no `pwsh` on the development host.) Separately, `@file` is the
LLVM/GCC/MSVC response-file convention, and CMake and Ninja *generate* response files
automatically when a command line exceeds the Windows 32k limit. `dsdlc` has no response-file
support today — unrecognized non-`-` tokens go straight to `positionalTargets` at
[`main.cpp:841`](https://github.com/OpenCyphal-Garage/llvm-dsdl/blob/main/tools/dsdlc/main.cpp) —
but build integration is exactly the work that would need it. Leaving `@` unspent is worth more
than the marginal readability of `@uavcan` over `+uavcan`.

### Escaping

`+` is significant in the positional stream, including after `--`; the `--` separator ends *option*
parsing, and a builtin reference is a target token, not an option. A real file whose name begins
with `+` is addressed as `./+uavcan`. Document this rather than inventing a second escape.

## 3. Semantics

### Granularity

All three levels resolve against `UavcanEmbeddedCatalog::typeKeys`, which is already an enumerable
`unordered_set` of `full_name:major:minor` keys:

| Form | Selects |
| --- | --- |
| `+uavcan` | Every embedded type under the namespace |
| `+uavcan.node` | Every embedded type under the subnamespace |
| `+uavcan.node.Heartbeat.1.0` | One type |

Namespace forms are a prefix match on the key's `full_name`, anchored at a dot boundary so
`+uavcan.n` does not match `uavcan.node`. The type form is an exact key match.

### Explicit-target marking

`isExplicitTarget` is currently computed purely by path comparison at
[`main.cpp:1234`](https://github.com/OpenCyphal-Garage/llvm-dsdl/blob/main/tools/dsdlc/main.cpp).
Embedded definitions carry synthetic `<embedded-uavcan>:` paths and can never match, so they need a
parallel key-based marking pass over the merged semantic module. Without it `collectExplicitKeys`
([`main.cpp:933`](https://github.com/OpenCyphal-Garage/llvm-dsdl/blob/main/tools/dsdlc/main.cpp))
and the closure computation disagree about what was requested, and `--omit-dependencies` silently
does the wrong thing.

Consequences that follow from marking them properly, and that should be asserted in tests:

- `+uavcan.node.Heartbeat.1.0` alone pulls `Health` and `Mode` into the closure.
- `+uavcan.node.Heartbeat.1.0 --omit-dependencies` emits Heartbeat only.
- A local target and a `+` target in the same invocation union their closures.
- A local `uavcan.node.Heartbeat.1.0` shadows the embedded one — `mergeSemanticModulesPreferPrimary`
  already prefers local definitions, and `+uavcan` must not defeat that.

### Errors

Both of these are hard errors, never empty selections. The failure this design exists to prevent is
a *successful* build that quietly omits or substitutes types.

- `+uavcan` with `--no-embedded-uavcan`: `cannot select embedded target '+uavcan' with
  --no-embedded-uavcan`.
- A `+` token matching zero keys: an unknown-target diagnostic with a did-you-mean drawn from
  `typeKeys`. `+uavcna` and `+uavcan.node.Hartbeat.1.0` must both fail loudly.

## 4. The depfile fix — landed

**This was a pre-existing bug, fixed ahead of `+`.** It is independent of the sigil, but `+` would
have amplified it from a handful of stray files to an entire namespace.

Two things combined to produce it. Embedded definitions are excluded from depfile prerequisites
because their synthetic paths name no file a build system can `stat`. And the planner was built
from the *local* semantic module, so embedded types had no node at all — making "resolved from the
compiled-in catalog" indistinguishable from "unknown type", with both yielding no dependencies. The
`.d` file was still written, so the output was a rule with no prerequisites:

```console
$ dsdlc --target-language c -MD --outdir out test/lit/fixtures_embedded_uavcan/demo
$ cat out/uavcan/node/Heartbeat_1_0.h.d
/…/out/uavcan/node/Heartbeat_1_0.h:
```

Nothing rebuilds that header, ever. Upgrade `dsdlc` to a build carrying a newer catalog and the
stale generated sources survive.

The correct prerequisite is **the `dsdlc` executable itself**: the catalog is compiled into the
binary, which is precisely why it has no source file.

As implemented, `DepfilePlanner` takes a `toolchainStampPath` and records it for any output whose
closure reaches an embedded definition. Outputs mixing local and embedded sources list their real
inputs plus the binary; outputs that never touch the catalog are untouched, so a compiler upgrade
does not rebuild work that owes the catalog nothing. `main.cpp` resolves the path with
`llvm::sys::fs::getMainExecutable` rather than trusting `argv[0]`, and now builds the planner from
the closure over the *merged* module so embedded definitions are present as nodes.

This makes the dependency graph honest — a toolchain upgrade is a real input change — at the cost
of rebuilding embedded-sourced files when `dsdlc` is rebuilt, which is what a compiler upgrade
should do. The catalog's identity is already tracked as `kEmbeddedUavcanMlirSha256` with a
load-time integrity check
([`UavcanEmbeddedCatalog.h:67`](https://github.com/OpenCyphal-Garage/llvm-dsdl/blob/main/include/llvmdsdl/CodeGen/UavcanEmbeddedCatalog.h)),
so a finer-grained stamp file remains available later if binary-granularity rebuilds prove too
coarse.

One adjacent case is deliberately left alone: `dsdl_runtime.h` is pure compiler output with no
required type keys, so it still gets an empty rule. Same class of defect, different cause; it
wants its own fix.

## 5. Interaction matrix

| Surface | Behaviour with `+` targets |
| --- | --- |
| `--list-outputs` | Lists embedded-sourced outputs. Already does so for closure-pulled types. |
| `--list-inputs` | Emits **nothing** for embedded types. Synthetic paths must never leak into a CMake `DEPENDS` set. |
| `-MD` | Prerequisite is the `dsdlc` binary, per §4. |
| `--omit-dependencies` | Restricts to the marked explicit keys, embedded included. |
| `--no-embedded-uavcan` | Hard error, per §3. |
| `--no-target-namespaces` | Unaffected; it governs filesystem folder expansion only. |

The `--list-inputs` / `--list-outputs` asymmetry is deliberate and worth a comment in the code:
outputs are real files a build system must know about, while inputs feed `DEPENDS` and must stay
filesystem-truthful. The binary lands in the depfile instead.

## 6. Implementation, as built

| Location | Change |
| --- | --- |
| `main.cpp` `addTargetToken` | Partitions target tokens: `+` prefix → `builtinTargets`, else `positionalTargets`. Shared by the normal loop and the post-`--` drain, which is what keeps the sigil significant after `--`. |
| `main.cpp` `validateLanguageGatedOptions` | Gates `+` on language and on `--no-embedded-uavcan`, alongside the existing per-language option gates. |
| `main.cpp` empty-target short-circuit | Now also requires `builtinTargets` to be empty, or `+uavcan` alone would exit 0 silently. |
| `main.cpp` after catalog load | Expands each selector and fails with a did-you-mean on zero matches, before analysis. |
| `main.cpp` `explicitKeys` | Unions the expanded builtin keys into the explicit set. |
| `UavcanEmbeddedCatalog.{h,cpp}` | `expandEmbeddedCatalogSelector` → `EmbeddedSelectorExpansion{typeKeys, suggestions}`. |
| `DepfilePlanner.{h,cpp}` | Done in §4; needed no further change for `+`. |
| `main.cpp` `printUsage` | `--help` text. |
| `README.md`, `docs/reference/commands/dsdlc.md` | User-facing documentation. |

Two things the design predicted and the implementation did **not** need:

- **No `TargetResolution` change.** Selectors never touch the filesystem, so threading them through
  `ResolvedTargets` would have been plumbing for its own sake. They go straight from `CliOptions` to
  the catalog.
- **No key-based `isExplicitTarget` marking.** `isExplicitTarget` has exactly one reader,
  `collectExplicitKeys`, so unioning the expanded keys into `explicitKeys` is equivalent and does
  not require mutating definitions that were never parsed from a file. Shadowing then falls out of
  `mergeSemanticModulesPreferPrimary` for free rather than needing its own rule.

## 7. Test plan

`test/lit/embedded-catalog-targets.txt` covers the CLI surface: the three granularities,
`--omit-dependencies`, the dot-boundary anchor, `mlir`, the `--list-outputs` / `--list-inputs`
asymmetry, the depfile stamp, local shadowing, all four diagnostics (typo'd namespace, typo'd type,
unavailable version, partial prefix), both gates, `--` non-interference, and `./+x` as a path.

It also pins the `:` behaviour that motivated the sigil choice, so nobody later makes empty-root
meaningful: `:uavcan/node/Heartbeat.1.0.dsdl` must stay an error via `resolveExistingDirectory("")`
([`TargetResolution.cpp:273`](https://github.com/OpenCyphal-Garage/llvm-dsdl/blob/main/lib/Frontend/TargetResolution.cpp)).

`test/lit/embedded-uavcan-catalog.txt` keeps the §4 regression pinned independently through its
`DEPFILE_EMBEDDED` prefix, so the depfile fix is not resting on the `+` tests.

Unit coverage in `UavcanEmbeddedCatalogTests.cpp` for selector expansion — exact version, padded
version, unversioned type name, namespace, root-selects-everything, sortedness, dot boundary,
version near-miss, spelling near-miss, empty selector — and in `DepfilePlannerTests.cpp` for the
toolchain stamp.

## 8. Open questions

1. **Does `+uavcan` include deprecated types?** It does — the catalog carries whatever
   `public_regulated_data_types` ships, and `+uavcan` selects all 189 schemas in it, so generating
   the whole namespace emits deprecation notices for types the user never named. A
   `--no-deprecated-builtins` filter may be wanted; still deferred until the noise is observed in a
   real build.
2. **Should `--version` report the catalog identity?** `kEmbeddedUavcanMlirSha256` and the upstream
   revision are both known at build time. Cheap, and it makes "which standard types does this
   binary carry" answerable without generating anything. Probably yes, separately.
3. **Is `+` right for future non-`uavcan` builtins?** The sigil is namespace-generic by
   construction, so a second embedded catalog needs no new syntax. Nothing to decide now.
