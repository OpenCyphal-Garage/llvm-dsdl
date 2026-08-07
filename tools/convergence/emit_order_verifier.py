#!/usr/bin/env python3
# Part of the OpenCyphal project, under the MIT licence
# SPDX-License-Identifier: MIT
"""
Emit-order verifier (the golden step-trace check).

Runs dsdlc over a set of DSDL fixtures for each string backend (rust/go/cpp/ts/python)
with the LLVMDSDL_EMIT_TRACE side channel enabled, then verifies each backend's abstract
op-trace is a MEMBER of the proven-safe equivalence class -- the same serialize/deserialize
ordering the Dafny oracle (spec/dafny/CyphalSerdes.dfy) proves for the canonical traces.

Traces are segmented per (type, direction) by SECTION header lines that the emitters
record at each serialize/deserialize function entry, so a divergence is reported against
one concrete type and direction, and misalignment cannot cancel across type boundaries.

It checks two things:

  1. Equivalence-class membership (per backend per segment, the safety property):
       serialize tag   : VALIDATE_TAG before MASK_TAG before WRITE_TAG
       deserialize tag : READ_TAG before MASK_TAG before VALIDATE_TAG  (before SWITCH)
       serialize len   : LEN_VALIDATE before LEN_WRITE
       deserialize len : LEN_READ before LEN_VALIDATE  (before ELEM_LOOP)
       each write op (and BULK_COPY) is immediately followed by ADVANCE
       no direction-mismatched ops (a READ_* in a serialize segment, etc.)
     These are tolerant of where the *bookkeeping* ops (ADVANCE / STORE_TAG) sit, which is
     exactly why the safe D4 backends (TS/Python advance-early, Python validate-before-store)
     pass while a genuine mask-before-validate reorder fails.

  2. Cross-backend skeleton agreement (the consistency property):
     per (type, direction), after removing the accepted-difference ops and applying the
     declared equivalences, every backend must produce the identical wire-shape sequence
     of (op, payload) pairs -- payloads included, so a wrong bit width, option index, or
     prefix width is a failure even when the op names agree.

Accepted differences (explicitly modeled):
  D2: LEN_CHECK (fixed-array exact-length guard) is backend-optional -- Go/C++ fixed
      arrays are compile-time sized so the guard is type-system-subsumed. Dropped from
      the skeleton.
  D3: the C++ fixed-bool-array fast path traces BULK_COPY instead of an element loop;
      the skeleton expands BULK_COPY to ELEM_LOOP + one 1-bit bool scalar op.
  D4: ADVANCE / STORE_TAG positions are bookkeeping freedom. Dropped from the skeleton;
      membership (check 1) still pins the safety-critical relative order.

Exit code is non-zero if any backend violates membership or the skeletons disagree.
Run with --selftest for the negative controls (a mask-before-validate trace, a payload
divergence, and the D3 equivalence must behave as specified).
Run with --mutation-test (requires --dsdlc) for the end-to-end negative control: dsdlc
is re-run with LLVMDSDL_EMIT_TRACE_MUTATE=swap-tag-validate, which reorders the recorded
VALIDATE_TAG/MASK_TAG pairs in the real emitter trace, and the verifier must go red.
"""
import argparse
import os
import subprocess
import sys
import tempfile

BACKENDS = ["rust", "go", "cpp", "ts", "python"]

# Ops whose position is a free bookkeeping/accepted-difference degree of freedom (D2/D4 +
# the native-only trailing byte-align). Removed before the cross-backend skeleton
# comparison; NOT used to relax the membership ordering.
SKELETON_DROP = {"ADVANCE", "STORE_TAG", "ALIGN", "LEN_CHECK"}

# Contiguous op groups that make up a tag / length prologue (scanned backward from the
# dispatch/loop op). Bookkeeping ops are allowed to appear interleaved.
TAG_GROUP = {"VALIDATE_TAG", "MASK_TAG", "WRITE_TAG", "READ_TAG", "STORE_TAG", "ADVANCE", "ALIGN"}
LEN_GROUP = {"LEN_VALIDATE", "LEN_WRITE", "LEN_READ", "LEN_CHECK", "ADVANCE", "ALIGN"}

WRITE_OPS = {
    "WRITE_TAG", "LEN_WRITE",
    "WRITE_SCALAR_BOOL", "WRITE_SCALAR_UINT", "WRITE_SCALAR_SINT", "WRITE_SCALAR_FLOAT",
}

