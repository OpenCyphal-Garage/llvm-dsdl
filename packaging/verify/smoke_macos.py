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
library reference must resolve inside the bundle. A binary still pointing at
/opt/homebrew would run perfectly here and fail on a user's machine, and only
this check tells the two apart.

Behaviour is checked too, since a bundle that links correctly and still cannot
emit code is no use either.
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

_OTOOL_LINE = re.compile(r"^\s+(?P<path>\S+)\s+\(compatibility")


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


def evaluate(root: Path, expected_version: str | None) -> list[str]:
    """Return failure messages; empty means the tarball is sound."""
    failures: list[str] = []
    bindir = root / "bin"

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
    args = parser.parse_args(argv)

    if not args.tarball.is_file():
        raise SystemExit(f"tarball not found: {args.tarball}")
    if sys.platform != "darwin":
        print("SKIPPED: this verifier uses otool and must run on macOS")
        return 77
    if not shutil.which("otool"):
        raise SystemExit("otool not found; Xcode command line tools are required")

    with tempfile.TemporaryDirectory() as tmp:
        dest = Path(tmp)
        # Extracted with tarfile rather than Finder: quarantine propagation
        # differs between them, and `tar` is what the install instructions say.
        with tarfile.open(args.tarball) as tf:
            tf.extractall(dest, filter="data")

        roots = [p for p in dest.iterdir() if p.is_dir()]
        root = roots[0] if len(roots) == 1 else dest

        failures = evaluate(root, args.expect_version)
        if not failures:
            failures = check_codegen(root)

        print(f"smoke-tested {args.tarball.name}")
        for tool in TOOLS:
            exe = root / "bin" / tool
            if exe.is_file():
                refs = [r for r in linked_paths(exe) if not r.startswith(SYSTEM_PREFIXES)]
                leaked = len(leaked_references(exe))
                # Counted separately: "non-system" alone would report a leaking
                # binary and a sound one identically.
                print(f"  {tool}: {len(refs) - leaked} bundle-relative, {leaked} external")

        if failures:
            print("\nFAILED:")
            for failure in failures:
                print(f"  - {failure}")
            return 1
        print("\nOK: self-contained, runs, and generates compilable code")
        return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
