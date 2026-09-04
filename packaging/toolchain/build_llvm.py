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

    # `--target-language obj` assembles inside dsdlc, so a target is emitted for
    # by naming it rather than by having a toolchain for it installed. These are
    # the backends that reach a Cyphal node or a host that builds for one; the
    # rest of LLVM's twenty are the bulk of a full build and none of them is a
    # target this compiler is asked for.
    "-DLLVM_TARGETS_TO_BUILD=X86;AArch64;ARM;RISCV;AVR;Mips;WebAssembly",

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


# macOS only, via --with-libcxx. Apple ships libc++ as a dylib only, so without
# building one there is nothing to link statically and the tools keep a second
# dynamic dependency.
#
# Do not drop the HERMETIC flags. Without them the build succeeds and otool -L
# still shows libSystem alone, but the tools crash at runtime: libSystem already
# provides libc++abi, and a non-hermetic libc++ adds a second copy. Check by
# running a tool, not by reading otool.
#
# LIBCXXABI_USE_LLVM_UNWINDER=OFF: the unwinder is in libSystem too.
LIBCXX_FLAGS = [
    "-DLLVM_ENABLE_RUNTIMES=libcxx;libcxxabi",
    "-DLIBCXXABI_USE_LLVM_UNWINDER=OFF",
    "-DLIBCXX_ENABLE_SHARED=OFF",
    "-DLIBCXXABI_ENABLE_SHARED=OFF",
    "-DLIBCXX_ENABLE_STATIC=ON",
    "-DLIBCXXABI_ENABLE_STATIC=ON",
    "-DLIBCXX_HERMETIC_STATIC_LIBRARY=ON",
    "-DLIBCXXABI_HERMETIC_STATIC_LIBRARY=ON",
    "-DLIBCXX_ENABLE_STATIC_ABI_LIBRARY=ON",
]


def run(cmd: list[str]) -> None:
    print("+ " + " ".join(cmd), flush=True)
    subprocess.run(cmd, check=True)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--source", help="llvm-project checkout")
    ap.add_argument("--build", default="/work/build")
    ap.add_argument("--prefix", default="/opt/llvm-dsdl-toolchain")
    ap.add_argument("--llvm-ref", help="tag recorded into the prefix")
    ap.add_argument("--jobs", type=int, default=0, help="0 lets ninja decide")
    ap.add_argument("--with-libcxx", action="store_true",
                    help="Also build a hermetic static libc++ into the prefix. macOS only: "
                         "it is what lets the tools link nothing but libSystem.")
    ap.add_argument("--print-config", action="store_true",
                    help="Print the effective CMake flags and exit. The Toolchain workflow "
                         "hashes this to decide whether an image needs rebuilding, so that "
                         "editing a comment here -- or adding a flag only another platform "
                         "uses -- does not invalidate images it cannot affect.")
    args = ap.parse_args()

    if args.print_config:
        for flag in CMAKE_FLAGS + (LIBCXX_FLAGS if args.with_libcxx else []):
            print(flag)
        return 0

    for required, name in ((args.source, "--source"), (args.llvm_ref, "--llvm-ref")):
        if not required:
            print(f"error: {name} is required", file=sys.stderr)
            return 1

    llvm_dir = pathlib.Path(args.source) / "llvm"
    if not (llvm_dir / "CMakeLists.txt").is_file():
        print(f"error: {llvm_dir} is not an LLVM source tree", file=sys.stderr)
        return 1

    if shutil.which("ninja") is None:
        print("error: ninja is required", file=sys.stderr)
        return 1

    run(["cmake", "-G", "Ninja", "-S", str(llvm_dir), "-B", args.build,
         f"-DCMAKE_INSTALL_PREFIX={args.prefix}", *CMAKE_FLAGS])

    jobs_args = ["-j", str(args.jobs)] if args.jobs else []

    # Generated headers first, to close a race rather than keep winning it.
    #
    # mlir/lib/ExecutionEngine/CMakeLists.txt declares MLIRExecutionEngineUtils
    # with `DEPENDS intrinsics_gen`, but its only source, OptUtils.cpp, reaches
    # llvm/Analysis/TargetLibraryInfo.inc through PassBuilder.h. That header is
    # produced by the analysis_gen target, which is not declared, so nothing
    # orders the two. (MLIR_ENABLE_EXECUTION_ENGINE=OFF does not help: it gates
    # the JIT engine, not these utilities, which are built regardless.)
    #
    # How much other work is queued decides who wins, so the outcome moves with
    # LLVM_TARGETS_TO_BUILD and with the core count: MLIR once started early
    # enough to lose on a 4-vCPU CI runner, having built on a 14-core developer
    # machine every time, with
    #
    #   fatal error: llvm/Analysis/TargetLibraryInfo.inc: No such file or directory
    #
    # Building the tablegen targets as their own pass costs seconds and makes the
    # outcome independent of how many cores happen to be available.
    run(["cmake", "--build", args.build, "--target",
         "intrinsics_gen", "analysis_gen", *jobs_args])

    run(["cmake", "--build", args.build, *jobs_args])
    run(["cmake", "--install", args.build])

    if args.with_libcxx:
        # A separate invocation rather than folded into the one above: LLVM and
        # the runtimes bootstrap in opposite directions, and two proven steps
        # are worth more here than one clever one. Same prefix, so consumers see
        # libc++.a beside the LLVM archives.
        runtimes = pathlib.Path(args.source) / "runtimes"
        if not (runtimes / "CMakeLists.txt").is_file():
            print(f"error: {runtimes} is not a runtimes source tree", file=sys.stderr)
            return 1
        rt_build = args.build + "-runtimes"
        run(["cmake", "-G", "Ninja", "-S", str(runtimes), "-B", rt_build,
             "-DCMAKE_BUILD_TYPE=Release",
             f"-DCMAKE_INSTALL_PREFIX={args.prefix}", *LIBCXX_FLAGS])
        rt_cmd = ["cmake", "--build", rt_build]
        if args.jobs:
            rt_cmd += ["-j", str(args.jobs)]
        run(rt_cmd)
        run(["cmake", "--install", rt_build])

    # Record what this is, so a prefix found in a cache or an image can be
    # identified without re-deriving it from a tag.
    prefix = pathlib.Path(args.prefix)
    prefix.mkdir(parents=True, exist_ok=True)
    (prefix / "LLVM_REF").write_text(args.llvm_ref + "\n")
    print(f"installed {args.llvm_ref} to {args.prefix}", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
