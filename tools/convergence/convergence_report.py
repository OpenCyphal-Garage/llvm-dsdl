#!/usr/bin/env python3
# ===----------------------------------------------------------------------===//
#
# Part of the OpenCyphal project, under the MIT licence
# SPDX-License-Identifier: MIT
#
# ===----------------------------------------------------------------------===//

"""Compute MLIR convergence scorecards for llvm-dsdl backends."""

from __future__ import annotations

import argparse
import json
import re
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Dict, Iterable, List, Tuple


SEMANTIC_CLASSES: List[Tuple[str, str]] = [
    ("field_step_ordering", "Field step ordering"),
    ("union_tag_mask_validate", "Union tag mask and validation"),
    ("scalar_normalize_sign_extend", "Scalar cast/normalize and sign extension"),
    ("variable_array_prefix_validate", "Variable array prefix normalize and length validation"),
    ("fixed_array_cardinality_validate", "Fixed array cardinality validation"),
    ("delimited_payload_validate", "Delimited payload header validation"),
    ("section_capacity_precheck", "Section capacity pre-check"),
    ("alignment_padding_orchestration", "Alignment/padding orchestration"),
    ("malformed_input_diagnostic_text", "Malformed-input diagnostic category text"),
    ("lowered_contract_validation", "Lowered contract schema/version validation"),
    ("helper_binding_completeness", "Helper-binding completeness gates"),
    ("verifier_first_invariants", "Verifier-first invariant enforcement"),
    ("contract_v2_usage", "Contract-v2 wire-program usage"),
    ("fallback_free_semantic_execution", "Fallback-free semantic execution gates"),
]

BACKEND_CONFIG = {
    "c": {
        "kind": "c",
        "path": "lib/CodeGen/CEmitter.cpp",
        "semantic_gate": {
            "path": "test/integration/RunUavcanGeneration.cmake",
            "helper_patterns": [
                r"did not use lowered union-tag validation helper",
                r"did not use lowered scalar unsigned helper",
                r"did not use lowered array-length validation helper",
                r"did not call lowered delimiter-header validation helper",
            ],
            "fallback_patterns": [
                r"array_length_fallback_hits",
                r"scalar_saturation_fallback_hits",
                r"delimiter_fallback_hits",
                r"scalar_deser_fallback_hits",
                r"still contain array-length fallback logic",
            ],
        },
    },
    "cpp": {
        "kind": "native",
        "path": "lib/CodeGen/CppEmitter.cpp",
        "semantic_gate": {
            "path": "test/integration/RunUavcanCppGeneration.cmake",
            "helper_patterns": [
                r"Missing MLIR union-tag",
                r"Missing MLIR capacity-check",
                r"Missing MLIR array-prefix",
                r"Missing MLIR scalar-unsigned",
                r"Missing MLIR delimiter-validate",
            ],
            "fallback_patterns": [
                r"found_backend_fallback_signature",
                r"found_backend_array_length_fallback_signature",
                r"found_backend_delimiter_fallback_signature",
                r"found_backend_scalar_deser_fallback_signature",
                r"Found backend fallback saturation signatures",
            ],
        },
    },
    "rust": {
        "kind": "native",
        "path": "lib/CodeGen/RustEmitter.cpp",
        "semantic_gate": {
            "path": "test/integration/RunUavcanRustGeneration.cmake",
            "helper_patterns": [
                r"Missing MLIR union-tag",
                r"Missing MLIR capacity-check",
                r"Missing MLIR array-prefix",
                r"Missing MLIR scalar-unsigned",
                r"Missing MLIR delimiter-validate",
            ],
            "fallback_patterns": [
                r"found_backend_fallback_signature",
                r"found_backend_array_length_fallback_signature",
                r"found_backend_delimiter_fallback_signature",
                r"found_backend_scalar_deser_fallback_signature",
                r"Found backend fallback saturation signatures",
            ],
        },
    },
    "go": {
        "kind": "native",
        "path": "lib/CodeGen/GoEmitter.cpp",
        "semantic_gate": {
            "path": "test/integration/RunUavcanGoGeneration.cmake",
            "helper_patterns": [
                r"Missing MLIR union-tag",
                r"Missing MLIR capacity-check",
                r"Missing MLIR array-prefix",
                r"Missing MLIR scalar-unsigned",
                r"Missing MLIR delimiter-validate",
            ],
            "fallback_patterns": [
                r"found_backend_fallback_signature",
                r"found_backend_array_length_fallback_signature",
                r"found_backend_delimiter_fallback_signature",
                r"found_backend_scalar_deser_fallback_signature",
                r"Found backend fallback saturation signatures",
            ],
        },
    },
    "ts": {
        "kind": "scripted",
        "path": "lib/CodeGen/TsEmitter.cpp",
        "semantic_gate": {
            "path": "test/integration/RunUavcanTsGeneration.cmake",
            "helper_patterns": [
                r"Missing MLIR union-tag",
                r"Missing MLIR capacity-check",
                r"Missing MLIR array-prefix",
                r"Missing MLIR scalar-unsigned",
                r"Missing MLIR delimiter-validate",
            ],
            "fallback_patterns": [
                r"found_backend_fallback_signature",
                r"found_backend_array_length_fallback_signature",
                r"found_backend_delimiter_fallback_signature",
                r"found_backend_scalar_deser_fallback_signature",
                r"Found backend fallback saturation signatures",
            ],
        },
    },
    "python": {
        "kind": "scripted",
        "path": "lib/CodeGen/PythonEmitter.cpp",
        "semantic_gate": {
            "path": "test/integration/RunUavcanPythonGeneration.cmake",
            "helper_patterns": [
                r"Missing MLIR union-tag",
                r"Missing MLIR capacity-check",
                r"Missing MLIR array-prefix",
                r"Missing MLIR scalar-unsigned",
                r"Missing MLIR delimiter-validate",
            ],
            "fallback_patterns": [
                r"found_backend_fallback_signature",
                r"found_backend_array_length_fallback_signature",
                r"found_backend_delimiter_fallback_signature",
                r"found_backend_scalar_deser_fallback_signature",
                r"Found backend fallback saturation signatures",
            ],
        },
    },
}

