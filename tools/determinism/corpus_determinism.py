#!/usr/bin/env python3
# Part of the OpenCyphal project, under the MIT licence
# SPDX-License-Identifier: MIT
"""
Differential generated-output determinism check.

Generates the full corpus with dsdlc for every text backend and records a canonical
per-file SHA-256 manifest. Two manifests produced by two different dsdlc binaries are
then compared byte-for-byte: identical input, identical output, or the difference is
a property of the build rather than of the data.

CI compares x86-64 against arm64, both built from the project's own toolchain. That
covers what actually ships -- two architectures of the same release -- and catches
anything that lets word size, alignment, or floating-point formatting reach emitted
text, which same-host environment perturbation (locale/TZ/PYTHONHASHSEED, already
gated) cannot expose.

Comparing two standard libraries instead -- libstdc++ against libc++ -- catches
std::unordered_* iteration order leaking into output, and is the obvious-looking
alternative. It does not fit a project that ships one toolchain: there is no such
divergence left to measure, and check_unordered_iteration.py excludes that class at
the source anyway.

The tool itself is general -- any two builds will do, which is also how it is used by
hand to prove a refactor changed no output.

Modes:
  generate:  --dsdlc PATH --dsdl-root DIR --out MANIFEST [--label NAME] [--meta k=v ...]
  compare:   --compare A.json B.json

Comparison rules:
  * The five string backends (cpp/rust/go/ts/python) are always compared strictly.
  * The C backend is routed through MLIR/EmitC, so its text can legitimately vary
    across MLIR MAJOR versions; if the two manifests record different llvm majors
    (--meta llvm=...), C is skipped with a loud warning — unless --require-c is
    given (as it is in CI, where both sides are pinned to the same major), in
    which case skew is itself a hard failure so C coverage cannot silently erode.
  * Everything else (missing files, extra files, differing bytes) is a hard failure
    with per-file detail.

Depfiles (*.d) are excluded defensively: they embed absolute host paths by design.
"""
import argparse
import hashlib
import json
import os
import platform
import subprocess
import sys
import tempfile

LANGS = ["c", "cpp", "rust", "go", "ts", "python"]
STRICT_LANGS = ["cpp", "rust", "go", "ts", "python"]  # C handled per the module docstring.


def run_dsdlc(dsdlc, lang, dsdl_root, out_dir):
    cmd = [dsdlc, "--target-language", lang, dsdl_root, "--outdir", out_dir]
    if lang == "ts":
        cmd += ["--ts-module", "cross_stdlib_determinism"]
    proc = subprocess.run(cmd, capture_output=True, text=True)
    if proc.returncode != 0:
        raise RuntimeError(f"dsdlc failed for {lang}:\n{proc.stderr}")


def hash_tree(root):
    """Canonical per-file SHA-256 map over a generated tree (sorted relative paths)."""
    files = {}
    for dirpath, _, filenames in os.walk(root):
        for name in filenames:
            if name.endswith(".d"):
                continue  # depfiles embed absolute host paths by design
            path = os.path.join(dirpath, name)
            rel = os.path.relpath(path, root).replace(os.sep, "/")
            with open(path, "rb") as handle:
                files[rel] = hashlib.sha256(handle.read()).hexdigest()
    digest = hashlib.sha256()
    for rel in sorted(files):
        digest.update(rel.encode("utf-8"))
        digest.update(b"\0")
        digest.update(files[rel].encode("ascii"))
        digest.update(b"\0")
    return {"files": files, "digest": digest.hexdigest()}


def generate(args):
    meta = {
        "label": args.label,
        "platform": sys.platform,
        "machine": platform.machine(),
    }
    for entry in args.meta:
        key, _, value = entry.partition("=")
        meta[key] = value
    version = subprocess.run([args.dsdlc, "--version"], capture_output=True, text=True)
    meta["dsdlc_version"] = version.stdout.strip()

    languages = {}
    with tempfile.TemporaryDirectory(prefix="cross-stdlib-") as workdir:
        for lang in LANGS:
            out_dir = os.path.join(workdir, lang)
            run_dsdlc(args.dsdlc, lang, args.dsdl_root, out_dir)
            languages[lang] = hash_tree(out_dir)
            print(f"  {lang:7s} {len(languages[lang]['files']):4d} files  {languages[lang]['digest'][:16]}...")

    manifest = {"meta": meta, "languages": languages}
    with open(args.out, "w", encoding="utf-8") as handle:
        json.dump(manifest, handle, indent=1, sort_keys=True)
    print(f"manifest written: {args.out} ({meta})")
    return 0


