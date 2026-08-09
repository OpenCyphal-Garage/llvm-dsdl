#!/usr/bin/env python3
# ===----------------------------------------------------------------------===//
#
# Part of the OpenCyphal project, under the MIT licence
# SPDX-License-Identifier: MIT
#
# ===----------------------------------------------------------------------===//

"""Verify a macOS release tarball is genuinely self-contained.

The Linux verifier installs into a pristine container, which makes "does this
package carry what it needs" answer itself: nothing else is there. macOS has no
such isolation available on a build runner, and the runner is the worst possible
judge -- it has Homebrew LLVM installed at the very paths a non-relocated binary
would reference, so a tarball that only works on the build machine passes every
behavioural test.

So the load-bearing check here is **linkage**, not behaviour: every non-system
library reference must resolve inside the tarball. A binary still pointing at
/opt/homebrew would run perfectly here and fail on a user's machine, and only
this check tells the two apart.

Against the self-built toolchain there are no such references at all: the tools
link /usr/lib/libSystem.B.dylib and /usr/lib/libc++.1.dylib and nothing else, and
the tarball ships no dylibs. The check still applies -- it is what would catch a
build that silently went back to Homebrew -- but it now passes by there being
nothing to relocate rather than by relocation having worked.

Behaviour is checked too, since a tarball that links correctly and still cannot
emit code is no use either.

Two further checks exist only because the release builds more than one macOS
architecture. Neither could fail when there was one: whatever the single runner
produced was by definition what shipped.

- **Architecture.** The tarball's name comes from the build, so a leg producing
  the other architecture under its own name is not caught by anything upstream.
- **Deployment floor.** Nothing in this repository sets one. The minos
  derivation in the top-level CMakeLists keys off finding a `libLLVM.dylib`,
  which a static toolchain does not produce, so the floor is whatever the runner
  boots. That is fine, and chosen -- but it means a runner image bump would
  narrow the supported hardware with no diff to review, which is exactly the
  kind of change that should fail loudly here instead.
"""

from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
from pathlib import Path

TOOLS = ("dsdlc", "dsdl-opt", "dsdld")

# Absolute paths that are legitimate in a shipped binary: the OS supplies these
# and every macOS machine has them. Anything else absolute is a leak.
SYSTEM_PREFIXES = ("/usr/lib/", "/System/")

# The project's packaging vocabulary, which is what names the tarball and what
# --expect-arch is given. lipo speaks the Mach-O spelling; cmake/Packaging.cmake
# performs the same normalisation on the other side of the build.
ARCH_ALIASES = {"x86_64": "amd64", "amd64": "amd64", "aarch64": "arm64", "arm64": "arm64"}

_OTOOL_LINE = re.compile(r"^\s+(?P<path>\S+)\s+\(compatibility")
_MINOS = re.compile(r"^\s*minos\s+(?P<version>[0-9]+(?:\.[0-9]+)*)\s*$", re.MULTILINE)


def linked_paths(binary: Path) -> list[str]:
    proc = subprocess.run(
        ["otool", "-L", str(binary)], capture_output=True, text=True, timeout=120)
    if proc.returncode != 0:
        raise SystemExit(f"otool -L failed for {binary}: {proc.stderr.strip()}")
    out = []
    for line in proc.stdout.splitlines()[1:]:
        m = _OTOOL_LINE.match(line)
        if m:
            out.append(m.group("path"))
    return out


def leaked_references(binary: Path) -> list[str]:
    """References that would not resolve on a machine without our build environment."""
    leaks = []
    for path in linked_paths(binary):
        if path.startswith(("@executable_path/", "@loader_path/", "@rpath/")):
            continue
        if path.startswith(SYSTEM_PREFIXES):
            continue
        leaks.append(path)
    return leaks


def version_tuple(version: str) -> tuple[int, ...]:
    """Compare as numbers. "9.0" sorts above "15.0" as a string, and would pass."""
    return tuple(int(part) for part in version.split("."))