VERIFIER_NEGATIVE_TEST_FILES = [
    "lower-dsdl-serialization-missing-kind-contract.mlir",
    "lower-dsdl-serialization-schema-empty-body-contract.mlir",
    "lower-dsdl-serialization-lowered-envelope-contract.mlir",
    "lower-dsdl-serialization-lowered-step-count-mismatch-contract.mlir",
    "lower-dsdl-serialization-plan-missing-bounds-contract.mlir",
    "lower-dsdl-serialization-unsupported-array-kind-contract.mlir",
    "lower-dsdl-serialization-unsupported-cast-mode-contract.mlir",
    "lower-dsdl-serialization-variable-array-prefix-width-contract.mlir",
    "lower-dsdl-serialization-union-missing-metadata-contract.mlir",
]


def _has_all(text: str, patterns: Iterable[str]) -> bool:
    return all(re.search(pattern, text, flags=re.MULTILINE) for pattern in patterns)


def _has_any(text: str, patterns: Iterable[str]) -> bool:
    return any(re.search(pattern, text, flags=re.MULTILINE) for pattern in patterns)


def _load_text(repo_root: Path, rel_path: str) -> str:
    file_path = repo_root / rel_path
    return file_path.read_text(encoding="utf-8")


def _detect_traits(repo_root: Path, cfg: Dict[str, object], text: str) -> Dict[str, bool]:
    kind = str(cfg["kind"])
    traits: Dict[str, bool] = {
        "semantic_gate_helper_checks": False,
        "semantic_gate_fallback_checks": False,
        "semantic_gate": False,
    }

    semantic_gate_cfg = cfg.get("semantic_gate")
    if isinstance(semantic_gate_cfg, dict):
        gate_path = repo_root / str(semantic_gate_cfg["path"])
        if gate_path.exists():
            gate_text = gate_path.read_text(encoding="utf-8")
            helper_patterns = semantic_gate_cfg.get("helper_patterns", [])
            fallback_patterns = semantic_gate_cfg.get("fallback_patterns", [])
            traits["semantic_gate_helper_checks"] = _has_all(gate_text, helper_patterns)
            traits["semantic_gate_fallback_checks"] = _has_all(gate_text, fallback_patterns)
            traits["semantic_gate"] = traits["semantic_gate_helper_checks"] and traits["semantic_gate_fallback_checks"]

    if kind == "c":
        convert_emitc_text = _load_text(repo_root, "lib/Transforms/ConvertDSDLToEmitC.cpp")
        traits["lower_pass"] = _has_any(
            text,
            [
                r"createLowerDSDLSerializationPass\(",
                r"createLowerDSDLExecPass\(",
            ],
        )
        traits["convert_pass"] = _has_any(text, [r"createConvertDSDLToEmitCPass\("])
        traits["schema_scan"] = _has_any(text, [r"dsdl\.schema", r"schemaByHeaderPath"])
        traits["schema_selection_guard"] = _has_any(text, [r"schema selection failed"])
        traits["diagnostic_catalog"] = _has_all(
            convert_emitc_text,
            [
                r"codegen_diagnostic_text::malformedArrayLengthCategory\(",
                r"codegen_diagnostic_text::malformedUnionTagCategory\(",
                r"codegen_diagnostic_text::malformedDelimiterHeaderCategory\(",
            ],
        )
        traits["emitc_pipeline"] = traits["lower_pass"] and traits["convert_pass"]
        return traits

    if kind == "native":
        traits["collect_lowered_facts"] = _has_any(text, [r"collectLoweredFactsFromMlir\("])
        traits["native_traversal"] = _has_any(text, [r"forEachNativeEmitterRenderStep\("])
        traits["native_function_skeleton"] = _has_any(text, [r"emitNativeFunctionSkeleton\("])
        traits["render_ir_helper_usage"] = _has_any(text, [r"renderIR\.helperBindings"])
        traits["helper_binding_render"] = _has_any(text, [r"renderSectionHelperBindings\("])
        traits["union_helper_usage"] = _has_any(text, [r"unionTagValidate"])
        traits["capacity_helper_usage"] = _has_any(text, [r"capacityCheck"])
        traits["shared_pipeline"] = traits["native_function_skeleton"] or _has_all(
            text,
            [
                r"collectLoweredFactsFromMlir\(",
                r"forEachNativeEmitterRenderStep\(",
                r"renderIR\.helperBindings",
                r"renderSectionHelperBindings\(",
                r"unionTagValidate",
                r"capacityCheck",
            ],
        )
        return traits

    if kind == "scripted":
        traits["collect_lowered_facts"] = _has_any(text, [r"collectLoweredFactsFromMlir\("])
        traits["runtime_plan"] = _has_any(text, [r"buildRuntimeSectionPlan\("])
        traits["scripted_body_plan"] = _has_any(
            text, [r"buildScriptedSectionBodyPlan\(", r"buildScriptedSectionOperationPlan\("]
        )
        traits["helper_plan_builder"] = _has_any(text, [r"buildSectionHelperBindingPlan\("])
        traits["helper_binding_render"] = _has_any(text, [r"renderSectionHelperBindings\("])
        traits["union_helper_usage"] = _has_any(text, [r"sectionHelperNames\.unionTagValidate"])
        traits["capacity_helper_usage"] = _has_any(text, [r"sectionHelperNames\.capacityCheck"])
        traits["scalar_helper_usage"] = _has_any(text, [r"helpers\.serScalar", r"helpers\.deserScalar"])
        traits["array_helper_usage"] = _has_any(
            text,
            [r"helpers\.arrayValidate", r"helpers\.serArrayPrefix", r"helpers\.deserArrayPrefix"],
        )
        traits["delimiter_helper_usage"] = _has_any(text, [r"helpers\.delimiterValidate"])
        traits["diagnostic_catalog"] = _has_any(text, [r"codegen_diagnostic_text::"])
        traits["shared_pipeline"] = _has_all(
            text,
            [
                r"collectLoweredFactsFromMlir\(",
                r"buildRuntimeSectionPlan\(",
                r"buildSectionHelperBindingPlan\(",
                r"renderSectionHelperBindings\(",
            ],
        ) and traits["scripted_body_plan"]
        return traits

    raise ValueError(f"unknown backend kind: {kind}")