# Ops that may only appear in one direction's segment.
SERIALIZE_ONLY = WRITE_OPS | {"LEN_CHECK"}
DESERIALIZE_ONLY = {
    "READ_TAG", "STORE_TAG", "LEN_READ",
    "READ_SCALAR_BOOL", "READ_SCALAR_UINT", "READ_SCALAR_SINT", "READ_SCALAR_FLOAT",
}


def parse_trace(path):
    """Read a trace file into {(section, direction): [(op, payload|None), ...]}."""
    segments = {}
    current = None
    with open(path, "r", encoding="utf-8") as handle:
        for lineno, line in enumerate(handle, 1):
            parts = line.split()
            if not parts:
                continue
            if parts[0] == "SECTION":
                if len(parts) != 3 or parts[2] not in ("serialize", "deserialize"):
                    raise ValueError(f"{path}:{lineno}: malformed SECTION header: {line.strip()}")
                key = (parts[1], parts[2])
                if key in segments:
                    raise ValueError(f"{path}:{lineno}: duplicate segment {key}")
                current = segments[key] = []
                continue
            if current is None:
                raise ValueError(f"{path}:{lineno}: op before any SECTION header")
            payload = int(parts[1]) if len(parts) > 1 else None
            current.append((parts[0], payload))
    return segments


def _prologue_before(ops, index, group):
    """The contiguous run of `group` op names immediately preceding ops[index], in order."""
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


def check_membership(segment, direction):
    """Return equivalence-class violations for one backend's (type, direction) segment."""
    ops = [op for op, _ in segment]
    violations = []

    wrong_direction = SERIALIZE_ONLY if direction == "deserialize" else DESERIALIZE_ONLY
    for i, op in enumerate(ops):
        if op in wrong_direction:
            violations.append(f"{op} @{i}: not a {direction} op")

    for i, op in enumerate(ops):
        if op == "SWITCH":
            grp = _prologue_before(ops, i, TAG_GROUP)
            if direction == "serialize":
                if not _ordered(grp, "VALIDATE_TAG", "MASK_TAG", "WRITE_TAG"):
                    violations.append(
                        f"serialize tag @{i}: need VALIDATE_TAG<MASK_TAG<WRITE_TAG, got {grp}")
            else:
                if not _ordered(grp, "READ_TAG", "MASK_TAG", "VALIDATE_TAG"):
                    violations.append(
                        f"deserialize tag @{i}: need READ_TAG<MASK_TAG<VALIDATE_TAG, got {grp}")

        if op == "ELEM_LOOP":
            grp = _prologue_before(ops, i, LEN_GROUP)
            if direction == "serialize":
                if "LEN_WRITE" in grp and not _ordered(grp, "LEN_VALIDATE", "LEN_WRITE"):
                    violations.append(
                        f"serialize len @{i}: need LEN_VALIDATE<LEN_WRITE, got {grp}")
            else:
                if "LEN_READ" in grp and not _ordered(grp, "LEN_READ", "LEN_VALIDATE"):
                    violations.append(
                        f"deserialize len @{i}: need LEN_READ<LEN_VALIDATE, got {grp}")

    advance_required = (WRITE_OPS if direction == "serialize" else set()) | {"BULK_COPY"}
    for i, op in enumerate(ops):
        if op in advance_required:
            nxt = ops[i + 1] if i + 1 < len(ops) else "<eof>"
            if nxt != "ADVANCE":
                violations.append(f"{op} @{i}: must be immediately followed by ADVANCE, got {nxt}")

    return violations


def skeleton(segment, direction):
    """The wire-shape (op, payload) sequence: accepted-difference ops removed/expanded."""
    scalar = "WRITE_SCALAR_BOOL" if direction == "serialize" else "READ_SCALAR_BOOL"
    out = []
    for op, payload in segment:
        if op in SKELETON_DROP:
            continue
        if op == "BULK_COPY":
            # D3: the C++ fixed-bool-array fast path is declared equivalent to an
            # element loop of 1-bit bool scalar ops.
            out.append(("ELEM_LOOP", None))
            out.append((scalar, 1))
            continue
        out.append((op, payload))
    return out


