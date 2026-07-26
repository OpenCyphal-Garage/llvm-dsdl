#!/usr/bin/env python3
# ===----------------------------------------------------------------------===//
#
# Part of the OpenCyphal project, under the MIT licence
# SPDX-License-Identifier: MIT
#
# ===----------------------------------------------------------------------===//

"""Install a built .deb on a pristine system and prove the tools actually work.

The package this checks is a *downloadable file*: users fetch it and run
``apt install ./llvm-dsdl_*.deb`` against stock Ubuntu, with no repository of
ours to resolve anything from. So the container here is deliberately clean --
no apt.llvm.org, no build tooling, nothing the build host had. A package that
only works where it was built is the failure this is designed to catch, and it
has caught it: the first vendored build installed cleanly and then died on a
missing ``libz3.so.4`` that the dependency list did not mention.

Checks, in order of what they would catch:

1. The package installs against stock Ubuntu -- i.e. every Depends resolves.
2. All three tools run -- i.e. the vendored libLLVM is found via RPATH.
3. Nothing is left unresolved in the dynamic linkage.
4. Generated C compiles with a stock compiler -- the whole emit path, from an
   installed binary, on a machine that has never seen the source tree.
5. A non-C backend emits too, so the check is not C-specific.

Like check_deb_config.py, the container payload is shell because it is a fixed
sequence of distribution commands; the verdict is made here, in Python.
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

SECTION_PREFIX = "@@LLVMDSDL@@ "
EXIT_SKIP = 77
TOOLS = ("dsdlc", "dsdl-opt", "dsdld")

CONTAINER_SCRIPT = r"""
set -uo pipefail
export DEBIAN_FRONTEND=noninteractive

# Ubuntu's container images are minimized: /etc/dpkg/dpkg.cfg.d/excludes tells
# dpkg to discard man pages and most documentation as packages are unpacked. A
# real Ubuntu install does no such thing, so leaving it in place would make this
# test report our own package as missing files it demonstrably ships.
rm -f /etc/dpkg/dpkg.cfg.d/excludes

apt-get update -qq >/dev/null 2>&1

echo "@@LLVMDSDL@@ install"
if apt-get install -y -qq /pkg/PACKAGE_FILE >/dev/null 2>&1; then
  echo "ok=yes"
else
  echo "ok=no"
fi

echo "@@LLVMDSDL@@ versions"
for tool in dsdlc dsdl-opt dsdld; do
  if v=$("$tool" --version 2>&1); then echo "$tool=$v"; else echo "$tool=FAILED: $v"; fi
done

echo "@@LLVMDSDL@@ linkage"
for tool in dsdlc dsdl-opt dsdld; do
  ldd "$(command -v "$tool")" 2>/dev/null | grep -i "not found" | sed "s/^/missing=/"
done
ldd "$(command -v dsdlc)" 2>/dev/null | grep -oE '/[^ ]*llvm-dsdl/libLLVM[^ ]*' | sed 's/^/vendored=/'

echo "@@LLVMDSDL@@ codegen"
apt-get install -y -qq gcc >/dev/null 2>&1
mkdir -p /tmp/ns/demo
printf 'uint16 id\nuint8[4] data\n@sealed\n' > /tmp/ns/demo/Widget.1.0.dsdl
cd /tmp
if dsdlc -l c -I ns -O gen_c ns/demo/Widget.1.0.dsdl >/dev/null 2>&1; then
  echo "c_emit=yes"
  if cc -std=c11 -Wall -I gen_c -I gen_c/ns/demo -c gen_c/ns/demo/Widget_1_0.c -o /tmp/w.o 2>/dev/null; then
    echo "c_compile=yes"
  else
    echo "c_compile=no"
  fi
else
  echo "c_emit=no"
fi
if dsdlc -l go -I ns -O gen_go ns/demo/Widget.1.0.dsdl >/dev/null 2>&1; then
  echo "go_emit=yes"
else
  echo "go_emit=no"
fi

echo "@@LLVMDSDL@@ files"
for f in /usr/share/man/man1/dsdlc.1.gz /usr/share/doc/llvm-dsdl/copyright \
         /usr/share/doc/llvm-dsdl/changelog.gz /usr/share/llvm-dsdl/llvm-dsdl-sbom.cdx.json; do
  [ -f "$f" ] && echo "present=$f" || echo "absent=$f"
