#!/usr/bin/env python3
"""Acceptance gates for direct MLIR-to-object lowering in the `obj` target language.

Each gate asserts one property that a lowering through LLVM IR has and a generator that
writes C source and drives a host compiler does not. They are written against a tree where
`--target-language obj` is unimplemented, and they report NOT_IMPLEMENTED there.

A gate reports one of three states:

  PASS             the property holds
  NOT_IMPLEMENTED  the lowering is absent, so the property has nothing to hold over
  FAIL             the lowering answered and the property does not hold

NOT_IMPLEMENTED is a passing exit unless --strict is given, so the suite stays green until
the backend exists and turns red the moment it exists and is wrong. A gate never reports
NOT_IMPLEMENTED because a probe crashed or answered unexpectedly: those are FAIL, so that a
gate cannot be satisfied by breaking the thing it measures.
"""

from __future__ import annotations

import argparse
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import List, Optional

# `e_machine` for the cross-target gate, from the ELF psABI register.
EM_RISCV = 243
ELFCLASS32 = 1

NOT_IMPLEMENTED_MARKER = "is not implemented"


class GateFailure(Exception):
    """The lowering answered and the asserted property does not hold."""


class NotImplementedYet(Exception):
    """The lowering is absent."""


@dataclass
class Run:
    code: int
    out: str
    stdout: str = ""

    @property
    def says_unimplemented(self) -> bool:
        return NOT_IMPLEMENTED_MARKER in self.out


def run(argv: List[str], env: Optional[dict] = None, cwd: Optional[Path] = None) -> Run:
    try:
        finished = subprocess.run(
            argv,
            capture_output=True,
            text=True,
            check=False,
            env=env,
            cwd=None if cwd is None else str(cwd),
        )
    except OSError as problem:
        raise GateFailure(f"could not run {argv[0]}: {problem}") from problem
    return Run(finished.returncode, finished.stdout + finished.stderr, finished.stdout)


def obj_command(args: argparse.Namespace, outdir: Path, extra: Optional[List[str]] = None) -> List[str]:
    """The obj invocation, carrying whatever flags this lane's CLI requires.

    The gates assert what comes out of the lane, not what its flags are spelled like, so the
    caller supplies the lane's own options through --obj-arg.
    """
    argv = [args.dsdlc, "--target-language", "obj"]
    argv += list(args.obj_arg)
    argv += list(extra or [])
    argv += ["--outdir", str(outdir), str(args.fixtures)]
    return argv


def require_obj_lane(args: argparse.Namespace, workdir: Path) -> None:
    """Raise NotImplementedYet when dsdlc has no `obj` lane, GateFailure when it misbehaves."""
    result = run(obj_command(args, workdir / "probe"))
    if result.says_unimplemented:
        raise NotImplementedYet("dsdlc reports --target-language obj is not implemented")
    if result.code != 0:
        raise GateFailure(
            f"the obj lane is present but failed the probe run (exit {result.code}):\n"
            f"{result.out.strip()}\n"
            "  (if the lane needs options, pass them with --obj-arg)")


def object_files(root: Path) -> List[Path]:
    return sorted(p for p in root.rglob("*.o") if p.is_file())


# ---------------------------------------------------------------------------------------
# Gate 1 -- the IR carries the serialisation, not a string holding C that expresses it.
# ---------------------------------------------------------------------------------------