def run_dsdlc(dsdlc, lang, fixture_root, trace_path, out_dir, extra_env=None):
    """Generate `lang` from fixture_root with tracing on; return (ok, stderr)."""
    cmd = [dsdlc, "--target-language", lang, fixture_root, "--outdir", out_dir]
    if lang == "cpp":
        cmd += ["--cpp-profile", "std"]   # a single flavor -> a single, non-doubled trace
    if lang == "ts":
        cmd += ["--ts-module", "emit_order_verifier"]
    env = dict(os.environ, LLVMDSDL_EMIT_TRACE=trace_path)
    if extra_env:
        env.update(extra_env)
    proc = subprocess.run(cmd, env=env, capture_output=True, text=True)
    return proc.returncode == 0, proc.stderr


FIXTURES = {
    # A tagged union: exercises the tag prologue (and the D4 advance/store variations).
    "vaultwidget/Union.1.0.dsdl": "@union\nuint8 first\nuint16 second\n@sealed\n",
    # A struct with a variable-length array: exercises the length prologue + element loop.
    "vaultwidget/VarArray.1.0.dsdl": "uint8[<=4] items\n@sealed\n",
    # A struct with a fixed-length array + scalars: exercises LenCheck (D2) + scalar writes.
    "vaultwidget/FixedArray.1.0.dsdl": "uint16[3] samples\nuint8 tag\n@sealed\n",
    # Floats at all three widths: exercises the float scalar ops + width payloads.
    "vaultwidget/Floats.1.0.dsdl": "float16 a\nfloat32 b\nfloat64 c\n@sealed\n",
    # Signed ints + void padding: exercises SINT scalars, PAD, and ALIGN.
    "vaultwidget/SignedPad.1.0.dsdl": "int7 a\nvoid9\nint32 b\n@sealed\n",
    # A fixed bool array + a scalar bool: exercises the C++ bulk-copy fast path (D3).
    "vaultwidget/BoolArray.1.0.dsdl": "bool[16] flags\nbool single\n@sealed\n",
    # A sealed composite member: exercises COMPOSITE_INLINE.
    "vaultwidget/Inner.1.0.dsdl": "uint8 x\n@sealed\n",
    "vaultwidget/Outer.1.0.dsdl": "vaultwidget.Inner.1.0 inner\nuint8 y\n@sealed\n",
    # A delimited (non-sealed) composite member: exercises COMPOSITE_DELIM_HEADER.
    "vaultwidget/DelimInner.1.0.dsdl": "uint16 x\n@extent 64 * 8\n",
    "vaultwidget/DelimOuter.1.0.dsdl": "vaultwidget.DelimInner.1.0 inner\n@sealed\n",
    # A fixed array of composites: exercises ELEM_LOOP + COMPOSITE_INLINE together.
    "vaultwidget/CompArray.1.0.dsdl": "vaultwidget.Inner.1.0[2] pair\n@sealed\n",
    # A service type: exercises the .Request/.Response section labeling.
    "vaultwidget/Svc.1.0.dsdl": "uint8 q\n@sealed\n---\nuint16 r\n@sealed\n",
}


def write_fixtures(workdir):
    fixture_root = os.path.join(workdir, "dsdl")
    for rel, body in FIXTURES.items():
        path = os.path.join(fixture_root, rel)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8") as handle:
            handle.write(body)
    return fixture_root


def collect_traces(dsdlc, workdir, extra_env=None, fixture_root=None):
    """Run all backends; return (traces {lang: segments}, failures [str])."""
    if fixture_root is None:
        fixture_root = write_fixtures(workdir)
    failures = []
    traces = {}
    for lang in BACKENDS:
        trace_path = os.path.join(workdir, f"trace_{lang}.txt")
        out_dir = os.path.join(workdir, f"out_{lang}")
        ok, stderr = run_dsdlc(dsdlc, lang, fixture_root, trace_path, out_dir, extra_env)
        if not ok:
            failures.append(f"[{lang}] dsdlc failed:\n{stderr}")
            continue
        if not os.path.exists(trace_path):
            failures.append(f"[{lang}] no trace emitted (side channel not wired?)")
            continue
        try:
            traces[lang] = parse_trace(trace_path)
        except ValueError as exc:
            failures.append(f"[{lang}] trace parse error: {exc}")
    return traces, failures


