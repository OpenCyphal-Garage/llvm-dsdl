#!/usr/bin/env python3
# ===----------------------------------------------------------------------===//
#
# Part of the OpenCyphal project, under the MIT licence
# SPDX-License-Identifier: MIT
#
# ===----------------------------------------------------------------------===//

"""Validate shared execution-engine boundaries for backend emitters and guard scripts."""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path
from typing import Iterable, List, Sequence


SOURCE_RULES = [
    {
        "path": "lib/CodeGen/emitter/C.cpp",
        "required": [
            r"addLowerDSDLBodiesPipeline\(",
            r"createConvertDSDLToEmitCPass\(",
            r"collectLoweredFactsFromMlir\(",
            r"mlirSchemaCoverageValidationFailedForEmission\(\"C\"\)",
        ],
        "forbidden": [
            r"emitNativeFunctionSkeleton\(",
            r"buildScriptedSectionOperationPlan\(",
            r"buildLoweredBodyRenderIR\(",
        ],
    },
    {
        "path": "lib/CodeGen/emitter/Cpp.cpp",
        "required": [
            r"emitNativeFunctionSkeleton\(",
            r"collectLoweredFactsFromMlir\(",
            r"mlirSchemaCoverageValidationFailedForEmission\(\"C\+\+\"\)",
        ],
        "forbidden": [r"buildLoweredBodyRenderIR\(", r"forEachLoweredRenderStep\(", r"validateNativeSectionHelperContract\("],
    },
    {
        "path": "lib/CodeGen/emitter/Rust.cpp",
        "required": [
            r"emitNativeFunctionSkeleton\(",
            r"collectLoweredFactsFromMlir\(",
            r"mlirSchemaCoverageValidationFailedForEmission\(\"Rust\"\)",
        ],
        "forbidden": [r"buildLoweredBodyRenderIR\(", r"forEachLoweredRenderStep\(", r"validateNativeSectionHelperContract\("],
    },
    {
        "path": "lib/CodeGen/emitter/Go.cpp",
        "required": [
            r"emitNativeFunctionSkeleton\(",
            r"collectLoweredFactsFromMlir\(",
            r"mlirSchemaCoverageValidationFailedForEmission\(\"Go\"\)",
        ],
        "forbidden": [r"buildLoweredBodyRenderIR\(", r"forEachLoweredRenderStep\(", r"validateNativeSectionHelperContract\("],
    },
    {
        "path": "lib/CodeGen/emitter/Ts.cpp",
        "required": [
            r"buildScriptedSectionOperationPlan\(",
            r"operationPlan\.takeError\(\)",
            r"collectLoweredFactsFromMlir\(",
            r"mlirSchemaCoverageValidationFailedForEmission\(\"TypeScript\"\)",
        ],
        "forbidden": [r"buildScriptedSectionBodyPlan\(", r"buildLoweredBodyRenderIR\(", r"forEachLoweredRenderStep\("],
    },
    {
        "path": "lib/CodeGen/emitter/Python.cpp",
        "required": [
            r"buildScriptedSectionOperationPlan\(",
            r"operationPlan\.takeError\(\)",
            r"collectLoweredFactsFromMlir\(",
            r"mlirSchemaCoverageValidationFailedForEmission\(\"Python\"\)",
        ],
        "forbidden": [r"buildScriptedSectionBodyPlan\(", r"buildLoweredBodyRenderIR\(", r"forEachLoweredRenderStep\("],
    },
]

BACKEND_GUARD_REQUIRED_MARKERS = [
    "set(found_backend_fallback_signature FALSE)",
    "set(found_backend_array_length_fallback_signature FALSE)",
    "set(found_backend_delimiter_fallback_signature FALSE)",
    "set(found_backend_scalar_deser_fallback_signature FALSE)",
    "if(found_backend_fallback_signature)",
    "if(found_backend_array_length_fallback_signature)",
    "if(found_backend_delimiter_fallback_signature)",
    "if(found_backend_scalar_deser_fallback_signature)",
    "Found backend fallback saturation signatures",
    "Found backend inline array-length fallback signatures",
    "Found backend inline delimiter fallback signatures",
    "Found backend scalar-deserialize fallback signatures",
]

GUARD_RULES = [
    {
        "path": "test/integration/RunUavcanCppGeneration.cmake",
        "required_markers": BACKEND_GUARD_REQUIRED_MARKERS,
    },
    {
        "path": "test/integration/RunUavcanRustGeneration.cmake",
        "required_markers": BACKEND_GUARD_REQUIRED_MARKERS,
    },
    {
        "path": "test/integration/RunUavcanGoGeneration.cmake",
        "required_markers": BACKEND_GUARD_REQUIRED_MARKERS,
    },
    {
        "path": "test/integration/RunUavcanTsGeneration.cmake",
        "required_markers": BACKEND_GUARD_REQUIRED_MARKERS,
    },
    {
        "path": "test/integration/RunUavcanPythonGeneration.cmake",
        "required_markers": BACKEND_GUARD_REQUIRED_MARKERS,
    },
    {
        "path": "test/integration/RunUavcanGeneration.cmake",
        "required_markers": [
            'set(array_length_fallback_hits "")',
            'set(scalar_saturation_fallback_hits "")',
            'set(delimiter_fallback_hits "")',
            'set(scalar_deser_fallback_hits "")',
            "if(array_length_fallback_hits)",
            "if(scalar_saturation_fallback_hits)",
            "if(delimiter_fallback_hits)",
            "if(scalar_deser_fallback_hits)",
            "still contain array-length fallback logic",
            "still contain scalar saturation fallback logic",
            "still contain inline delimiter fallback checks",
            "still contain scalar deserialize fallback assignments",
        ],
    },
]


def _parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Validate backend execution-engine boundaries.")
    parser.add_argument("--repo-root", required=True, help="Path to repository root.")
    return parser.parse_args(list(argv))


def _validate_source_rule(repo_root: Path, rule: dict, failures: List[str]) -> None:
    rel_path = str(rule["path"])
    path = repo_root / rel_path
    if not path.exists():
        failures.append(f"missing required source file: {rel_path}")
        return
    text = path.read_text(encoding="utf-8")
    for pattern in rule["required"]:
        if re.search(pattern, text) is None:
            failures.append(f"missing required source marker in {rel_path}: /{pattern}/")
    for pattern in rule["forbidden"]:
        if re.search(pattern, text) is not None:
            failures.append(f"forbidden source marker present in {rel_path}: /{pattern}/")


def _validate_guard_rule(repo_root: Path, rule: dict, failures: List[str]) -> None:
    rel_path = str(rule["path"])
    path = repo_root / rel_path
    if not path.exists():
        failures.append(f"missing required guard file: {rel_path}")
        return
    text = path.read_text(encoding="utf-8")
    for marker in rule["required_markers"]:
        if marker not in text:
            failures.append(f"missing required guard marker in {rel_path}: {marker}")


def main(argv: Iterable[str]) -> int:
    args = _parse_args(list(argv))
    repo_root = Path(args.repo_root).resolve()
    failures: List[str] = []
    for rule in SOURCE_RULES:
        _validate_source_rule(repo_root, rule, failures)
    for rule in GUARD_RULES:
        _validate_guard_rule(repo_root, rule, failures)
    if failures:
        for failure in failures:
            print(f"execution-engine regression: {failure}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