def binary_arches(binary: Path) -> list[str]:
    """The architectures present, in the project's packaging vocabulary."""
    proc = subprocess.run(
        ["lipo", "-archs", str(binary)], capture_output=True, text=True, timeout=120)
    if proc.returncode != 0:
        raise SystemExit(f"lipo -archs failed for {binary}: {proc.stderr.strip()}")
    # An unrecognised slice is reported as lipo spelled it rather than dropped:
    # the point of the check is to notice the unexpected, so swallowing it here
    # would defeat it.
    return [ARCH_ALIASES.get(a, a) for a in proc.stdout.split()]


def deployment_floor(binary: Path) -> tuple[int, ...] | None:
    """The LC_BUILD_VERSION minos, or None if the binary declares none."""
    proc = subprocess.run(
        ["otool", "-l", str(binary)], capture_output=True, text=True, timeout=120)
    if proc.returncode != 0:
        raise SystemExit(f"otool -l failed for {binary}: {proc.stderr.strip()}")
    match = _MINOS.search(proc.stdout)
    return version_tuple(match.group("version")) if match else None


def evaluate(root: Path, expected_version: str | None,
             expected_arch: str | None, max_minos: str | None) -> list[str]:
    """Return failure messages; empty means the tarball is sound."""
    failures: list[str] = []
    bindir = root / "bin"

    ceiling = version_tuple(max_minos) if max_minos else None
    want_arch = ARCH_ALIASES.get(expected_arch, expected_arch) if expected_arch else None

    if not bindir.is_dir():
        return [f"no bin/ directory in the tarball (found: {[p.name for p in root.iterdir()]})"]

    for tool in TOOLS:
        exe = bindir / tool
        if not exe.is_file():
            failures.append(f"{tool} missing from the tarball")
            continue

        leaks = leaked_references(exe)
        if leaks:
            failures.append(
                f"{tool} references paths outside the bundle: {', '.join(leaks)} "
                "-- it would fail on a machine without them")

        if want_arch:
            arches = binary_arches(exe)
            if arches != [want_arch]:
                failures.append(
                    f"{tool} is {'+'.join(arches)}, expected {want_arch} alone "
                    "-- this tarball is named for an architecture it does not contain")

        if ceiling:
            floor = deployment_floor(exe)
            if floor is None:
                failures.append(
                    f"{tool} declares no minimum macOS version, so the hardware it "
                    "supports cannot be established")
            elif floor > ceiling:
                shipped = ".".join(str(p) for p in floor)
                failures.append(
                    f"{tool} requires macOS {shipped}, above the {max_minos} this release "
                    "claims to support -- a runner image bump will do this, and it drops "
                    "hardware. Either lower the floor or raise --max-minos deliberately")

        proc = subprocess.run(
            [str(exe), "--version"], capture_output=True, text=True, timeout=120)
        if proc.returncode != 0:
            failures.append(f"{tool} --version failed: {proc.stderr.strip()[:200]}")
        elif expected_version and expected_version not in proc.stdout:
            failures.append(
                f"{tool} reports {proc.stdout.strip()!r}, expected {expected_version!r}")

    # The vendored libraries must be self-consistent too: one of them still
    # pointing at Homebrew breaks the tools just as thoroughly.
    for lib in sorted((root / "lib").glob("*.dylib")) if (root / "lib").is_dir() else []:
        leaks = leaked_references(lib)
        if leaks:
            failures.append(f"{lib.name} references paths outside the bundle: {', '.join(leaks)}")

    return failures