def gate_ir_has_no_emitc(args: argparse.Namespace, workdir: Path) -> str:
    schema = run([args.dsdlc, "--target-language", "mlir", str(args.fixtures)])
    if schema.code != 0:
        raise GateFailure(f"could not produce dsdl IR to lower:\n{schema.out.strip()}")
    # The IR alone: dsdlc reports its run on stderr, and a summary pasted onto the end of a
    # module is not a module.
    source = workdir / "schema.mlir"
    source.write_text(schema.stdout, encoding="utf-8")

    lowered = run([args.dsdl_opt, "--lower-dsdl-exec", "--build-dsdl-plan-bodies",
                   "--convert-dsdl-to-llvm", str(source)])
    if lowered.code != 0:
        if "convert-dsdl-to-llvm" in lowered.out and (
                "Unknown command line argument" in lowered.out or "unknown pass" in lowered.out.lower()):
            raise NotImplementedYet("dsdl-opt has no --convert-dsdl-to-llvm pass")
        raise GateFailure(f"--convert-dsdl-to-llvm failed:\n{lowered.out.strip()}")

    # A body the builder never produced is the object lane not existing, not the conversion
    # being wrong. Told apart here so that the gate reports the one that is true.
    if "_ir_" not in lowered.out:
        raise NotImplementedYet(
            "the pipeline produced no plan bodies to convert; the object lane does not yet "
            "supply the member names a body is built from")

    emitc = lowered.out.count("emitc.")
    residual = len(re.findall(r"\bdsdl\.[a-z_]+", lowered.out))
    llvm_ops = lowered.out.count("llvm.")
    if emitc:
        verbatim = lowered.out.count("emitc.verbatim")
        raise GateFailure(
            f"{emitc} emitc op(s) survived the LLVM conversion, {verbatim} of them emitc.verbatim; "
            "serialisation expressed as C text is not lowered")
    if residual:
        # Counting emitc alone would pass a conversion that simply declined to touch the
        # plan: what is left behind is dsdl, not emitc, and the module is no closer to an
        # object for it.
        kinds = sorted(set(re.findall(r"\bdsdl\.[a-z_]+", lowered.out)))
        raise GateFailure(
            f"{residual} dsdl op(s) were left unconverted: {', '.join(kinds[:6])}; "
            "a plan that is still partly in its own dialect has not been lowered")
    if llvm_ops == 0:
        raise GateFailure("the conversion produced no llvm dialect ops")
    return f"no emitc or dsdl ops remain, {llvm_ops} llvm dialect ops present"


# ---------------------------------------------------------------------------------------
# Gate 2 -- objects come out with no C compiler anywhere on PATH.
# ---------------------------------------------------------------------------------------

def gate_emits_without_c_compiler(args: argparse.Namespace, workdir: Path) -> str:
    require_obj_lane(args, workdir)

    empty_path = workdir / "empty-path"
    empty_path.mkdir(parents=True, exist_ok=True)
    env = {k: v for k, v in os.environ.items() if k not in ("CC", "CXX", "OBJC")}
    env["PATH"] = str(empty_path)

    outdir = workdir / "out"
    result = run(obj_command(args, outdir), env=env)
    if result.code != 0:
        raise GateFailure(
            f"emission failed with no compiler on PATH (exit {result.code}):\n{result.out.strip()}")
    produced = object_files(outdir)
    if not produced:
        raise GateFailure("emission reported success but wrote no object files")
    return f"{len(produced)} object file(s) emitted with PATH={empty_path} and CC unset"


# ---------------------------------------------------------------------------------------
# Gate 3 -- no C is written, to the output tree or to a temporary directory.
# ---------------------------------------------------------------------------------------

def gate_writes_no_c_intermediates(args: argparse.Namespace, workdir: Path) -> str:
    require_obj_lane(args, workdir)

    scratch = workdir / "tmp"
    scratch.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ)
    env["TMPDIR"] = str(scratch)

    outdir = workdir / "out"
    result = run(obj_command(args, outdir), env=env)
    if result.code != 0:
        raise GateFailure(f"emission failed (exit {result.code}):\n{result.out.strip()}")

    sources = sorted(p.relative_to(outdir) for p in outdir.rglob("*.c"))
    if sources:
        raise GateFailure(
            f"{len(sources)} C source file(s) under the output directory, e.g. {sources[0]}")

    staging = sorted(p.relative_to(outdir) for p in outdir.rglob(".obj_stage*"))
    if staging:
        raise GateFailure(f"staging tree present: {staging[0]}")

    leaked = sorted(p.relative_to(scratch) for p in scratch.rglob("*") if p.is_file())
    if leaked:
        raise GateFailure(
            f"{len(leaked)} file(s) written to TMPDIR, e.g. {leaked[0]}; "
            "C staged outside the output tree is still C")

    headers = len(list(outdir.rglob("*.h")))
    return f"no .c anywhere, no staging tree, TMPDIR untouched; {headers} published header(s)"


# ---------------------------------------------------------------------------------------
# Gate 4 -- a foreign target, on a host with no toolchain for it.
# ---------------------------------------------------------------------------------------

