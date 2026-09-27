#!/usr/bin/env python3
# ===----------------------------------------------------------------------===#
#
# Part of the OpenCyphal project, under the MIT licence
# SPDX-License-Identifier: MIT
#
# ===----------------------------------------------------------------------===#
"""
Tests for corpus_determinism.py.

The comparison is held against a stand-in dsdlc that writes the same bytes on every run unless told
which run to change: two clean builds compare green, and one byte, one exit code or one standard output
changed in one run compares red. The oracle matrix is held against the real dsdlc, named by the DSDLC
environment variable, so that a target dsdlc accepts and the oracle omits fails here.
"""

from __future__ import annotations

import json
import os
import pathlib
import stat
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import corpus_determinism as oracle  # noqa: E402

SCRIPT = pathlib.Path(oracle.__file__).resolve()

# Writes its own command line, less --outdir, into one file per run. STANDIN_FLIP, STANDIN_EXIT and
# STANDIN_STDOUT each name a run by its identifier, which is the tail of its output directory.
STANDIN = """#!/usr/bin/env python3
import os
import sys

args = sys.argv[1:]
if args == ["--version"]:
    print("dsdlc stand-in")
    sys.exit(0)
at = args.index("--outdir")
outdir = args[at + 1]
command = " ".join(args[:at] + args[at + 2:])


def named(variable):
    run = os.environ.get(variable)
    return bool(run) and outdir.replace(os.sep, "/").endswith("/" + run)


content = bytearray(command.encode("utf-8"))
if named("STANDIN_FLIP"):
    content[0] ^= 0x01
os.makedirs(os.path.join(outdir, "ns"), exist_ok=True)
with open(os.path.join(outdir, "ns", "Type_1_0.out"), "wb") as handle:
    handle.write(content)
print("module" + (" changed" if named("STANDIN_STDOUT") else ""))
sys.exit(1 if named("STANDIN_EXIT") else 0)
"""


class Comparison(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls._tmp = tempfile.TemporaryDirectory(prefix="corpus-determinism-test-")
        cls.tmp = pathlib.Path(cls._tmp.name)
        cls.standin = cls.tmp / "dsdlc"
        cls.standin.write_text(STANDIN, encoding="utf-8")
        cls.standin.chmod(cls.standin.stat().st_mode | stat.S_IXUSR)
        cls.baseline = cls.generate("baseline")

    @classmethod
    def tearDownClass(cls) -> None:
        cls._tmp.cleanup()

    @classmethod
    def run_generation(cls, label: str, **environment: str) -> tuple[pathlib.Path, subprocess.CompletedProcess]:
        out = cls.tmp / f"{label}.json"
        result = subprocess.run(
            [sys.executable, str(SCRIPT), "--matrix", "oracle", "--dsdlc", str(cls.standin),
             "--work-dir", str(cls.tmp / "work"), "--out", str(out), "--label", label],
            capture_output=True, text=True, env={**os.environ, **environment})
        return out, result

    @classmethod
    def generate(cls, label: str, **environment: str) -> pathlib.Path:
        out, result = cls.run_generation(label, **environment)
        if result.returncode != 0:
            raise AssertionError(f"generation {label} failed:\n{result.stdout}\n{result.stderr}")
        return out

    def compare(self, other: pathlib.Path) -> subprocess.CompletedProcess:
        return subprocess.run([sys.executable, str(SCRIPT), "--compare", str(self.baseline), str(other)],
                              capture_output=True, text=True)

    def test_unchanged_output_compares_green(self) -> None:
        result = self.compare(self.generate("unchanged"))
        self.assertEqual(result.returncode, 0, result.stdout)
        runs = len(oracle.ORACLE_TARGETS) * (len(oracle.ORACLE_CORPORA) + len(oracle.ADVERSARIAL_PASSES))
        self.assertIn(f"all {runs} runs byte-identical", result.stdout)

    def test_one_seeded_byte_compares_red(self) -> None:
        result = self.compare(self.generate("seeded", STANDIN_FLIP="showroom/rust"))
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("[showroom/rust] content differs: ['ns/Type_1_0.out']", result.stdout)
        self.assertEqual(result.stdout.count("\n  - "), 1, result.stdout)

    def test_a_seeded_byte_in_the_adversarial_corpus_compares_red(self) -> None:
        result = self.compare(self.generate("adversarial", STANDIN_FLIP="adversarial-versioned/cpp-std"))
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("[adversarial-versioned/cpp-std] content differs", result.stdout)

    def test_a_failing_run_fails_the_generation_and_compares_red(self) -> None:
        manifest, generation = self.run_generation("failing", STANDIN_EXIT="views/go")
        self.assertEqual(generation.returncode, 1, generation.stdout)
        self.assertIn("dsdlc failed for views/go (exit 1)", generation.stderr)
        result = self.compare(manifest)
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("[views/go] exit 0 against 1", result.stdout)

    def test_a_changed_product_on_standard_output_compares_red(self) -> None:
        result = self.compare(self.generate("printed", STANDIN_STDOUT="uavcan/mlir"))
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn(f"[uavcan/mlir] content differs: ['{oracle.STDOUT_KEY}']", result.stdout)

    def test_a_code_target_standard_output_is_not_recorded(self) -> None:
        result = self.compare(self.generate("summary", STANDIN_STDOUT="uavcan/c"))
        self.assertEqual(result.returncode, 0, result.stdout)

    def test_different_matrices_are_refused(self) -> None:
        other = self.tmp / "languages.json"
        manifest = json.loads(self.baseline.read_text(encoding="utf-8"))
        manifest["matrix"] = "languages"
        other.write_text(json.dumps(manifest), encoding="utf-8")
        result = self.compare(other)
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("different matrices", result.stdout)


class OracleMatrix(unittest.TestCase):
    def test_every_target_runs_over_every_corpus(self) -> None:
        corpora = dict(oracle.ORACLE_CORPORA)
        corpora.update({name: (["adv"], [], []) for name in oracle.ADVERSARIAL_PASSES})
        runs = {run["id"]: run for run in oracle.oracle_runs(corpora)}
        self.assertEqual(set(runs), {f"{corpus}/{target}" for corpus in corpora for target in oracle.ORACLE_TARGETS})
        for run_id, run in runs.items():
            self.assertIn("--naming-manifest", run["command"], run_id)

    def test_only_ast_takes_the_analysis_lookup(self) -> None:
        runs = {run["id"]: run["command"] for run in oracle.oracle_runs(oracle.ORACLE_CORPORA)}
        self.assertIn(oracle.REGULATED_ROOT, runs["showroom/ast"])
        self.assertNotIn(oracle.REGULATED_ROOT, runs["showroom/c"])

    @unittest.skipUnless(os.environ.get("DSDLC"), "DSDLC names no dsdlc")
    def test_every_language_dsdlc_accepts_is_a_target(self) -> None:
        help_text = subprocess.run([os.environ["DSDLC"], "--help"], check=True, stdout=subprocess.PIPE,
                                   stderr=subprocess.STDOUT, text=True).stdout.splitlines()
        heading = help_text.index("LANGUAGES")
        accepted = {name.strip() for name in help_text[heading + 1].split("|")}
        covered = {language for language, _, _ in oracle.ORACLE_TARGETS.values()}
        self.assertEqual(accepted, covered)


if __name__ == "__main__":
    unittest.main()