def check_codegen(root: Path) -> list[str]:
    """Generate C from the packaged dsdlc and compile it with the stock toolchain."""
    failures: list[str] = []
    dsdlc = root / "bin" / "dsdlc"
    if not dsdlc.is_file():
        return ["cannot check codegen: dsdlc missing"]

    with tempfile.TemporaryDirectory() as tmp:
        work = Path(tmp)
        src = work / "ns" / "demo"
        src.mkdir(parents=True)
        (src / "Widget.1.0.dsdl").write_text("uint16 id\nuint8[4] data\n@sealed\n")

        gen = work / "gen"
        proc = subprocess.run(
            [str(dsdlc), "-l", "c", "-I", "ns", "-O", str(gen), "ns/demo/Widget.1.0.dsdl"],
            cwd=work, capture_output=True, text=True, timeout=300)
        if proc.returncode != 0:
            return [f"C codegen failed: {proc.stderr.strip()[:300]}"]

        emitted = gen / "ns" / "demo" / "Widget_1_0.c"
        if not emitted.is_file():
            return [f"C codegen produced no {emitted.name}"]

        cc = subprocess.run(
            ["cc", "-std=c11", "-Wall", "-I", str(gen), "-I", str(emitted.parent),
             "-c", str(emitted), "-o", str(work / "w.o")],
            capture_output=True, text=True, timeout=300)
        if cc.returncode != 0:
            failures.append(f"generated C did not compile: {cc.stderr.strip()[:300]}")
    return failures


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tarball", type=Path, required=True, help="Release tarball to verify.")
    parser.add_argument("--expect-version", help="Assert the tools report this version.")
    parser.add_argument("--expect-arch", choices=sorted(set(ARCH_ALIASES)),
                        help="Assert the binaries contain this architecture and no other.")
    parser.add_argument("--max-minos", metavar="VERSION",
                        help="Assert the binaries run on macOS VERSION, and fail if they "
                             "require anything newer. The floor comes from the build "
                             "machine rather than from any setting, so this is what "
                             "notices a runner image bump narrowing the supported hardware.")
    args = parser.parse_args(argv)

    if not args.tarball.is_file():
        raise SystemExit(f"tarball not found: {args.tarball}")
    if args.max_minos:
        try:
            version_tuple(args.max_minos)
        except ValueError:
            raise SystemExit(
                f"--max-minos must be a dotted version, got {args.max_minos!r}") from None
    if sys.platform != "darwin":
        print("SKIPPED: this verifier uses otool and must run on macOS")
        return 77
    for tool in ("otool", "lipo"):
        if not shutil.which(tool):
            raise SystemExit(f"{tool} not found; Xcode command line tools are required")

    with tempfile.TemporaryDirectory() as tmp:
        dest = Path(tmp)
        # Note that this says nothing about Gatekeeper. The tarball reaching this
        # verifier was built in this job and never downloaded, so it carries no
        # com.apple.quarantine for anything to propagate -- and `tar` does
        # propagate it, onto every extracted file, when the archive has one (see
        # the Gatekeeper section of docs/development/release-packaging.md). No
        # extraction method available here reproduces that, so the quarantine
        # path is verified by hand against a real browser download, not in CI.
        with tarfile.open(args.tarball) as tf:
            tf.extractall(dest, filter="data")

        roots = [p for p in dest.iterdir() if p.is_dir()]
        root = roots[0] if len(roots) == 1 else dest

        failures = evaluate(root, args.expect_version, args.expect_arch, args.max_minos)
        if not failures:
            failures = check_codegen(root)

        print(f"smoke-tested {args.tarball.name}")
        for tool in TOOLS:
            exe = root / "bin" / tool
            if exe.is_file():
                refs = [r for r in linked_paths(exe) if not r.startswith(SYSTEM_PREFIXES)]
                leaked = len(leaked_references(exe))
                floor = deployment_floor(exe)
                # Counted separately: "non-system" alone would report a leaking
                # binary and a sound one identically. Both zero is the expected
                # result against a static toolchain -- nothing to relocate.
                #
                # The architecture and floor are printed whether or not they were
                # asserted: a release log that records what shipped is worth more
                # than one recording only that a check passed.
                print(f"  {tool}: {'+'.join(binary_arches(exe))}, "
                      f"macOS {'.'.join(str(p) for p in floor) if floor else '?'}+, "
                      f"{len(refs) - leaked} relocated, {leaked} external")

        if failures:
            print("\nFAILED:")
            for failure in failures:
                print(f"  - {failure}")
            return 1
        print("\nOK: self-contained, runs, and generates compilable code")
        return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