def verify_traces(traces):
    """Membership + cross-backend skeleton agreement over parsed traces; return failures."""
    failures = []

    for lang, segments in traces.items():
        if not segments:
            failures.append(f"[{lang}] empty trace")
            continue
        n_viol = 0
        for (section, direction), segment in segments.items():
            for v in check_membership(segment, direction):
                failures.append(f"[{lang}] {section} {direction}: {v}")
                n_viol += 1
        print(f"  {lang:7s} {len(segments):3d} segments  "
              f"membership: {'OK' if n_viol == 0 else f'{n_viol} violations'}")

    # Cross-backend agreement: identical segment keys, identical skeletons per key.
    if len(traces) > 1:
        ref_lang = "rust" if "rust" in traces else next(iter(traces))
        ref_keys = set(traces[ref_lang].keys())
        agree = True
        for lang, segments in traces.items():
            if lang == ref_lang:
                continue
            missing = ref_keys - set(segments.keys())
            extra = set(segments.keys()) - ref_keys
            for key in sorted(missing):
                failures.append(f"[{lang}] missing segment {key[0]} {key[1]}")
                agree = False
            for key in sorted(extra):
                failures.append(f"[{lang}] unexpected extra segment {key[0]} {key[1]}")
                agree = False
        for section, direction in sorted(ref_keys):
            ref_sk = skeleton(traces[ref_lang][(section, direction)], direction)
            for lang, segments in traces.items():
                if lang == ref_lang or (section, direction) not in segments:
                    continue
                sk = skeleton(segments[(section, direction)], direction)
                if sk != ref_sk:
                    diff = next((i for i, (a, b) in enumerate(zip(sk, ref_sk)) if a != b),
                                min(len(sk), len(ref_sk)))
                    failures.append(
                        f"[{lang}] {section} {direction}: skeleton disagrees with {ref_lang} "
                        f"at op {diff}: {sk[max(0, diff - 2):diff + 3]} vs "
                        f"{ref_sk[max(0, diff - 2):diff + 3]}")
                    agree = False
        n_ops = sum(len(skeleton(seg, d)) for (s, d), seg in traces[ref_lang].items())
        print(f"  skeleton agreement: {'OK' if agree else 'FAIL'} "
              f"({len(ref_keys)} segments, {n_ops} wire ops, ref={ref_lang})")

    return failures


def verify_fixtures(dsdlc, workdir, corpus=None):
    traces, failures = collect_traces(dsdlc, workdir)
    failures += verify_traces(traces)
    if corpus:
        print(f"emit-order verifier: checking corpus {corpus}")
        corpus_workdir = os.path.join(workdir, "corpus")
        os.makedirs(corpus_workdir, exist_ok=True)
        traces, corpus_failures = collect_traces(dsdlc, corpus_workdir, fixture_root=corpus)
        failures += corpus_failures + verify_traces(traces)
    return failures


def mutation_test(dsdlc):
    """End-to-end negative control: a reordered real trace must turn the verifier red.

    dsdlc honors LLVMDSDL_EMIT_TRACE_MUTATE=swap-tag-validate by swapping each recorded
    VALIDATE_TAG with the MASK_TAG that follows it before the trace file is written --
    a genuine mask-before-validate reorder flowing through the entire pipeline (real
    emitter run -> trace side channel -> parser -> membership check). This proves the
    pipeline detects the reorder, not merely that check_membership rejects a hand-written
    list (that is --selftest's job).
    """
    with tempfile.TemporaryDirectory(prefix="emit-order-mut-") as workdir:
        print("mutation test: baseline (unmutated) run must be green")
        baseline = verify_fixtures(dsdlc, workdir)
        if baseline:
            print("MUTATION TEST FAIL: baseline run is not green:")
            for f in baseline:
                print(f"  - {f}")
            return 1
    with tempfile.TemporaryDirectory(prefix="emit-order-mut-") as workdir:
        print("mutation test: mutated run must be red")
        traces, failures = collect_traces(
            dsdlc, workdir, extra_env={"LLVMDSDL_EMIT_TRACE_MUTATE": "swap-tag-validate"})
        failures += verify_traces(traces)
        tag_failures = [f for f in failures if "tag" in f]
        if not tag_failures:
            print("MUTATION TEST FAIL: mutated VALIDATE_TAG/MASK_TAG order was NOT detected "
                  "(the verifier has no teeth)")
            return 1
        print(f"MUTATION TEST OK: mutation detected ({len(tag_failures)} tag-order violations), "
              f"e.g.: {tag_failures[0]}")
        return 0


