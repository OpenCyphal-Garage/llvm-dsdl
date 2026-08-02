#!/usr/bin/env python3
"""Assert a built LLVM/MLIR toolchain is the one llvm-dsdl intends to link.

A toolchain that merely *builds* is not the goal: the point of building it
ourselves is that specific properties hold. Each check below corresponds to a
claim in docs/development/distribution-channels.md section 1, so a regression
in the Dockerfile fails here rather than surfacing as a mysterious dependency
in a shipped package.
"""
from __future__ import annotations

import argparse
import pathlib
import re
import subprocess
import sys

FAILURES: list[str] = []
NOTES: list[str] = []


def check(ok: bool, label: str, detail: str = "") -> None:
    print(f"  [{'PASS' if ok else 'FAIL'}] {label}{(' -- ' + detail) if detail else ''}")
    if not ok:
        FAILURES.append(label)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--prefix", default="/opt/llvm-dsdl-toolchain")
    ap.add_argument("--expect-ref", required=True, help="e.g. llvmorg-22.1.8")
    args = ap.parse_args()
    p = pathlib.Path(args.prefix)

    print("== identity ==")
    ref_file = p / "LLVM_REF"
    actual_ref = ref_file.read_text().strip() if ref_file.is_file() else "<missing>"
    check(actual_ref == args.expect_ref, "pinned revision matches", f"{actual_ref!r}")

    expect_major = re.search(r"llvmorg-(\d+)", args.expect_ref)
    print("\n== cmake packages (what find_package needs) ==")
    for rel in ("lib/cmake/llvm/LLVMConfig.cmake", "lib/cmake/mlir/MLIRConfig.cmake"):
        check((p / rel).is_file(), f"{rel} present")

    cfg = p / "lib/cmake/llvm/LLVMConfig.cmake"
    if cfg.is_file():
        text = cfg.read_text()
        m = re.search(r'set\(LLVM_VERSION_MAJOR\s+(\d+)\)', text)
        if m and expect_major:
            check(m.group(1) == expect_major.group(1),
                  "LLVM_VERSION_MAJOR matches the pin", m.group(1))
        dylib = re.search(r'set\(LLVM_LINK_LLVM_DYLIB\s+(\w+)\)', text)
        val = dylib.group(1) if dylib else "<unset>"
        check(val in ("OFF", "0", "<unset>"),
              "LLVMConfig does not force the shared LLVM", f"LLVM_LINK_LLVM_DYLIB={val}")

    print("\n== static-only (the whole point) ==")
    libdir = p / "lib"
    shared = sorted(
        f.name for f in libdir.glob("libLLVM*")
        if f.suffix in (".so", ".dylib") or ".so." in f.name
    ) if libdir.is_dir() else []
    check(not shared, "no shared libLLVM in the install", ", ".join(shared) or "none")
    archives = sorted(libdir.glob("libLLVM*.a")) if libdir.is_dir() else []
    check(len(archives) > 10, "LLVM component archives present", f"{len(archives)} archives")
    mlir_archives = sorted(libdir.glob("libMLIR*.a")) if libdir.is_dir() else []
    check(len(mlir_archives) > 10, "MLIR component archives present", f"{len(mlir_archives)} archives")

    print("\n== no target backends ==")
    backend = re.compile(
        r"libLLVM(AArch64|X86|ARM|RISCV|PowerPC|NVPTX|AMDGPU|WebAssembly|Mips|"
        r"SPARC|SystemZ|Hexagon|Lanai|MSP430|XCore|BPF|AVR|VE|LoongArch)")
    found = sorted(a.name for a in archives if backend.match(a.name))
    check(not found, "no target-backend archives", ", ".join(found[:4]) or "none")

    print("\n== tools the build and test suite require ==")
    # mlir-tblgen: include/llvmdsdl/IR/CMakeLists.txt calls mlir_tablegen() 8 times.
    # FileCheck and not: every test/lit/*.txt RUN line uses them.
    for tool in ("mlir-tblgen", "llvm-tblgen", "FileCheck", "not"):
        exe = p / "bin" / tool
        check(exe.is_file(), f"bin/{tool} installed")

    # lit is a Python package; LLVM never installs it, and a missing lit makes
    # test/lit/CMakeLists.txt warn and return -- a green run that tested nothing.
    r = subprocess.run([sys.executable, "-c", "import lit; print(lit.__version__)"],
                       capture_output=True, text=True)
    check(r.returncode == 0, "lit importable", r.stdout.strip() or r.stderr.strip()[:60])

    print("\n== runtime dependencies of the installed tools ==")
    tblgen = p / "bin" / "mlir-tblgen"
    if tblgen.is_file():
        r = subprocess.run(["ldd", str(tblgen)], capture_output=True, text=True)
        out = (r.stdout or "") + (r.stderr or "")
        forbidden = [n for n in ("libz3", "libxml2", "libedit", "libffi", "libzstd", "libz.so")
                     if n in out]
        check(not forbidden, "no optional-feature libraries linked",
              ", ".join(forbidden) or "none")
        NOTES.append("mlir-tblgen ldd:\n    " + "\n    ".join(
            l.strip() for l in out.splitlines() if l.strip())[:600])

    if NOTES:
        print("\n== notes ==")
        for n in NOTES:
            print("  " + n)

    print()
    if FAILURES:
        print(f"FAILED ({len(FAILURES)}): " + "; ".join(FAILURES))
        return 1
    print("All toolchain assertions hold.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
