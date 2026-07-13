#!/usr/bin/env python3
# Part of the OpenCyphal project, under the MIT licence
# SPDX-License-Identifier: MIT
"""
Emit-order verifier (the golden step-trace check).

Runs dsdlc over a set of DSDL fixtures for each string backend (rust/go/cpp/ts/python)
with the LLVMDSDL_EMIT_TRACE side channel enabled, then verifies each backend's abstract
op-trace is a MEMBER of the proven-safe equivalence class -- the same serialize/deserialize
ordering the Dafny oracle (spec/dafny/CyphalSerdes.dfy) proves for the canonical traces.

It checks two things:

  1. Equivalence-class membership (per backend, the safety property):
       serialize tag   : VALIDATE_TAG before MASK_TAG before WRITE_TAG
       deserialize tag : READ_TAG before MASK_TAG before VALIDATE_TAG  (before SWITCH)
       serialize len   : LEN_VALIDATE before LEN_WRITE
       deserialize len : LEN_READ before LEN_VALIDATE  (before ELEM_LOOP)
       each write op is immediately followed by ADVANCE
     These are tolerant of where the *bookkeeping* ops (ADVANCE / STORE_TAG) sit, which is
     exactly why the safe D4 backends (TS/Python advance-early, Python validate-before-store)
     pass while a genuine mask-before-validate reorder fails.

  2. Cross-backend skeleton agreement (the consistency property):
     after removing the accepted-difference ops (ADVANCE, STORE_TAG, ALIGN, LEN_CHECK -- the
     D2/D3/D4 degrees of freedom + the native-only trailing byte-align), every backend must
     produce the identical wire-shape op sequence.

Exit code is non-zero if any backend violates membership or the skeletons disagree.
Run with --selftest for the negative control (a mask-before-validate trace must be rejected).
"""
import argparse
import os
import subprocess
import sys
import tempfile

BACKENDS = ["rust", "go", "cpp", "ts", "python"]

# Ops whose position is a free bookkeeping/accepted-difference degree of freedom. Removed
# before the cross-backend skeleton comparison; NOT used to relax the membership ordering.
SKELETON_DROP = {"ADVANCE", "STORE_TAG", "ALIGN", "LEN_CHECK"}

# Contiguous op groups that make up a tag / length prologue (scanned backward from the
# dispatch/loop op). Bookkeeping ops are allowed to appear interleaved.
TAG_GROUP = {"VALIDATE_TAG", "MASK_TAG", "WRITE_TAG", "READ_TAG", "STORE_TAG", "ADVANCE"}
LEN_GROUP = {"LEN_VALIDATE", "LEN_WRITE", "LEN_READ", "LEN_CHECK", "ADVANCE"}

WRITE_OPS = {
    "WRITE_TAG", "LEN_WRITE",
    "WRITE_SCALAR_BOOL", "WRITE_SCALAR_UINT", "WRITE_SCALAR_SINT", "WRITE_SCALAR_FLOAT",
}


def parse_trace(path):
    """Read a trace file into a list of op-name strings (payloads dropped)."""
    ops = []
    with open(path, "r", encoding="utf-8") as handle:
        for line in handle:
            line = line.strip()
            if line:
                ops.append(line.split()[0])
    return ops


def _prologue_before(ops, index, group):
    """The contiguous run of `group` ops immediately preceding ops[index], in order."""
    j = index - 1
    run = []
    while j >= 0 and ops[j] in group:
        run.append(ops[j])
        j -= 1
    return list(reversed(run))


def _ordered(seq, *needles):
    """True iff every needle is present in seq and they occur in the given order."""
    if any(n not in seq for n in needles):
        return False
    idxs = [seq.index(n) for n in needles]
    return all(a < b for a, b in zip(idxs, idxs[1:]))


def check_membership(ops):
    """Return a list of equivalence-class violations for one backend's trace."""
    violations = []

    for i, op in enumerate(ops):
        if op == "SWITCH":
            grp = _prologue_before(ops, i, TAG_GROUP)
            if "WRITE_TAG" in grp:  # serialize dispatch
                if not _ordered(grp, "VALIDATE_TAG", "MASK_TAG", "WRITE_TAG"):
                    violations.append(
                        f"serialize tag @{i}: need VALIDATE_TAG<MASK_TAG<WRITE_TAG, got {grp}")
            elif "READ_TAG" in grp:  # deserialize dispatch
                if not _ordered(grp, "READ_TAG", "MASK_TAG", "VALIDATE_TAG"):
                    violations.append(
                        f"deserialize tag @{i}: need READ_TAG<MASK_TAG<VALIDATE_TAG, got {grp}")

        if op == "ELEM_LOOP":
            grp = _prologue_before(ops, i, LEN_GROUP)
            if "LEN_WRITE" in grp:  # serialize
                if not _ordered(grp, "LEN_VALIDATE", "LEN_WRITE"):
                    violations.append(
                        f"serialize len @{i}: need LEN_VALIDATE<LEN_WRITE, got {grp}")
            elif "LEN_READ" in grp:  # deserialize
                if not _ordered(grp, "LEN_READ", "LEN_VALIDATE"):
                    violations.append(
                        f"deserialize len @{i}: need LEN_READ<LEN_VALIDATE, got {grp}")

    for i, op in enumerate(ops):
        if op in WRITE_OPS:
            nxt = ops[i + 1] if i + 1 < len(ops) else "<eof>"
            if nxt != "ADVANCE":
                violations.append(f"{op} @{i}: must be immediately followed by ADVANCE, got {nxt}")

    return violations


