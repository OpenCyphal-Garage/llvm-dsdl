#!/usr/bin/env python3
# ===----------------------------------------------------------------------===//
#
# Part of the OpenCyphal project, under the MIT licence
# SPDX-License-Identifier: MIT
#
# ===----------------------------------------------------------------------===//

"""Exercise cmake/Packaging.cmake's Debian output without a full LLVM build.

CMake's DEB generator exists only on Linux, so on any other host the packaging
configuration is unexercised -- the control files, the component split, the
policy metadata, and the inter-package dependency are all invisible until
something actually builds a ``.deb``. This drives a container that runs the real
``cmake/Packaging.cmake`` (not a copy) against stand-in binaries, then checks
what came out.

The stand-ins are sound because nothing in the control files derives from the
binaries: ``shlibdeps`` is off, so no field depends on what is linked. What this
does NOT cover is the payload of a real build or the real dependency list --
those need a Linux build of the project and belong to the release verify lane
(docs/plans/P3_release_packaging.md §6).

Only the container payload is shell, because it is a fixed sequence of
distribution commands (``apt-get``, ``cpack``, ``dpkg-deb``, ``lintian``) running
in a known Linux image. It emits delimited sections; every decision about whether
the packaging is correct is made here, in Python, by functions with no I/O so
they can be unit-tested without Docker -- see test_check_deb_config.py.
"""

from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import sys
import tempfile
from dataclasses import dataclass, field
from pathlib import Path

# Marks a section boundary in the container's output. Chosen not to collide with
# anything dpkg, lintian, or apt emit.
SECTION_PREFIX = "@@LLVMDSDL@@ "

PACKAGES = ("llvm-dsdl", "llvm-dsdl-dev")
TOOLS = ("dsdlc", "dsdl-opt", "dsdld")

# ctest reads this exit code as "skipped" (see SKIP_RETURN_CODE where the test is
# registered). A host without a usable Docker daemon cannot build a .deb at all,
# which is a missing capability rather than a packaging defect -- reporting it as
# a failure would train people to ignore a red test.
EXIT_SKIP = 77

CONTAINER_SCRIPT = r"""
set -euo pipefail
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq >/dev/null 2>&1
# python3 is needed by Packaging.cmake's Depends derivation, which runs at
# package time; without it the harness would skip the very path it is checking.
apt-get install -y -qq cmake make gcc dpkg-dev lintian python3 >/dev/null 2>&1

cmake -S /work -B /tmp/b -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build /tmp/b >/dev/null
cd /tmp/b
cpack -G DEB >/dev/null

version="$(sed -n 's/^set(CPACK_PACKAGE_VERSION "\(.*\)")$/\1/p' CPackConfig.cmake | head -1)"
arch="$(dpkg --print-architecture)"
echo "@@LLVMDSDL@@ meta"
echo "version=${version}"
echo "arch=${arch}"

for pkg in llvm-dsdl llvm-dsdl-dev; do
  deb="${pkg}_${version}_${arch}.deb"
  echo "@@LLVMDSDL@@ exists ${pkg}"
  [ -f "${deb}" ] && echo yes || echo no
  [ -f "${deb}" ] || continue

  echo "@@LLVMDSDL@@ control ${pkg}"
  dpkg-deb -f "${deb}"

  echo "@@LLVMDSDL@@ contents ${pkg}"
  dpkg-deb -c "${deb}" | awk '{print $6}'

  # lintian exits non-zero whenever it reports anything, which `set -e` would
  # treat as fatal; the tags themselves are the result, so swallow the status.
  echo "@@LLVMDSDL@@ lintian ${pkg}"
  lintian --tag-display-limit 0 "${deb}" 2>/dev/null || true
done

echo "@@LLVMDSDL@@ install"
apt-get install -y -qq "./llvm-dsdl_${version}_${arch}.deb" >/dev/null 2>&1
for tool in dsdlc dsdl-opt dsdld; do
  if command -v "${tool}" >/dev/null; then echo "onpath=${tool}"; fi
done

# llvm-dsdl-dev pins an exact version of llvm-dsdl. With the tools package gone,
# dpkg must refuse to configure it; if it succeeds, the pin is not doing its job.
echo "@@LLVMDSDL@@ dependency"
apt-get remove -y -qq llvm-dsdl >/dev/null 2>&1
if dpkg -i "./llvm-dsdl-dev_${version}_${arch}.deb" >/dev/null 2>&1; then
  echo "enforced=no"
else
  echo "enforced=yes"
fi
"""