def _detect_global_traits(repo_root: Path) -> Dict[str, bool]:
    traits: Dict[str, bool] = {}

    ops_text = _load_text(repo_root, "lib/IR/DSDLOps.cpp")
    traits["verifier_invariants_in_ops"] = _has_all(
        ops_text,
        [
            r"LogicalResult SerializationPlanOp::verify\(\)",
            r"LogicalResult IOOp::verify\(\)",
            r"union plan missing union_tag_bits/union_option_count metadata",
            r"unsupported 'array_kind' value",
            r"unsupported 'cast_mode' value",
        ],
    )
    lit_root = repo_root / "test/lit"
    traits["verifier_negative_lit_coverage"] = all((lit_root / name).exists() for name in VERIFIER_NEGATIVE_TEST_FILES)
    traits["verifier_first_checks"] = traits["verifier_invariants_in_ops"] and traits["verifier_negative_lit_coverage"]

    contract_validation_text = _load_text(repo_root, "lib/Transforms/LoweredSerDesContractValidation.cpp")
    lowered_facts_text = _load_text(repo_root, "lib/CodeGen/MlirLoweredFacts.cpp")
    emitc_text = _load_text(repo_root, "lib/Transforms/ConvertDSDLToEmitC.cpp")
    wire_contract_text = _load_text(repo_root, "include/llvmdsdl/CodeGen/WireOperationContract.h")
    render_ir_text = _load_text(repo_root, "lib/CodeGen/LoweredRenderIR.cpp")
    runtime_plan_text = _load_text(repo_root, "lib/CodeGen/RuntimeLoweredPlan.cpp")
    scripted_plan_text = _load_text(repo_root, "lib/CodeGen/ScriptedOperationPlan.cpp")

    traits["contract_v2_validation_api"] = _has_all(
        contract_validation_text,
        [
            r"findLoweredContractEnvelopeViolation",
            r"findLoweredPlanContractViolation",
            r"kLoweredSerDesContractVersionAttr",
            r"kLoweredSerDesContractProducerAttr",
        ],
    )
    traits["contract_v2_codegen_consumer"] = _has_all(
        lowered_facts_text,
        [
            r"findLoweredContractEnvelopeViolation",
            r"findLoweredPlanContractViolation",
        ],
    ) and _has_any(
        lowered_facts_text,
        [
            r"kLoweredSerDesContractMajor",
            r"loweredSerDesUnsupportedMajorVersionDiagnosticDetail",
        ],
    )
    traits["contract_v2_c_consumer"] = _has_all(
        emitc_text,
        [
            r"findLoweredContractEnvelopeViolation",
            r"findLoweredPlanContractViolation",
        ],
    ) and _has_any(
        emitc_text,
        [
            r"kLoweredSerDesContractMajor",
            r"loweredSerDesUnsupportedMajorVersionDiagnosticDetail",
        ],
    )
    traits["contract_v2_wire_operation_contract_marker"] = _has_all(
        wire_contract_text,
        [
            r"kWireOperationContractMajor",
            r"kWireOperationContractVersion",
            r"wireOperationUnsupportedMajorVersionDiagnosticDetail",
        ],
    )
    traits["contract_v2_wire_operation_contract_consumers"] = _has_all(
        render_ir_text,
        [
            r"validateLoweredBodyRenderIRContract",
            r"unsupported wire-operation contract major version",
        ],
    ) and _has_all(
        runtime_plan_text,
        [
            r"validateRuntimeSectionPlanContract",
            r"validateLoweredBodyRenderIRContract",
        ],
    ) and _has_all(
        scripted_plan_text,
        [
            r"validateScriptedSectionOperationPlanContract",
            r"validateRuntimeSectionPlanContract",
        ],
    )
    traits["contract_v2_checks"] = (
        traits["contract_v2_validation_api"]
        and traits["contract_v2_codegen_consumer"]
        and traits["contract_v2_c_consumer"]
        and traits["contract_v2_wire_operation_contract_marker"]
        and traits["contract_v2_wire_operation_contract_consumers"]
    )

    native_helper_contract_tests = _load_text(repo_root, "test/unit/NativeHelperContractTests.cpp")
    helper_binding_render_tests = _load_text(repo_root, "test/unit/HelperBindingRenderTests.cpp")
    unit_main_text = _load_text(repo_root, "test/unit/UnitMain.cpp")
    unit_cmake_text = _load_text(repo_root, "test/unit/CMakeLists.txt")
    traits["helper_binding_unit_contract_coverage"] = _has_all(
        native_helper_contract_tests,
        [
            r"runNativeHelperContractTests\(\)",
            r"missing scalar helper bindings",
            r"missing array validate helper binding",
            r"missing delimiter helper binding",
        ],
    )
    traits["helper_binding_render_coverage"] = _has_all(
        helper_binding_render_tests,
        [
            r"runHelperBindingRenderTests\(\)",
            r"renderSectionHelperBindings\(",
            r"go section helper binding render mismatch",
            r"section helper binding render mismatch",
        ],
    )
    traits["helper_binding_unit_wiring"] = _has_all(
        unit_main_text,
        [
            r"runNativeHelperContractTests\(\)",
            r"runHelperBindingRenderTests\(\)",
        ],
    ) and _has_all(
        unit_cmake_text,
        [
            r"NativeHelperContractTests\.cpp",
            r"HelperBindingRenderTests\.cpp",
        ],
    )
    traits["helper_binding_completeness_checks"] = (
        traits["helper_binding_unit_contract_coverage"]
        and traits["helper_binding_render_coverage"]
        and traits["helper_binding_unit_wiring"]
    )

    return traits