done
"""


def parse(output: str) -> dict[str, list[str]]:
    """Group the container's output into {section: lines}."""
    sections: dict[str, list[str]] = {}
    current: list[str] | None = None
    for line in output.splitlines():
        if line.startswith(SECTION_PREFIX):
            current = []
            sections[line[len(SECTION_PREFIX):].strip()] = current
        elif current is not None and line.strip():
            current.append(line.strip())
    return sections


def evaluate(sections: dict[str, list[str]], expected_version: str | None) -> list[str]:
    """Return failure messages; empty means the package is sound."""
    failures: list[str] = []

    if "ok=yes" not in sections.get("install", []):
        # Nothing downstream is meaningful if the install did not happen.
        return ["package failed to install on a clean system (unsatisfied Depends?)"]

    versions = dict(
        line.split("=", 1) for line in sections.get("versions", []) if "=" in line)
    for tool in TOOLS:
        reported = versions.get(tool, "<missing>")
        if reported.startswith("FAILED") or reported == "<missing>":
            failures.append(f"{tool} did not run: {reported}")
        elif expected_version and expected_version not in reported:
            failures.append(
                f"{tool} reports {reported!r}, expected version {expected_version!r}")

    linkage = sections.get("linkage", [])
    missing = [ln.partition("=")[2] for ln in linkage if ln.startswith("missing=")]
    if missing:
        failures.append(f"unresolved shared objects: {', '.join(missing)}")
    if not any(ln.startswith("vendored=") for ln in linkage):
        failures.append(
            "dsdlc does not resolve libLLVM from the package's private libdir; "
            "the vendored copy or its RPATH is not doing its job")

    codegen = sections.get("codegen", [])
    for key, message in (
        ("c_emit=yes", "C backend did not emit from the installed binary"),
        ("c_compile=yes", "generated C did not compile with a stock compiler"),
        ("go_emit=yes", "Go backend did not emit from the installed binary"),
    ):
        if key not in codegen:
            failures.append(message)

    for line in sections.get("files", []):
        if line.startswith("absent="):
            failures.append(f"missing from the installed package: {line.partition('=')[2]}")

    return failures


def docker_unavailable_reason() -> str | None:
    docker = shutil.which("docker")
    if not docker:
        return "docker was not found on PATH"
    if subprocess.run([docker, "info"], capture_output=True, timeout=120).returncode != 0:
        return "the docker daemon is not reachable"
    return None


def run(deb: Path, image: str, platform: str | None) -> str:
    docker = shutil.which("docker")
    script = CONTAINER_SCRIPT.replace("PACKAGE_FILE", deb.name)
    argv = [docker, "run", "--rm"]
    if platform:
        argv += ["--platform", platform]
    argv += ["-v", f"{deb.parent.resolve()}:/pkg:ro", image, "bash", "-c", script]
    proc = subprocess.run(argv, capture_output=True, text=True)
    if proc.returncode != 0 and not proc.stdout:
        sys.stderr.write(proc.stderr)
        raise SystemExit(f"container run failed with exit code {proc.returncode}")
    return proc.stdout


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--deb", type=Path, required=True, help="Package to verify.")
    parser.add_argument(
        "--image", default="ubuntu:jammy",
        help="Pristine image to install into. Must NOT be the build image: the point "
             "is to prove the package works where nothing was pre-installed.")
    parser.add_argument(
        "--expect-version", help="Assert the tools report this version.")
    parser.add_argument(
        "--platform",
        help="Container platform, e.g. linux/amd64. Needed only when checking a "
             "package built for an architecture other than this host's -- in CI each "
             "runner is native and the default is correct.")
    parser.add_argument("--print-output", action="store_true")
    args = parser.parse_args(argv)

    if not args.deb.is_file():
        raise SystemExit(f"package not found: {args.deb}")

    reason = docker_unavailable_reason()
    if reason:
        print(f"SKIPPED: {reason}")
        return EXIT_SKIP

    output = run(args.deb, args.image, args.platform)
    if args.print_output:
        print(output)

    sections = parse(output)
    failures = evaluate(sections, args.expect_version)

    print(f"smoke-tested {args.deb.name} on {args.image}")
    for line in sections.get("versions", []):
        print(f"  {line}")
    for line in sections.get("linkage", []):
        if line.startswith("vendored="):
            print(f"  libLLVM <- {line.partition('=')[2]}")

    if failures:
        print("\nFAILED:")
        for failure in failures:
            print(f"  - {failure}")
        return 1
    print("\nOK: installs on a pristine system and generates compilable code")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