def gate_cross_target_without_toolchain(args: argparse.Namespace, workdir: Path) -> str:
    require_obj_lane(args, workdir)

    triple = "riscv32-unknown-elf"
    outdir = workdir / "out"
    result = run(obj_command(args, outdir, ["--target-triple", triple]))
    if result.code != 0:
        raise GateFailure(f"emission for {triple} failed (exit {result.code}):\n{result.out.strip()}")

    produced = object_files(outdir)
    if not produced:
        raise GateFailure(f"emission for {triple} wrote no object files")

    sample = produced[0]
    header = sample.read_bytes()[:20]
    if len(header) < 20 or header[:4] != b"\x7fELF":
        raise GateFailure(f"{sample.name} is not an ELF object; a {triple} object must be ELF")
    elf_class = header[4]
    little_endian = header[5] == 1
    (machine, ) = struct.unpack("<H" if little_endian else ">H", header[18:20])
    if machine != EM_RISCV:
        raise GateFailure(
            f"{sample.name} has e_machine {machine}, expected {EM_RISCV} (EM_RISCV); "
            "the object was built for the wrong target")
    if elf_class != ELFCLASS32:
        raise GateFailure(f"{sample.name} has EI_CLASS {elf_class}, expected {ELFCLASS32} (32-bit)")
    return f"{len(produced)} ELF object(s) for {triple}, e_machine=EM_RISCV, 32-bit"


# ---------------------------------------------------------------------------------------
# Gate 5 -- the object links and produces the bytes the C lane produces.
# ---------------------------------------------------------------------------------------

DRIVER_C = r"""
#include <stdio.h>
#include <string.h>
#include "fixtures/vendor/Widget_1_0.h"

/* Serialises a spread of values and prints the wire bytes, so two implementations of the
   same schema can be compared by their output rather than by inspection. */
int main(void)
{
    static const unsigned cases[][2] = {
        {0u, 0u}, {1u, 1u}, {255u, 65535u}, {0u, 65535u}, {255u, 0u}, {0x5Au, 0xA5A5u},
    };
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        fixtures__vendor__Widget obj;
        memset(&obj, 0, sizeof(obj));
        obj.foo = (uint8_t)cases[i][0];
        obj.bar = (uint16_t)cases[i][1];

        uint8_t buffer[fixtures__vendor__Widget_SERIALIZATION_BUFFER_SIZE_BYTES_];
        memset(buffer, 0, sizeof(buffer));
        size_t size = sizeof(buffer);
        const int8_t rc = fixtures__vendor__Widget__serialize_(&obj, buffer, &size);
        if (rc < 0) {
            printf("case %zu: error %d\n", i, (int)rc);
            continue;
        }
        printf("case %zu:", i);
        for (size_t b = 0u; b < size; ++b) {
            printf(" %02X", buffer[b]);
        }
        printf("\n");

        fixtures__vendor__Widget back;
        memset(&back, 0, sizeof(back));
        size_t consumed = size;
        const int8_t rd = fixtures__vendor__Widget__deserialize_(&back, buffer, &consumed);
        printf("  back: rc=%d foo=%u bar=%u\n", (int)rd, (unsigned)back.foo, (unsigned)back.bar);
    }
    return 0;
}
"""


def _build_reference(args: argparse.Namespace, workdir: Path) -> str:
    """Generate, compile and run the C lane; return its wire-byte transcript."""
    refdir = workdir / "reference"
    generated = run(
        [args.dsdlc, "--target-language", "c", "--outdir", str(refdir), str(args.fixtures)])
    if generated.code != 0:
        raise GateFailure(f"the C reference lane failed to generate:\n{generated.out.strip()}")

    driver = workdir / "driver.c"
    driver.write_text(DRIVER_C, encoding="utf-8")
    binary = workdir / "reference_driver"
    compiled = run([
        args.cc, "-std=c11", "-I",
        str(refdir), "-o",
        str(binary),
        str(driver),
        str(refdir / "fixtures" / "vendor" / "Widget_1_0.c")
    ])
    if compiled.code != 0:
        raise GateFailure(f"the C reference driver failed to build:\n{compiled.out.strip()}")

    executed = run([str(binary)])
    if executed.code != 0:
        raise GateFailure(f"the C reference driver failed to run:\n{executed.out.strip()}")
    return executed.out