def _classifications(kind: str, traits: Dict[str, bool], global_traits: Dict[str, bool]) -> Dict[str, str]:
    classes = {name: "backend-local" for name, _ in SEMANTIC_CLASSES}

    if kind == "c":
        if traits["emitc_pipeline"]:
            for name, _ in SEMANTIC_CLASSES[:8]:
                classes[name] = "shared"
        if traits.get("diagnostic_catalog", False):
            classes["malformed_input_diagnostic_text"] = "shared"
        else:
            classes["malformed_input_diagnostic_text"] = "backend-local"
        if traits["emitc_pipeline"] and traits["schema_scan"] and traits["schema_selection_guard"]:
            classes["lowered_contract_validation"] = "shared"
    elif kind == "native":
        if traits["shared_pipeline"]:
            for name, _ in SEMANTIC_CLASSES[:8]:
                classes[name] = "shared"
        if traits["helper_binding_render"]:
            classes["malformed_input_diagnostic_text"] = "shared"
        if traits["collect_lowered_facts"]:
            classes["lowered_contract_validation"] = "shared"
    elif kind == "scripted":
        if traits["runtime_plan"]:
            classes["field_step_ordering"] = "shared"
        if traits["union_helper_usage"] and traits["helper_binding_render"]:
            classes["union_tag_mask_validate"] = "shared"
        if traits["scalar_helper_usage"] and traits["helper_binding_render"]:
            classes["scalar_normalize_sign_extend"] = "shared"
        if traits["array_helper_usage"]:
            classes["variable_array_prefix_validate"] = "shared"
            classes["fixed_array_cardinality_validate"] = "shared"
        if traits["delimiter_helper_usage"]:
            classes["delimited_payload_validate"] = "shared"
        if traits["capacity_helper_usage"]:
            classes["section_capacity_precheck"] = "shared"
        if traits["scripted_body_plan"]:
            classes["alignment_padding_orchestration"] = "shared"
        if traits["diagnostic_catalog"]:
            classes["malformed_input_diagnostic_text"] = "shared"
        if traits["collect_lowered_facts"]:
            classes["lowered_contract_validation"] = "shared"
    else:
        raise ValueError(f"unknown backend kind: {kind}")

    if global_traits.get("verifier_first_checks", False):
        classes["verifier_first_invariants"] = "shared"

    if global_traits.get("helper_binding_completeness_checks", False) and traits.get("semantic_gate_helper_checks", False):
        classes["helper_binding_completeness"] = "shared"

    if global_traits.get("contract_v2_checks", False):
        if kind == "c" and traits.get("emitc_pipeline", False):
            classes["contract_v2_usage"] = "shared"
        if kind in {"native", "scripted"} and traits.get("collect_lowered_facts", False):
            classes["contract_v2_usage"] = "shared"

    if traits.get("semantic_gate", False):
        classes["fallback_free_semantic_execution"] = "shared"

    return classes


