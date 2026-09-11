#!/usr/bin/env python3
# ===----------------------------------------------------------------------===#
#
# Part of the OpenCyphal project, under the MIT licence
# SPDX-License-Identifier: MIT
#
# ===----------------------------------------------------------------------===#
"""Tests for the parts of generate_prdt_corpus.py that need neither a compiler nor the network."""

from __future__ import annotations

import json
import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import generate_prdt_corpus as gen  # noqa: E402


class RootNamespaceDiscovery(unittest.TestCase):
    def test_only_directories_holding_dsdl_count(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            corpus = pathlib.Path(tmp)
            (corpus / "uavcan" / "node").mkdir(parents=True)
            (corpus / "uavcan" / "node" / "7509.Heartbeat.1.0.dsdl").write_text("@sealed\n")
            (corpus / "reg" / "udral").mkdir(parents=True)
            (corpus / "reg" / "udral" / "Thing.0.1.dsdl").write_text("@sealed\n")
            (corpus / "verify").mkdir()
            (corpus / "README.md").write_text("")
            self.assertEqual([root.name for root in gen.root_namespaces(corpus)], ["reg", "uavcan"])

    def test_missing_corpus_has_no_roots(self) -> None:
        self.assertEqual(gen.root_namespaces(pathlib.Path("/nonexistent/public_regulated_data_types")), [])


class PresetResolution(unittest.TestCase):
    PRESETS = {
        "configurePresets": [
            {
                "name": "base",
                "binaryDir": "${sourceDir}/build/matrix/${presetName}",
                "cacheVariables": {"CMAKE_DEFAULT_BUILD_TYPE": "RelWithDebInfo"},
            },
            {"name": "dev", "inherits": "base"},
            {"name": "ci", "inherits": ["base"]},
        ],
        "buildPresets": [
            {"name": "build-dev", "configurePreset": "dev"},
            {"name": "build-ci", "configurePreset": "ci", "configuration": "Debug"},
        ],
    }

    def test_binary_dir_names_the_derived_preset(self) -> None:
        source = pathlib.Path("/src")
        self.assertEqual(gen.preset_binary_dir(self.PRESETS, "dev", source), source / "build/matrix/dev")
        self.assertEqual(gen.preset_binary_dir(self.PRESETS, "ci", source), source / "build/matrix/ci")

    def test_build_configuration_prefers_the_build_preset(self) -> None:
        self.assertEqual(gen.preset_build_config(self.PRESETS, "ci"), "Debug")
        self.assertEqual(gen.preset_build_config(self.PRESETS, "dev"), "RelWithDebInfo")

    def test_unknown_preset_is_an_error(self) -> None:
        with self.assertRaises(KeyError):
            gen.preset_build_config(self.PRESETS, "nope")

    def test_repository_presets_resolve(self) -> None:
        presets = json.loads((gen.REPO_ROOT / "CMakePresets.json").read_text(encoding="utf-8"))
        self.assertEqual(gen.preset_build_config(presets, "dev-homebrew"), "RelWithDebInfo")
        self.assertEqual(gen.preset_build_config(presets, "ci"), "Debug")
        self.assertEqual(
            gen.preset_binary_dir(presets, "dev-llvm-env", gen.REPO_ROOT),
            gen.REPO_ROOT / "build" / "matrix" / "dev-llvm-env",
        )


class CommandShapes(unittest.TestCase):
    ROOTS = [pathlib.Path("/c/reg"), pathlib.Path("/c/uavcan")]

    def test_dsdlc_covers_every_root_from_the_checkout_alone(self) -> None:
        command = gen.dsdlc_command(pathlib.Path("/bin/dsdlc"), "c", self.ROOTS, pathlib.Path("/o/c"))
        self.assertEqual(command[:4], ["/bin/dsdlc", "--target-language", "c", "--no-embedded-uavcan"])
        self.assertEqual(command.count("--lookup-dir"), 2)
        self.assertEqual(command[command.index("--lookup-dir") + 1], "/c/reg")
        self.assertIn("/c/reg", command[-4:])
        self.assertIn("/c/uavcan", command[-4:])
        self.assertEqual(command[-2:], ["--outdir", "/o/c"])

    def test_nnvg_runs_one_job_with_experimental_languages(self) -> None:
        command = gen.nnvg_command(pathlib.Path("/venv/bin/nnvg"), "py", self.ROOTS, pathlib.Path("/o/py"))
        self.assertEqual(command[:3], ["/venv/bin/nnvg", "--target-language", "py"])
        self.assertIn("--include-experimental-languages", command)
        self.assertEqual(command[command.index("--jobs") + 1], "1")
        self.assertEqual(command.count("--lookup-dir"), 2)
        self.assertEqual(command[-2:], ["--outdir", "/o/py"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