def selftest():
    """Negative controls for the checker logic itself (no dsdlc needed)."""
    failures = []

    def expect(name, cond):
        print(f"  {'ok' if cond else 'FAIL'}: {name}")
        if not cond:
            failures.append(name)

    good = [("READ_TAG", 8), ("MASK_TAG", None), ("STORE_TAG", None), ("VALIDATE_TAG", None),
            ("ADVANCE", 8), ("SWITCH", None)]
    bad = [("READ_TAG", 8), ("STORE_TAG", None), ("VALIDATE_TAG", None), ("MASK_TAG", None),
           ("ADVANCE", 8), ("SWITCH", None)]  # mask after validate
    expect("safe D4-style deserialize trace accepted",
           not check_membership(good, "deserialize"))
    expect("mask-after-validate deserialize trace rejected",
           bool(check_membership(bad, "deserialize")))

    ser_good = [("VALIDATE_TAG", None), ("MASK_TAG", None), ("WRITE_TAG", 8), ("ADVANCE", 8),
                ("SWITCH", None)]
    ser_bad = [("MASK_TAG", None), ("VALIDATE_TAG", None), ("WRITE_TAG", 8), ("ADVANCE", 8),
               ("SWITCH", None)]
    expect("canonical serialize tag prologue accepted",
           not check_membership(ser_good, "serialize"))
    expect("mask-before-validate serialize trace rejected",
           bool(check_membership(ser_bad, "serialize")))
    expect("direction mismatch rejected (WRITE_TAG in a deserialize segment)",
           bool(check_membership(ser_good, "deserialize")))

    # Payload awareness: same op names, different bit width, must NOT be skeleton-equal.
    a = skeleton([("WRITE_SCALAR_UINT", 8), ("ADVANCE", 8)], "serialize")
    b = skeleton([("WRITE_SCALAR_UINT", 16), ("ADVANCE", 16)], "serialize")
    expect("payload divergence detected (uint8 vs uint16)", a != b)

    # D3: C++ BULK_COPY must be skeleton-equal to the element-loop form.
    loop_form = skeleton([("ELEM_LOOP", None), ("WRITE_SCALAR_BOOL", 1), ("ADVANCE", 1)],
                         "serialize")
    bulk_form = skeleton([("BULK_COPY", 16), ("ADVANCE", 16)], "serialize")
    expect("D3 bulk-copy equivalence accepted", loop_form == bulk_form)
    # ...and BULK_COPY without ADVANCE is a membership violation.
    expect("BULK_COPY without ADVANCE rejected",
           bool(check_membership([("BULK_COPY", 16), ("SWITCH", None)], "deserialize")))

    if failures:
        print(f"SELFTEST FAIL: {len(failures)} control(s) misbehaved")
        return 1
    print("SELFTEST OK: all negative/positive controls behaved")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--dsdlc", help="path to the dsdlc executable")
    ap.add_argument("--selftest", action="store_true", help="run the checker-logic self test")
    ap.add_argument("--mutation-test", action="store_true",
                    help="run the end-to-end mutation negative control (requires --dsdlc)")
    ap.add_argument("--corpus",
                    help="additionally verify a real DSDL namespace root "
                         "(e.g. the UAVCAN public regulated data types)")
    args = ap.parse_args()

    if args.selftest:
        return selftest()

    if not args.dsdlc or not os.path.exists(args.dsdlc):
        print(f"error: --dsdlc <path> required (got {args.dsdlc!r})", file=sys.stderr)
        return 2

    if args.mutation_test:
        return mutation_test(args.dsdlc)

    if args.corpus and not os.path.isdir(args.corpus):
        print(f"error: --corpus root does not exist: {args.corpus!r}", file=sys.stderr)
        return 2

    with tempfile.TemporaryDirectory(prefix="emit-order-") as workdir:
        print("emit-order verifier: generating + checking traces")
        failures = verify_fixtures(args.dsdlc, workdir, corpus=args.corpus)

    if failures:
        print("\nEMIT-ORDER VERIFIER FAILED:")
        for f in failures:
            print(f"  - {f}")
        return 1
    print("\nemit-order verifier: all backends are members of the proven equivalence class")
    return 0


if __name__ == "__main__":
    sys.exit(main())