def _build_report(repo_root: Path) -> Dict[str, object]:
    backends: Dict[str, object] = {}
    per_backend_scores: Dict[str, int] = {}
    total_classes = len(SEMANTIC_CLASSES)
    global_traits = _detect_global_traits(repo_root)

    for backend_name, cfg in BACKEND_CONFIG.items():
        emitter_text = _load_text(repo_root, cfg["path"])
        traits = _detect_traits(repo_root, cfg, emitter_text)
        classifications = _classifications(cfg["kind"], traits, global_traits)
        shared_count = sum(1 for status in classifications.values() if status == "shared")
        score = int(round((shared_count * 100.0) / total_classes))
        per_backend_scores[backend_name] = score

        backends[backend_name] = {
            "kind": cfg["kind"],
            "emitter": cfg["path"],
            "score": score,
            "shared_classes": shared_count,
            "total_classes": total_classes,
            "classifications": classifications,
            "traits": traits,
        }

    return {
        "version": 1,
        "generated_at_utc": datetime.now(timezone.utc).isoformat(),
        "semantic_classes": [{"id": class_id, "label": label} for class_id, label in SEMANTIC_CLASSES],
        "global_traits": global_traits,
        "backends": backends,
        "project_floor_score": min(per_backend_scores.values()),
    }


