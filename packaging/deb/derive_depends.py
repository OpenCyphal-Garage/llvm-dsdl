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

# Always present, and not always reported as a resolvable "=>" entry by ldd.
BASELINE = ("libc6",)

_LDD_LINE = re.compile(r"^\s*\S+\s*=>\s*(?P<path>/\S+)")


def linked_libraries(binary: Path) -> list[Path]:
    """Absolute paths of the shared objects @p binary resolves against."""
    try:
        proc = subprocess.run(
            ["ldd", str(binary)], capture_output=True, text=True, timeout=120)
    except FileNotFoundError:
        raise SystemExit("ldd not found; this must run on the build host")
    if proc.returncode != 0:
        # Not a dynamic executable, or unresolvable -- nothing to contribute.
        return []
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
    packages: set[str] = set(BASELINE)
    for binary in binaries:
        for library in linked_libraries(binary):
            # Anything we ship ourselves must not become a dependency: there is
            # no package providing it, and depending on it would be circular.
            if any(token in library.name for token in vendored):
                continue
            pkg = owning_package(library)
            if pkg:
                packages.add(pkg)
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