def skeleton(ops):
    """The wire-shape op sequence: bookkeeping / accepted-difference ops removed."""
    return [op for op in ops if op not in SKELETON_DROP]


def run_dsdlc(dsdlc, lang, fixture_root, trace_path, out_dir):
    """Generate `lang` from fixture_root with tracing on; return (ok, stderr)."""
    cmd = [dsdlc, "--target-language", lang, fixture_root, "--outdir", out_dir]
    if lang == "cpp":
        cmd += ["--cpp-profile", "std"]   # a single flavor -> a single, non-doubled trace
    if lang == "ts":
        cmd += ["--ts-module", "emit_order_verifier"]
    env = dict(os.environ, LLVMDSDL_EMIT_TRACE=trace_path)
    proc = subprocess.run(cmd, env=env, capture_output=True, text=True)
    return proc.returncode == 0, proc.stderr


FIXTURES = {
    # A tagged union: exercises the tag prologue (and the D4 advance/store variations).
    "vaultwidget/Union.1.0.dsdl": "@union\nuint8 first\nuint16 second\n@sealed\n",
    # A struct with a variable-length array: exercises the length prologue + element loop.
    "vaultwidget/VarArray.1.0.dsdl": "uint8[<=4] items\n@sealed\n",
    # A struct with a fixed-length array + scalars: exercises LenCheck (D2) + scalar writes.
    "vaultwidget/FixedArray.1.0.dsdl": "uint16[3] samples\nuint8 tag\n@sealed\n",
}


def verify_fixtures(dsdlc, workdir):
    fixture_root = os.path.join(workdir, "dsdl")
    for rel, body in FIXTURES.items():
        path = os.path.join(fixture_root, rel)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8") as handle:
            handle.write(body)

    failures = []
    traces = {}
    for lang in BACKENDS:
        trace_path = os.path.join(workdir, f"trace_{lang}.txt")
        out_dir = os.path.join(workdir, f"out_{lang}")
        ok, stderr = run_dsdlc(dsdlc, lang, fixture_root, trace_path, out_dir)
        if not ok:
            failures.append(f"[{lang}] dsdlc failed:\n{stderr}")
            continue
        if not os.path.exists(trace_path):
            failures.append(f"[{lang}] no trace emitted (side channel not wired?)")
            continue
        ops = parse_trace(trace_path)
        traces[lang] = ops
        for v in check_membership(ops):
            failures.append(f"[{lang}] equivalence-class violation: {v}")
        print(f"  {lang:7s} {len(ops):4d} ops  membership: "
              f"{'OK' if not check_membership(ops) else 'FAIL'}")

    # Cross-backend skeleton agreement.
    skeletons = {lang: skeleton(ops) for lang, ops in traces.items()}
    if len(skeletons) > 1:
        ref_lang = "rust" if "rust" in skeletons else next(iter(skeletons))
        ref = skeletons[ref_lang]
        for lang, sk in skeletons.items():
            if sk != ref:
                # Show the first divergence for diagnosis.
                diff = next((i for i, (a, b) in enumerate(zip(sk, ref)) if a != b), min(len(sk), len(ref)))
                failures.append(
                    f"[{lang}] skeleton disagrees with {ref_lang} at op {diff}: "
                    f"{sk[max(0,diff-2):diff+3]} vs {ref[max(0,diff-2):diff+3]}")
        print(f"  skeleton agreement: {'OK' if not any('skeleton' in f for f in failures) else 'FAIL'} "
              f"({len(ref)} wire ops, ref={ref_lang})")

    return failures


def selftest():
    """Negative control: the membership check must reject a mask-before-validate reorder."""
    good = ["READ_TAG", "MASK_TAG", "STORE_TAG", "VALIDATE_TAG", "ADVANCE", "SWITCH"]
    bad = ["READ_TAG", "STORE_TAG", "VALIDATE_TAG", "MASK_TAG", "ADVANCE", "SWITCH"]  # mask after validate
    good_v = check_membership(good)
    bad_v = check_membership(bad)
    print(f"  good trace violations: {good_v}")
    print(f"  bad  trace violations: {bad_v}")
    if good_v:
        print("SELFTEST FAIL: a safe trace was rejected")
        return 1
    if not bad_v:
        print("SELFTEST FAIL: a mask-after-validate trace was NOT rejected (check has no teeth)")
        return 1
    print("SELFTEST OK: safe trace accepted, mask-after-validate rejected")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--dsdlc", help="path to the dsdlc executable")
    ap.add_argument("--selftest", action="store_true", help="run the negative-control self test")
    args = ap.parse_args()

    if args.selftest:
        return selftest()

    if not args.dsdlc or not os.path.exists(args.dsdlc):
        print(f"error: --dsdlc <path> required (got {args.dsdlc!r})", file=sys.stderr)
        return 2

    with tempfile.TemporaryDirectory(prefix="emit-order-") as workdir:
        print("emit-order verifier: generating + checking traces")
        failures = verify_fixtures(args.dsdlc, workdir)

    if failures:
        print("\nEMIT-ORDER VERIFIER FAILED:")
        for f in failures:
            print(f"  - {f}")
        return 1
    print("\nemit-order verifier: all backends are members of the proven equivalence class")
    return 0


if __name__ == "__main__":
    sys.exit(main())