def gate_object_matches_c_lane(args: argparse.Namespace, workdir: Path) -> str:
    reference = _build_reference(args, workdir)
    if not reference.strip():
        raise GateFailure("the C reference driver produced no output to compare against")

    require_obj_lane(args, workdir)

    objdir = workdir / "subject"
    generated = run(obj_command(args, objdir))
    if generated.code != 0:
        raise GateFailure(f"the obj lane failed to generate:\n{generated.out.strip()}")

    widget = objdir / "fixtures" / "vendor" / "Widget_1_0.o"
    if not widget.is_file():
        raise GateFailure(f"the obj lane published no object for Widget at {widget}")

    driver = workdir / "driver.c"
    binary = workdir / "subject_driver"
    linked = run([
        args.cc, "-std=c11", "-I",
        str(objdir), "-o",
        str(binary),
        str(driver),
        str(widget)
    ] + [str(p) for p in object_files(objdir) if p != widget])
    if linked.code != 0:
        raise GateFailure(f"the emitted object did not link into a driver:\n{linked.out.strip()}")

    executed = run([str(binary)])
    if executed.code != 0:
        raise GateFailure(f"the emitted object crashed or errored when called:\n{executed.out.strip()}")

    if executed.out != reference:
        diff = []
        for line_no, (a, b) in enumerate(zip(reference.splitlines(), executed.out.splitlines()), 1):
            if a != b:
                diff.append(f"    line {line_no}: c lane {a!r} vs obj lane {b!r}")
        detail = "\n".join(diff[:6]) or "    (output lengths differ)"
        raise GateFailure("the emitted object does not agree with the C lane:\n" + detail)
    return f"{len(reference.splitlines())} transcript line(s) identical to the C lane"


# ---------------------------------------------------------------------------------------
# Gate 6 -- every entry point a published header declares, its object defines.
# ---------------------------------------------------------------------------------------

# `int8_t <type>__serialize_ir_(` as the header declares the body the object must define.
ENTRY_POINT = re.compile(r"^int8_t\s+(\w+_ir_)\s*\(", re.M)

SHT_SYMTAB = 2
SHN_UNDEF = 0