def _write_json(path: Path, data: Dict[str, object]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def _write_markdown(path: Path, report: Dict[str, object]) -> None:
    backends = report["backends"]
    lines: List[str] = []
    lines.append("# Shared-Infrastructure Consistency Lint")
    lines.append("")
    lines.append("Generated by `tools/convergence/convergence_report.py`.")
    lines.append("")
    lines.append(
        "> **What this is.** A *static lint* that verifies each backend emitter references "
        "the shared planning/lowering helpers and gates (field-step ordering, union-tag "
        "validation, lowered-contract validation, and so on). A `shared` mark means the "
        "marker for that class is present in the emitter source: it certifies "
        "**infrastructure consistency, not runtime behavioral equivalence.** Behavioral "
        "equivalence is enforced separately: emitted *step order* by the **emit-order "
        "verifier** (`tools/convergence/emit_order_verifier.py`, ctest "
        "`llvmdsdl-emit-order-verifier`), which compares each string backend's abstract "
        "serialize/deserialize op trace per (type, direction) against the equivalence class "
        "proven safe by the Dafny oracle (`spec/dafny/CyphalSerdes.dfy`); and *wire bytes* "
        "by the parity and malformed-input gates, which consume executed test pass/fail. "
        "Read each number here as *markers present*, not as a correctness proof or a "
        "convergence guarantee."
    )
    lines.append("")
    lines.append("## Summary")
    lines.append("")
    lines.append("| Backend | Markers present | Classes shared / total |")
    lines.append("| --- | ---: | ---: |")
    for backend_name in sorted(backends):
        backend = backends[backend_name]
        lines.append(
            f"| `{backend_name}` | `{backend['score']}` | `{backend['shared_classes']} / {backend['total_classes']}` |"
        )
    lines.append("")
    lines.append(f"Project floor (minimum markers-present score across backends): `{report['project_floor_score']}`")
    lines.append("")
    lines.append("## Per-Backend Classifications")
    lines.append("")
    class_labels = {entry["id"]: entry["label"] for entry in report["semantic_classes"]}
    for backend_name in sorted(backends):
        backend = backends[backend_name]
        lines.append(f"### `{backend_name}`")
        lines.append("")
        lines.append("| Semantic class | Status |")
        lines.append("| --- | --- |")
        for class_id, _ in SEMANTIC_CLASSES:
            lines.append(f"| `{class_labels[class_id]}` | `{backend['classifications'][class_id]}` |")
        lines.append("")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def _check_baseline(report: Dict[str, object], baseline_path: Path) -> List[str]:
    baseline = json.loads(baseline_path.read_text(encoding="utf-8"))
    failures: List[str] = []
    baseline_expected = baseline.get("expected", {})
    report_backends = report["backends"]
    semantic_class_ids = {class_id for class_id, _ in SEMANTIC_CLASSES}
    required_backend_ids = set(BACKEND_CONFIG.keys())

    report_backend_ids = set(report_backends.keys())
    missing_report_backends = sorted(required_backend_ids - report_backend_ids)
    unexpected_report_backends = sorted(report_backend_ids - required_backend_ids)
    if missing_report_backends:
        failures.append(f"report missing required backends: {', '.join(missing_report_backends)}")
    if unexpected_report_backends:
        failures.append(f"report contains unexpected backends: {', '.join(unexpected_report_backends)}")

    baseline_backend_ids = set(baseline_expected.keys())
    missing_baseline_backends = sorted(required_backend_ids - baseline_backend_ids)
    unexpected_baseline_backends = sorted(baseline_backend_ids - required_backend_ids)
    if missing_baseline_backends:
        failures.append(f"baseline missing required backends: {', '.join(missing_baseline_backends)}")
    if unexpected_baseline_backends:
        failures.append(f"baseline contains unexpected backends: {', '.join(unexpected_baseline_backends)}")

    for backend_name, expected_classes in baseline_expected.items():
        if not isinstance(expected_classes, dict):
            failures.append(f"baseline backend '{backend_name}' expected classifications must be an object")
            continue
        expected_ids = set(expected_classes.keys())
        missing_ids = sorted(semantic_class_ids - expected_ids)
        unexpected_ids = sorted(expected_ids - semantic_class_ids)
        if missing_ids:
            failures.append(f"baseline backend '{backend_name}' missing semantic classes: {', '.join(missing_ids)}")
        if unexpected_ids:
            failures.append(f"baseline backend '{backend_name}' has unknown semantic classes: {', '.join(unexpected_ids)}")
        for class_id, status in expected_classes.items():
            if status != "shared":
                failures.append(
                    f"baseline backend '{backend_name}' class '{class_id}' must be 'shared', found '{status}'"
                )

    for backend_name, expected_classes in baseline_expected.items():
        if backend_name not in report_backends:
            failures.append(f"baseline backend missing from report: {backend_name}")
            continue
        actual_classes = report_backends[backend_name]["classifications"]
        actual_class_ids = set(actual_classes.keys())
        missing_class_ids = sorted(semantic_class_ids - actual_class_ids)
        unexpected_class_ids = sorted(actual_class_ids - semantic_class_ids)
        if missing_class_ids:
            failures.append(f"report backend '{backend_name}' missing semantic classes: {', '.join(missing_class_ids)}")
        if unexpected_class_ids:
            failures.append(
                f"report backend '{backend_name}' has unknown semantic classes: {', '.join(unexpected_class_ids)}"
            )
        for class_id, expected_status in expected_classes.items():
            actual_status = actual_classes.get(class_id)
            if expected_status == "shared" and actual_status != "shared":
                failures.append(
                    f"regression: backend '{backend_name}' class '{class_id}' expected shared but found {actual_status}"
                )

    minimum_scores = baseline.get("minimum_scores", {})
    for backend_name, minimum_score in minimum_scores.get("backends", {}).items():
        if backend_name not in report_backends:
            failures.append(f"minimum score references unknown backend: {backend_name}")
            continue
        actual_score = report_backends[backend_name]["score"]
        if actual_score < int(minimum_score):
            failures.append(
                f"score regression: backend '{backend_name}' score {actual_score} is below minimum {minimum_score}"
            )

    project_floor_min = minimum_scores.get("project_floor")
    if project_floor_min is not None:
        actual_project_floor = int(report["project_floor_score"])
        if actual_project_floor < int(project_floor_min):
            failures.append(
                f"score regression: project floor {actual_project_floor} is below minimum {project_floor_min}"
            )
    return failures


def parse_args(argv: List[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Generate convergence score report for llvm-dsdl backends.")
    parser.add_argument("--repo-root", required=True, help="Path to repository root.")
    parser.add_argument("--output-json", help="Path for JSON report output.")
    parser.add_argument("--output-md", help="Path for Markdown scorecard output.")
    parser.add_argument("--baseline", help="Path to baseline JSON used for regression checks.")
    parser.add_argument(
        "--check-regressions",
        action="store_true",
        help="Fail if baseline shared classifications regress or score minimums are violated.",
    )
    return parser.parse_args(argv)


def main(argv: List[str]) -> int:
    args = parse_args(argv)
    repo_root = Path(args.repo_root).resolve()
    report = _build_report(repo_root)

    if args.output_json:
        _write_json(Path(args.output_json), report)
    if args.output_md:
        _write_markdown(Path(args.output_md), report)

    if args.check_regressions:
        if not args.baseline:
            print("error: --check-regressions requires --baseline", file=sys.stderr)
            return 2
        baseline_path = Path(args.baseline).resolve()
        if not baseline_path.exists():
            print(f"error: baseline not found: {baseline_path}", file=sys.stderr)
            return 2
        failures = _check_baseline(report, baseline_path)
        if failures:
            for failure in failures:
                print(f"convergence regression: {failure}", file=sys.stderr)
            return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
