#!/usr/bin/env python3
#===----------------------------------------------------------------------===#
#
# Part of the OpenCyphal project, under the MIT licence
# SPDX-License-Identifier: MIT
#
#===----------------------------------------------------------------------===#

"""Regression tests for embedded UAVCAN MLIR catalogue generator."""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
import textwrap
import unittest
from pathlib import Path
from typing import List


class EmbeddedUavcanMlirGeneratorTests(unittest.TestCase):
    def setUp(self) -> None:
        self.repo_root = Path(self.repo_root_arg).resolve()
        self.generator = self.repo_root / "tools/dsdlc/generate_embedded_uavcan_mlir.py"
        self.assertTrue(self.generator.exists(), f"missing generator script: {self.generator}")

    def _make_fixture(self) -> tuple[Path, Path, Path]:
        fixture_root = Path(tempfile.mkdtemp(prefix="llvmdsdl-embedded-uavcan-gen-test-"))
        repo_fixture = fixture_root / "repo"
        uavcan_root = repo_fixture / "submodules/public_regulated_data_types/uavcan"
        output = repo_fixture / "lib/CodeGen/UavcanEmbeddedMlir.inc"
        fake_dsdlc = fixture_root / "fake-dsdlc.sh"

        uavcan_root.mkdir(parents=True, exist_ok=True)
        output.parent.mkdir(parents=True, exist_ok=True)

        mlir_text = textwrap.dedent(
            """
            module {
              dsdl.schema @uavcan_node_Heartbeat_1_0 {
                full_name = \"uavcan.node.Heartbeat\"
                major = 1 : i32
                minor = 0 : i32
              }
            }
            """
        ).lstrip()

        fake_dsdlc.write_text(
            "#!/usr/bin/env bash\n"
            "set -euo pipefail\n"
            "if [[ \"${1:-}\" != \"--target-language\" || \"${2:-}\" != \"mlir\" ]]; then\n"
            "  echo \"unexpected args\" >&2\n"
            "  exit 2\n"
            "fi\n"
            f"cat <<'MLIR'\n{mlir_text}MLIR\n",
            encoding="utf-8",
        )
        fake_dsdlc.chmod(fake_dsdlc.stat().st_mode | 0o111)

        return repo_fixture, output, fake_dsdlc

    def _run(self, repo_root: Path, fake_dsdlc: Path, check: bool) -> subprocess.CompletedProcess[str]:
        command = [
            sys.executable,
            str(self.generator),
            "--repo-root",
            str(repo_root),
            "--uavcan-root",
            str(repo_root / "submodules/public_regulated_data_types/uavcan"),
            "--dsdlc",
            str(fake_dsdlc),
            "--output",
            str(repo_root / "lib/CodeGen/UavcanEmbeddedMlir.inc"),
        ]
        if check:
            command.append("--check")
        return subprocess.run(command, text=True, capture_output=True, check=False)

    def test_generation_and_check_roundtrip(self) -> None:
        repo_root, output, fake_dsdlc = self._make_fixture()
        try:
            generate = self._run(repo_root, fake_dsdlc, check=False)
            self.assertEqual(generate.returncode, 0, msg=f"generation failed: {generate.stdout}\n{generate.stderr}")
            self.assertTrue(output.exists(), "expected generator to create output include")

            check = self._run(repo_root, fake_dsdlc, check=True)
            self.assertEqual(check.returncode, 0, msg=f"check failed: {check.stdout}\n{check.stderr}")
            self.assertIn("up to date", check.stdout)
        finally:
            shutil.rmtree(repo_root.parent, ignore_errors=True)

    def test_check_detects_drift(self) -> None:
        repo_root, output, fake_dsdlc = self._make_fixture()
        try:
            first = self._run(repo_root, fake_dsdlc, check=False)
            self.assertEqual(first.returncode, 0, msg=f"initial generation failed: {first.stdout}\n{first.stderr}")

            output.write_text(output.read_text(encoding="utf-8") + "\n// drift\n", encoding="utf-8")

            check = self._run(repo_root, fake_dsdlc, check=True)
            self.assertNotEqual(check.returncode, 0, msg="expected check mode to fail on drift")
            self.assertIn("out of date", check.stdout)
        finally:
            shutil.rmtree(repo_root.parent, ignore_errors=True)


def parse_args(argv: List[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Run embedded UAVCAN MLIR generator regression tests.")
    parser.add_argument("--repo-root", required=True, help="Path to repository root.")
    return parser.parse_args(argv)


def main(argv: List[str]) -> int:
    args = parse_args(argv)
    EmbeddedUavcanMlirGeneratorTests.repo_root_arg = args.repo_root
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(EmbeddedUavcanMlirGeneratorTests)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