@dataclass
class Report:
    """Parsed result of one container run."""

    version: str = ""
    arch: str = ""
    exists: dict[str, bool] = field(default_factory=dict)
    control: dict[str, dict[str, str]] = field(default_factory=dict)
    contents: dict[str, list[str]] = field(default_factory=dict)
    lintian: dict[str, list[str]] = field(default_factory=dict)
    tools_on_path: list[str] = field(default_factory=list)
    dependency_enforced: bool = False


def split_sections(output: str) -> list[tuple[str, list[str]]]:
    """Split container output into (header, body-lines) pairs, in order."""
    sections: list[tuple[str, list[str]]] = []
    current: list[str] | None = None
    for line in output.splitlines():
        if line.startswith(SECTION_PREFIX):
            current = []
            sections.append((line[len(SECTION_PREFIX):].strip(), current))
        elif current is not None:
            current.append(line)
    return sections


def _parse_control(lines: list[str]) -> dict[str, str]:
    """Parse a Debian control stanza, joining folded continuation lines."""
    fields: dict[str, str] = {}
    key = None
    for line in lines:
        if not line.strip():
            continue
        if line[0].isspace() and key:
            fields[key] += "\n" + line.strip()
        else:
            match = re.match(r"^([A-Za-z0-9-]+):\s*(.*)$", line)
            if match:
                key = match.group(1)
                fields[key] = match.group(2)
    return fields


def parse_report(output: str) -> Report:
    report = Report()
    for header, body in split_sections(output):
        parts = header.split()
        kind = parts[0]
        pkg = parts[1] if len(parts) > 1 else None
        text = [ln for ln in body if ln.strip()]

        if kind == "meta":
            for line in text:
                if line.startswith("version="):
                    report.version = line.partition("=")[2]
                elif line.startswith("arch="):
                    report.arch = line.partition("=")[2]
        elif kind == "exists" and pkg:
            report.exists[pkg] = bool(text) and text[0] == "yes"
        elif kind == "control" and pkg:
            report.control[pkg] = _parse_control(body)
        elif kind == "contents" and pkg:
            report.contents[pkg] = [ln.strip() for ln in text]
        elif kind == "lintian" and pkg:
            report.lintian[pkg] = [ln for ln in text if re.match(r"^[EWI]:", ln)]
        elif kind == "install":
            report.tools_on_path = [
                ln.partition("=")[2] for ln in text if ln.startswith("onpath=")
            ]
        elif kind == "dependency":
            report.dependency_enforced = "enforced=yes" in text
    return report


def evaluate(report: Report) -> list[str]:
    """Return a list of failure messages; empty means the packaging is sound."""
    failures: list[str] = []

    for pkg in PACKAGES:
        if not report.exists.get(pkg):
            failures.append(f"{pkg}: package was not produced")
            continue

        control = report.control.get(pkg, {})
        if control.get("Package") != pkg:
            failures.append(f"{pkg}: control Package field is {control.get('Package')!r}")
        if control.get("Version") != report.version:
            failures.append(
                f"{pkg}: control Version {control.get('Version')!r} != {report.version!r}")
        if not control.get("Maintainer"):
            failures.append(f"{pkg}: no Maintainer field")

        # Debian's first description line is the synopsis. The two packages must
        # not share one: CPack prepends a single global summary to every
        # component unless it is deliberately left unset, which silently gives
        # the -dev package the tools package's synopsis.
        description = control.get("Description", "")
        if not description:
            failures.append(f"{pkg}: no Description field")

        tags = report.lintian.get(pkg, [])
        if tags:
            failures.append(f"{pkg}: lintian is not clean:\n    " + "\n    ".join(tags))

    synopses = {
        pkg: report.control.get(pkg, {}).get("Description", "").splitlines()[0].strip()
        for pkg in PACKAGES
        if report.control.get(pkg, {}).get("Description")
    }
    if len(synopses) == len(PACKAGES) and len(set(synopses.values())) == 1:
        failures.append(
            "both packages share one synopsis: "
            f"{next(iter(synopses.values()))!r} -- CPACK_PACKAGE_DESCRIPTION_SUMMARY "
            "is probably set again, which re-enables CPack's per-component prepend")

    dev_depends = report.control.get("llvm-dsdl-dev", {}).get("Depends", "")
    if report.version and f"llvm-dsdl (= {report.version})" not in dev_depends:
        failures.append(
            f"llvm-dsdl-dev: Depends {dev_depends!r} does not pin llvm-dsdl (= {report.version})")

    missing_tools = [t for t in TOOLS if t not in report.tools_on_path]
    if missing_tools:
        failures.append(f"not on PATH after install: {', '.join(missing_tools)}")

    if not report.dependency_enforced:
        failures.append("llvm-dsdl-dev configured without llvm-dsdl present; version pin is inert")

    return failures


