#!/usr/bin/env python3
# ===----------------------------------------------------------------------===//
#
# Part of the OpenCyphal project, under the MIT licence
# SPDX-License-Identifier: MIT
#
# ===----------------------------------------------------------------------===//

"""Derive the Debian ``Depends`` list from what the binaries actually link.

``dpkg-shlibdeps`` cannot be used here: it resolves every linked library against
the dpkg database, and the vendored ``libLLVM`` belongs to no installed package,
so it either fails or invents a wrong dependency. Hand-maintaining the list
instead is worse -- it is invisible when wrong. A package whose ``Depends`` omits
a transitively needed library installs perfectly and then dies on first run with
a missing shared object, which is exactly what an unmaintained list produces.

So the list is derived from the binaries at package time: walk what the tools and
the vendored library link, drop anything shipped inside the package, and map the
rest back to owning packages with ``dpkg -S``.

Linux-only by nature, and only meaningful on a Debian-family host.
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

# Applied only when something actually links a dynamic libc: ldd does not always
# report it as a resolvable "=>" entry, so a dynamically linked binary needs it
# added by hand. A statically linked binary genuinely has no libc dependency, and
# asserting one there would contradict the point of linking statically.
BASELINE = ("libc6",)

_LDD_LINE = re.compile(r"^\s*\S+\s*=>\s*(?P<path>/\S+)")


def is_dynamic(binary: Path) -> bool:
    """Whether @p binary declares a dynamic loader.

    Read from the ELF program headers rather than inferred from ldd's exit
    status, because ldd fails identically for a statically linked binary and
    for one it cannot process at all -- a foreign architecture, most likely.
    Those must not be conflated: the first contributes no dependencies because
    it genuinely has none, and the second contributes none because we failed to
    look, which would silently produce a package claiming to need nothing.
    """
    try:
        proc = subprocess.run(
            ["readelf", "-lW", str(binary)], capture_output=True, text=True, timeout=120)
    except FileNotFoundError:
        raise SystemExit("readelf not found; this must run on the build host")
    if proc.returncode != 0:
        raise SystemExit(
            f"cannot read ELF program headers of {binary}: {proc.stderr.strip()}")
    return "INTERP" in proc.stdout


def linked_libraries(binary: Path) -> list[Path]:
    """Absolute paths of the shared objects @p binary resolves against."""
    try:
        proc = subprocess.run(
            ["ldd", str(binary)], capture_output=True, text=True, timeout=120)
    except FileNotFoundError:
        raise SystemExit("ldd not found; this must run on the build host")
    if proc.returncode != 0:
        # The caller only reaches here for a binary the ELF headers say is
        # dynamic, so a failure now means ldd could not resolve it -- which is
        # exactly the situation where guessing is worse than stopping.
        raise SystemExit(
            f"ldd failed on {binary}, which declares a dynamic loader: "
            f"{proc.stderr.strip() or proc.stdout.strip()}")
    out: list[Path] = []
    for line in proc.stdout.splitlines():
        match = _LDD_LINE.match(line)
        if match:
            out.append(Path(match.group("path")))
    return out


def owning_package(library: Path) -> str | None:
    """The Debian package providing @p library, or None if unpackaged."""
    resolved = library.resolve()
    proc = subprocess.run(
        ["dpkg", "-S", str(resolved)], capture_output=True, text=True, timeout=120)
    if proc.returncode != 0 or not proc.stdout.strip():
        return None
    # "pkg1, pkg2: /path" for a diverted file; the first entry is enough.
    return proc.stdout.split(":", 1)[0].split(",")[0].strip() or None


def derive(binaries: list[Path], vendored: list[str]) -> list[str]:
    """Return sorted package names the given binaries need at runtime."""
    packages: set[str] = set()
    any_dynamic = False
    for binary in binaries:
        # An analysis target that is not there at all is a build or wiring
        # error, not a binary with no dependencies. Saying so here is what keeps
        # an empty result meaningful: it can then only mean "statically linked".
        if not binary.is_file():
            raise SystemExit(f"analysis target does not exist: {binary}")
        if not is_dynamic(binary):
            continue
        any_dynamic = True
        for library in linked_libraries(binary):
            # Anything we ship ourselves must not become a dependency: there is
            # no package providing it, and depending on it would be circular.
            if any(token in library.name for token in vendored):
                continue
            pkg = owning_package(library)
            if pkg:
                packages.add(pkg)
    if any_dynamic:
        packages.update(BASELINE)
    return sorted(packages)


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "binaries", nargs="+", type=Path,
        help="Executables and shared objects to analyse. Include the vendored "
             "library itself: its own dependencies are equally load-bearing, and "
             "missing them is what makes a package install cleanly and then fail.")
    # No default: whether libLLVM is vendored is the caller's decision, and
    # excluding it by default would silently drop a genuine dependency from a
    # build that links the system copy.
    parser.add_argument(
        "--vendored", action="append", default=[],
        help="Substring identifying a library shipped inside the package, which "
             "must therefore not appear in Depends (e.g. 'libLLVM.so'). Repeatable.")
    args = parser.parse_args(argv)

    missing = [b for b in args.binaries if not b.exists()]
    if missing:
        raise SystemExit(f"not found: {', '.join(str(m) for m in missing)}")

    print(", ".join(derive(args.binaries, args.vendored)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