def elf_defined_symbols(path: Path) -> set:
    """The names an ELF relocatable object defines, read from its symbol table.

    A symbol with a section index is defined here; one at SHN_UNDEF is a reference to
    something another object has to supply.
    """
    data = path.read_bytes()
    if data[:4] != b"\x7fELF":
        raise GateFailure(f"{path.name} is not an ELF object")
    is_32 = data[4] == ELFCLASS32
    order = "<" if data[5] == 1 else ">"
    if is_32:
        (shoff, ) = struct.unpack(order + "I", data[32:36])
        shentsize, shnum = struct.unpack(order + "HH", data[46:50])
    else:
        (shoff, ) = struct.unpack(order + "Q", data[40:48])
        shentsize, shnum = struct.unpack(order + "HH", data[58:62])

    sections = []
    for index in range(shnum):
        at = shoff + index * shentsize
        if is_32:
            _, kind, _, _, offset, size, link, _, _, entsize = struct.unpack(order + "IIIIIIIIII", data[at:at + 40])
        else:
            _, kind, _, _, offset, size, link, _, _, entsize = struct.unpack(order + "IIQQQQIIQQ", data[at:at + 64])
        sections.append((kind, offset, size, link, entsize))

    defined = set()
    for kind, offset, size, link, entsize in sections:
        if kind != SHT_SYMTAB or entsize == 0:
            continue
        _, names_at, names_size, _, _ = sections[link]
        names = data[names_at:names_at + names_size]
        for index in range(size // entsize):
            at = offset + index * entsize
            if is_32:
                st_name, _, _, _, _, st_shndx = struct.unpack(order + "IIIBBH", data[at:at + 16])
            else:
                st_name, _, _, st_shndx, _, _ = struct.unpack(order + "IBBHQQ", data[at:at + 24])
            if st_shndx == SHN_UNDEF:
                continue
            end = names.index(b"\0", st_name)
            if end > st_name:
                defined.add(names[st_name:end].decode("ascii", "replace"))
    return defined


def gate_object_defines_every_entry_point(args: argparse.Namespace, workdir: Path) -> str:
    """A header's `_ir_` prototypes are promises its object has to keep.

    An object that lacks one links nowhere, and dsdlc reporting success over it is the
    failure this gate exists to catch. The target is ELF on every host, so one symbol-table
    reader covers them all.
    """
    require_obj_lane(args, workdir)

    triple = "riscv32-unknown-elf"
    outdir = workdir / "out"
    result = run(obj_command(args, outdir, ["--target-triple", triple]))
    if result.code != 0:
        raise GateFailure(f"emission for {triple} failed (exit {result.code}):\n{result.out.strip()}")

    objects = object_files(outdir)
    if not objects:
        raise GateFailure("emission wrote no object files")

    checked = 0
    for obj in objects:
        header = obj.with_suffix(".h")
        if not header.is_file():
            raise GateFailure(f"{obj.relative_to(outdir)} has no header beside it")
        declared = set(ENTRY_POINT.findall(header.read_text(encoding="utf-8")))
        if not declared:
            raise GateFailure(f"{header.relative_to(outdir)} declares no entry points")
        missing = sorted(declared - elf_defined_symbols(obj))
        if missing:
            more = f" and {len(missing) - 1} more" if len(missing) > 1 else ""
            raise GateFailure(
                f"{obj.relative_to(outdir)} does not define {missing[0]}{more}, which "
                f"{header.name} declares; the object would fail to link")
        checked += len(declared)
    return f"{checked} entry point(s) across {len(objects)} object(s) are defined where their headers declare them"


# ---------------------------------------------------------------------------------------

GATES = {
    1: ("ir-has-no-emitc", gate_ir_has_no_emitc),
    2: ("emits-without-c-compiler", gate_emits_without_c_compiler),
    3: ("writes-no-c-intermediates", gate_writes_no_c_intermediates),
    4: ("cross-target-without-toolchain", gate_cross_target_without_toolchain),
    5: ("object-matches-c-lane", gate_object_matches_c_lane),
    6: ("object-defines-every-entry-point", gate_object_defines_every_entry_point),
}


def selftest(args: argparse.Namespace, workdir: Path) -> int:
    """Exercise gate 5's harness against the C lane on both sides.

    Running the reference against itself proves the machinery -- generate, compile, link,
    run, compare -- independently of the lane it measures.
    """
    first = _build_reference(args, workdir / "a")
    second = _build_reference(args, workdir / "b")
    if not first.strip():
        print("SELFTEST: FAIL -- the reference driver produced no output")
        return 1
    if first != second:
        print("SELFTEST: FAIL -- the reference driver is not reproducible")
        return 1
    print(f"SELFTEST: PASS -- harness builds, links, runs and compares "
          f"({len(first.splitlines())} transcript lines, reproducible)")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--gate", type=int, choices=sorted(GATES), help="Which gate to run.")
    parser.add_argument("--selftest",
                        action="store_true",
                        help="Prove gate 5's harness against the C lane on both sides.")
    parser.add_argument("--dsdlc", required=True)
    parser.add_argument("--dsdl-opt", default="")
    parser.add_argument("--fixtures", required=True, type=Path)
    parser.add_argument("--cc", default=os.environ.get("CC", "cc"))
    parser.add_argument("--obj-arg",
                        action="append",
                        default=[],
                        metavar="FLAG",
                        help="Extra flag for the obj lane; repeat for several.")
    parser.add_argument("--strict",
                        action="store_true",
                        help="Treat NOT_IMPLEMENTED as a failure.")
    parser.add_argument("--workdir", type=Path, help="Scratch directory; a temporary one by default.")
    args = parser.parse_args()

    if not args.selftest and args.gate is None:
        parser.error("one of --gate or --selftest is required")

    holder = None
    if args.workdir:
        workdir = args.workdir
        if workdir.exists():
            shutil.rmtree(workdir)
        workdir.mkdir(parents=True)
    else:
        holder = tempfile.TemporaryDirectory()
        workdir = Path(holder.name)

    try:
        if args.selftest:
            return selftest(args, workdir)

        name, gate = GATES[args.gate]
        label = f"GATE {args.gate} {name}"
        try:
            detail = gate(args, workdir)
        except NotImplementedYet as absent:
            print(f"{label}: NOT_IMPLEMENTED -- {absent}")
            if args.strict:
                print(f"{label}: strict mode, an absent lowering is a failure")
                return 1
            return 0
        except GateFailure as failure:
            print(f"{label}: FAIL -- {failure}")
            return 1
        print(f"{label}: PASS -- {detail}")
        return 0
    finally:
        if holder is not None:
            holder.cleanup()


if __name__ == "__main__":
    sys.exit(main())