def docker_unavailable_reason() -> str | None:
    """Return why Docker cannot be used here, or None if it can."""
    docker = shutil.which("docker")
    if not docker:
        return "docker was not found on PATH"
    # Installed but not running is the common case, and it is indistinguishable
    # from a broken install until the daemon is actually probed.
    probe = subprocess.run(
        [docker, "info"], capture_output=True, text=True, timeout=120)
    if probe.returncode != 0:
        return "the docker daemon is not reachable (is Docker running?)"
    return None


def run_container(repo: Path, image: str) -> str:
    docker = shutil.which("docker")
    if not docker:
        raise SystemExit("docker not found on PATH")

    here = Path(__file__).resolve().parent
    with tempfile.TemporaryDirectory() as tmp:
        work = Path(tmp)
        shutil.copyfile(here / "deb-config-harness.CMakeLists.txt", work / "CMakeLists.txt")
        shutil.copyfile(here / "deb-config-harness-stub.c", work / "stub.c")
        (work / "run.sh").write_text(CONTAINER_SCRIPT, encoding="utf-8")

        proc = subprocess.run(
            [
                docker, "run", "--rm",
                "-v", f"{repo}:/repo:ro",
                "-v", f"{work}:/work",
                "-w", "/work",
                image, "bash", "/work/run.sh",
            ],
            capture_output=True,
            text=True,
        )
        if proc.returncode != 0:
            sys.stderr.write(proc.stdout)
            sys.stderr.write(proc.stderr)
            raise SystemExit(f"container run failed with exit code {proc.returncode}")
        return proc.stdout


def format_summary(report: Report) -> str:
    lines = [f"packages built for {report.arch} at version {report.version}"]
    for pkg in PACKAGES:
        control = report.control.get(pkg, {})
        synopsis = control.get("Description", "").splitlines()
        lines.append(f"  {pkg}")
        lines.append(f"    Depends:  {control.get('Depends', '(none)')}")
        lines.append(f"    Synopsis: {synopsis[0] if synopsis else '(none)'}")
        lines.append(f"    lintian:  {len(report.lintian.get(pkg, []))} tag(s)")
    lines.append(f"  tools on PATH after install: {', '.join(report.tools_on_path) or '(none)'}")
    lines.append(f"  -dev version pin enforced:   {'yes' if report.dependency_enforced else 'no'}")
    return "\n".join(lines)


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--image", default="ubuntu:24.04",
        help="Container image to build and inspect the packages in.")
    parser.add_argument(
        "--repo", type=Path, default=Path(__file__).resolve().parents[2],
        help="Repository root, mounted read-only at /repo.")
    parser.add_argument(
        "--print-output", action="store_true",
        help="Echo the container's raw output before the verdict.")
    parser.add_argument(
        "--require-docker", action="store_true",
        help=f"Fail instead of exiting {EXIT_SKIP} (skip) when Docker is unusable. "
             "Use in release lanes, where a silent skip would let an unverified "
             "package through.")
    args = parser.parse_args(argv)

    reason = docker_unavailable_reason()
    if reason:
        if args.require_docker:
            print(f"ERROR: {reason}; --require-docker was given", file=sys.stderr)
            return 1
        print(f"SKIPPED: {reason}")
        return EXIT_SKIP

    output = run_container(args.repo.resolve(), args.image)
    if args.print_output:
        print(output)

    report = parse_report(output)
    print(format_summary(report))

    failures = evaluate(report)
    if failures:
        print("\nFAILED:")
        for failure in failures:
            print(f"  - {failure}")
        return 1
    print("\nOK: both packages are well-formed and lintian-clean")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