def _llvm_major(manifest):
    llvm = manifest["meta"].get("llvm", "")
    return llvm.split(".")[0] if llvm else ""


def compare(path_a, path_b, require_c=False):
    with open(path_a, encoding="utf-8") as handle:
        a = json.load(handle)
    with open(path_b, encoding="utf-8") as handle:
        b = json.load(handle)
    print(f"A: {a['meta']}")
    print(f"B: {b['meta']}")

    langs = list(STRICT_LANGS)
    failures = []
    major_a, major_b = _llvm_major(a), _llvm_major(b)
    if major_a and major_b and major_a != major_b:
        if require_c:
            failures.append(
                f"[c] llvm majors differ ({major_a} vs {major_b}) and --require-c is set: "
                f"the MLIR/EmitC-routed C backend cannot be compared across MLIR majors. "
                f"Re-align the toolchains (bump both sides together) to restore C coverage.")
        else:
            print(f"WARNING: llvm majors differ ({major_a} vs {major_b}); skipping the "
                  f"MLIR/EmitC-routed C backend comparison (its text may legitimately vary "
                  f"across MLIR majors). Align the toolchains to restore C coverage.")
    else:
        langs.insert(0, "c")

    for lang in langs:
        la, lb = a["languages"].get(lang), b["languages"].get(lang)
        if la is None or lb is None:
            failures.append(f"[{lang}] missing from manifest: A={la is not None} B={lb is not None}")
            continue
        if la["digest"] == lb["digest"]:
            print(f"  {lang:7s} OK ({len(la['files'])} files)")
            continue
        only_a = sorted(set(la["files"]) - set(lb["files"]))
        only_b = sorted(set(lb["files"]) - set(la["files"]))
        differing = sorted(f for f in set(la["files"]) & set(lb["files"])
                           if la["files"][f] != lb["files"][f])
        detail = []
        if only_a:
            detail.append(f"only in A: {only_a[:5]}{'...' if len(only_a) > 5 else ''}")
        if only_b:
            detail.append(f"only in B: {only_b[:5]}{'...' if len(only_b) > 5 else ''}")
        if differing:
            detail.append(f"content differs: {differing[:10]}{'...' if len(differing) > 10 else ''}")
        failures.append(f"[{lang}] tree digests differ; " + "; ".join(detail))

    if failures:
        print("\nCROSS-TOOLCHAIN DETERMINISM FAILED — generated output depends on the "
              "host toolchain (likely unordered container iteration or other "
              "stdlib-dependent behaviour leaking into emission):")
        for f in failures:
            print(f"  - {f}")
        return 1
    print("\ncross-toolchain determinism: all compared backends byte-identical")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--dsdlc", help="path to the dsdlc executable (generate mode)")
    ap.add_argument("--dsdl-root", help="DSDL namespace root to generate from (generate mode)")
    ap.add_argument("--out", help="manifest output path (generate mode)")
    ap.add_argument("--label", default="", help="human-readable side label, e.g. libstdc++/libc++")
    ap.add_argument("--meta", action="append", default=[],
                    help="extra manifest metadata as key=value (e.g. llvm=22.1.8); repeatable")
    ap.add_argument("--compare", nargs=2, metavar=("A", "B"),
                    help="compare two manifests instead of generating")
    ap.add_argument("--require-c", action="store_true",
                    help="fail (instead of warn and skip) when the C backend cannot be "
                         "compared, e.g. on llvm major skew; used in CI where both sides "
                         "are pinned to the same major")
    args = ap.parse_args()

    if args.compare:
        return compare(*args.compare, require_c=args.require_c)
    if not (args.dsdlc and args.dsdl_root and args.out):
        ap.error("generate mode requires --dsdlc, --dsdl-root, and --out")
    if not os.path.exists(args.dsdlc):
        ap.error(f"dsdlc not found: {args.dsdlc}")
    if not os.path.isdir(args.dsdl_root):
        ap.error(f"dsdl root not found: {args.dsdl_root}")
    return generate(args)


if __name__ == "__main__":
    sys.exit(main())
