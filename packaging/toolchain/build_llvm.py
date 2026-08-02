#!/usr/bin/env python3
"""Configure, build and install the LLVM/MLIR toolchain llvm-dsdl links against.

The configuration lives here rather than in the Dockerfiles because there is
more than one flavour of it -- musl for the `bin` component, glibc for
`llvm-dsdl-dev` and the CI toolshed -- and a flag that drifts between them
produces two toolchains that are not the same toolchain. The Dockerfiles supply
a base image and a compiler; this supplies the decisions.

Each decision is justified in docs/development/distribution-channels.md section 1.
Verify the result with verify_toolchain.py, which asserts these settings held.
"""
from __future__ import annotations

import argparse
import os
import pathlib
import shutil
import subprocess
import sys

# Every flag here is load-bearing. Grouped by why it is set.
CMAKE_FLAGS = [
    "-DCMAKE_BUILD_TYPE=Release",

    # MLIR is the only project we need. Note this still builds every MLIR
    # dialect -- XeGPU, NVVM, ArmSVE and the rest -- because MLIR has no
    # dialect-subsetting knob. We link 13 of them. That is now the dominant
    # cost of this build, not LLVM.
    "-DLLVM_ENABLE_PROJECTS=mlir",

    # Required, and the one setting here that adds rather than removes. LLVM
    # defaults it OFF; apt.llvm.org and Homebrew both ship it ON, so the
    # project has always depended on it without declaring it. llvmdsdl derives
    # from mlir::Dialect and mlir::Pass, and deriving from them emits typeinfo
    # references an -fno-rtti LLVM does not contain. Building it OFF fails at
    # link with "undefined reference to typeinfo for mlir::Pass", not at
    # compile. Costs ~31 MB of the install prefix.
    "-DLLVM_ENABLE_RTTI=ON",

    # dsdlc emits source text, not machine code, and no target backend is
    # reachable from the MLIR libraries we link. This removes the bulk of an
    # LLVM build: 4,861 ninja edges against 30,000-plus for a full one.
    "-DLLVM_TARGETS_TO_BUILD=",

    # The point of the exercise: no shared libLLVM to vendor, sign or resolve.
    "-DLLVM_BUILD_LLVM_DYLIB=OFF",
    "-DLLVM_LINK_LLVM_DYLIB=OFF",

    # Optional features whose only effect here is to drag shared libraries into
    # the link. Together these account for the whole derived Depends list of
    # the current .deb bar libc6 and libstdc++6.
    #
    # There is deliberately no LLVM_ENABLE_TERMINFO: LLVM 22 does not define
    # it, and passing it earns a "Manually-specified variables were not used"
    # warning rather than an effect. Terminfo support was removed upstream.
    "-DLLVM_ENABLE_Z3_SOLVER=OFF",
    "-DLLVM_ENABLE_LIBEDIT=OFF",
    "-DLLVM_ENABLE_LIBXML2=OFF",
    "-DLLVM_ENABLE_ZLIB=OFF",
    "-DLLVM_ENABLE_ZSTD=OFF",
    "-DLLVM_ENABLE_FFI=OFF",
    "-DLLVM_ENABLE_PLUGINS=OFF",
    "-DLLVM_ENABLE_ASSERTIONS=OFF",

    "-DLLVM_INCLUDE_TESTS=OFF",
    "-DLLVM_INCLUDE_BENCHMARKS=OFF",
    "-DLLVM_INCLUDE_EXAMPLES=OFF",

    # Worth 1.5 GB. Without a shared libLLVM every tool statically links the
    # world, so mlir-opt alone reaches 200 MB. Nothing here uses llc, opt, lli
    # or mlir-opt: dsdlc and dsdl-opt are our own binaries, and the lit suite
    # substitutes only those two plus FileCheck and not.
    "-DLLVM_BUILD_TOOLS=OFF",

    # Installs FileCheck and not, which every test/lit RUN line uses. It does
    # not install lit -- that is a Python package the images add separately,
    # and without it test/lit/CMakeLists.txt warns, returns, and the suite
    # reports green having run nothing.
    "-DLLVM_INSTALL_UTILS=ON",

    "-DMLIR_ENABLE_EXECUTION_ENGINE=OFF",
    "-DMLIR_INCLUDE_TESTS=OFF",
]


def run(cmd: list[str]) -> None:
    print("+ " + " ".join(cmd), flush=True)
    subprocess.run(cmd, check=True)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--source", required=True, help="llvm-project checkout")
    ap.add_argument("--build", default="/work/build")
    ap.add_argument("--prefix", default="/opt/llvm-dsdl-toolchain")
    ap.add_argument("--llvm-ref", required=True, help="tag recorded into the prefix")
    ap.add_argument("--jobs", type=int, default=0, help="0 lets ninja decide")
    args = ap.parse_args()

    llvm_dir = pathlib.Path(args.source) / "llvm"
    if not (llvm_dir / "CMakeLists.txt").is_file():
        print(f"error: {llvm_dir} is not an LLVM source tree", file=sys.stderr)
        return 1

    if shutil.which("ninja") is None:
        print("error: ninja is required", file=sys.stderr)
        return 1

    run(["cmake", "-G", "Ninja", "-S", str(llvm_dir), "-B", args.build,
         f"-DCMAKE_INSTALL_PREFIX={args.prefix}", *CMAKE_FLAGS])

    build_cmd = ["cmake", "--build", args.build]
    if args.jobs:
        build_cmd += ["-j", str(args.jobs)]
    run(build_cmd)
    run(["cmake", "--install", args.build])

    # Record what this is, so a prefix found in a cache or an image can be
    # identified without re-deriving it from a tag.
    prefix = pathlib.Path(args.prefix)
    prefix.mkdir(parents=True, exist_ok=True)
    (prefix / "LLVM_REF").write_text(args.llvm_ref + "\n")
    print(f"installed {args.llvm_ref} to {args.prefix}", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
