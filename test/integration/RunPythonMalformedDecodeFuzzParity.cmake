cmake_minimum_required(VERSION 3.24)

foreach(var DSDLC FIXTURES_ROOT OUT_DIR PYTHON_EXECUTABLE)
  if(NOT DEFINED ${var} OR "${${var}}" STREQUAL "")
    message(FATAL_ERROR "Missing required variable: ${var}")
  endif()
endforeach()

if(NOT EXISTS "${DSDLC}")
  message(FATAL_ERROR "dsdlc executable not found: ${DSDLC}")
endif()
if(NOT EXISTS "${FIXTURES_ROOT}")
  message(FATAL_ERROR "fixtures root not found: ${FIXTURES_ROOT}")
endif()
if(NOT EXISTS "${PYTHON_EXECUTABLE}")
  message(FATAL_ERROR "python executable not found: ${PYTHON_EXECUTABLE}")
endif()

if(NOT DEFINED FUZZ_SEED OR "${FUZZ_SEED}" STREQUAL "")
  set(FUZZ_SEED "12648430")
endif()
if(NOT DEFINED FUZZ_CASES OR "${FUZZ_CASES}" STREQUAL "")
  set(FUZZ_CASES "256")
endif()

set(require_accel FALSE)
if(DEFINED REQUIRE_ACCEL)
  if(REQUIRE_ACCEL)
    set(require_accel TRUE)
  endif()
endif()

set(has_accel FALSE)
if(DEFINED ACCEL_MODULE AND NOT "${ACCEL_MODULE}" STREQUAL "" AND EXISTS "${ACCEL_MODULE}")
  set(has_accel TRUE)
endif()

if(require_accel AND NOT has_accel)
  message(FATAL_ERROR
    "Python malformed decode fuzz parity test requires accelerator module, but ACCEL_MODULE is missing.")
endif()

set(portable_out "${OUT_DIR}/portable")
set(fast_out "${OUT_DIR}/fast")
set(portable_pkg "llvmdsdl_py_fuzz_portable")
set(fast_pkg "llvmdsdl_py_fuzz_fast")
set(portable_pkg_path "${portable_pkg}")
string(REPLACE "." "/" portable_pkg_path "${portable_pkg_path}")
set(fast_pkg_path "${fast_pkg}")
string(REPLACE "." "/" fast_pkg_path "${fast_pkg_path}")

file(REMOVE_RECURSE "${OUT_DIR}")
file(MAKE_DIRECTORY "${OUT_DIR}")

execute_process(
  COMMAND "${DSDLC}" --target-language python
    "${FIXTURES_ROOT}"
    --outdir "${portable_out}"
    --py-package "${portable_pkg}"
    --py-runtime-specialization portable
  RESULT_VARIABLE portable_result
  OUTPUT_VARIABLE portable_stdout
  ERROR_VARIABLE portable_stderr
)
if(NOT portable_result EQUAL 0)
  message(STATUS "portable generation stdout:\n${portable_stdout}")
  message(STATUS "portable generation stderr:\n${portable_stderr}")
  message(FATAL_ERROR "portable malformed decode fuzz parity generation failed")
endif()

execute_process(
  COMMAND "${DSDLC}" --target-language python
    "${FIXTURES_ROOT}"
    --outdir "${fast_out}"
    --py-package "${fast_pkg}"
    --py-runtime-specialization fast
  RESULT_VARIABLE fast_result
  OUTPUT_VARIABLE fast_stdout
  ERROR_VARIABLE fast_stderr
)
if(NOT fast_result EQUAL 0)
  message(STATUS "fast generation stdout:\n${fast_stdout}")
  message(STATUS "fast generation stderr:\n${fast_stderr}")
  message(FATAL_ERROR "fast malformed decode fuzz parity generation failed")
endif()

if(has_accel)
  file(COPY "${ACCEL_MODULE}" DESTINATION "${portable_out}/${portable_pkg_path}")
  file(COPY "${ACCEL_MODULE}" DESTINATION "${fast_out}/${fast_pkg_path}")
endif()

set(fuzz_script "${OUT_DIR}/python_malformed_decode_fuzz_parity.py")
file(WRITE
  "${fuzz_script}"
  [=[
from __future__ import annotations

import importlib
import os
import random
import sys
from dataclasses import dataclass
from typing import Dict, List, Tuple

PORTABLE_OUT = r"@PORTABLE_OUT@"
FAST_OUT = r"@FAST_OUT@"
PORTABLE_PKG = "@PORTABLE_PKG@"
FAST_PKG = "@FAST_PKG@"
HAS_ACCEL = @HAS_ACCEL@
REQUIRE_ACCEL = @REQUIRE_ACCEL@
FUZZ_SEED = @FUZZ_SEED@
FUZZ_CASES = @FUZZ_CASES@


@dataclass
class Variant:
    name: str
    output_root: str
    package: str
    runtime_mode: str
    backend: str
    classes: Dict[str, type]


def reset_package_modules(package: str) -> None:
    for module_name in list(sys.modules):
        if module_name == package or module_name.startswith(package + "."):
            del sys.modules[module_name]


def ensure_on_path(path: str) -> None:
    if path not in sys.path:
        sys.path.insert(0, path)


def load_variant(name: str, output_root: str, package: str, runtime_mode: str) -> Variant:
    ensure_on_path(output_root)
    os.environ["LLVMDSDL_PY_RUNTIME_MODE"] = runtime_mode
    reset_package_modules(package)

    runtime_loader = importlib.import_module(f"{package}._runtime_loader")
    classes: Dict[str, type] = {}
    classes["type"] = importlib.import_module(
        f"{package}.fixtures.vendor.widget_1_0"
    ).Widget_1_0
    classes["helpers"] = importlib.import_module(
        f"{package}.fixtures.vendor.helpers_1_0"
    ).Helpers_1_0
    classes["union"] = importlib.import_module(
        f"{package}.fixtures.vendor.union_tag_1_0"
    ).UnionTag_1_0
    classes["delimited"] = importlib.import_module(
        f"{package}.fixtures.vendor.delimited_1_0"
    ).Delimited_1_0
    classes["uses_delimited"] = importlib.import_module(
        f"{package}.fixtures.vendor.uses_delimited_1_0"
    ).UsesDelimited_1_0
    empty_service_mod = importlib.import_module(
        f"{package}.fixtures.vendor.empty_service_1_0"
    )
    classes["empty_req"] = empty_service_mod.EmptyService_1_0_Request
    classes["empty_resp"] = empty_service_mod.EmptyService_1_0_Response
    return Variant(
        name=name,
        output_root=output_root,
        package=package,
        runtime_mode=runtime_mode,
        backend=getattr(runtime_loader, "BACKEND", "unknown"),
        classes=classes,
    )


def build_baseline_payloads(portable: Variant) -> Dict[str, bytes]:
    samples: Dict[str, object] = {}
    samples["type"] = portable.classes["type"](foo=10, bar=513)
    samples["helpers"] = portable.classes["helpers"](a=-7, b=1.5, c=[1, 2, 3, 4, 5])
    samples["union"] = portable.classes["union"](_tag=1, second=1027)
    samples["delimited"] = portable.classes["delimited"](value=42)
    samples["uses_delimited"] = portable.classes["uses_delimited"](
        nested=portable.classes["delimited"](value=17)
    )
    samples["empty_req"] = portable.classes["empty_req"](request_value=9)
    samples["empty_resp"] = portable.classes["empty_resp"]()
    return {name: value.serialize() for name, value in samples.items()}


def decode_result(variant: Variant, type_name: str, payload: bytes) -> Tuple[str, object]:
    type_class = variant.classes[type_name]
    try:
        decoded = type_class.deserialize(payload)
        return ("ok", decoded.serialize())
    except Exception as ex:  # noqa: BLE001 - integration harness should expose unexpected exception classes.
        return ("err", ex)


def assert_subset_semantics(
    base_name: str,
    base_result: Tuple[str, object],
    other_name: str,
    other_result: Tuple[str, object],
    case_label: str,
) -> None:
    if other_result[0] == "ok":
        assert base_result[0] == "ok", (
            case_label,
            f"{other_name} accepted malformed payload while {base_name} rejected it",
        )
        assert other_result[1] == base_result[1], (
            case_label,
            f"{other_name} decode output diverged from {base_name}",
        )
        return

    ex = other_result[1]
    assert isinstance(ex, ValueError), (
        case_label,
        f"{other_name} raised unexpected exception class: {type(ex).__name__}",
    )


def build_fuzz_cases(payloads: Dict[str, bytes], seed: int, random_cases: int) -> List[Tuple[str, bytes, str]]:
    rng = random.Random(seed)
    cases: List[Tuple[str, bytes, str]] = []
    for type_name, payload in payloads.items():
        for cut in range(len(payload)):
            cases.append((type_name, payload[:cut], f"{type_name}:truncate:{cut}"))

    type_names = list(payloads.keys())
    for index in range(random_cases):
        type_name = rng.choice(type_names)
        base_payload = payloads[type_name]
        strategy = rng.randrange(6)

        if strategy == 0:
            if len(base_payload) == 0:
                mutated = bytes([rng.randrange(256)])
            else:
                mutated_bytes = bytearray(base_payload)
                offset = rng.randrange(len(mutated_bytes))
                mutated_bytes[offset] ^= 1 << rng.randrange(8)
                mutated = bytes(mutated_bytes)
        elif strategy == 1:
            if len(base_payload) == 0:
                mutated = bytes([rng.randrange(256), rng.randrange(256)])
            else:
                mutated_bytes = bytearray(base_payload)
                offset = rng.randrange(len(mutated_bytes))
                mutated_bytes[offset] = rng.randrange(256)
                mutated = bytes(mutated_bytes)
        elif strategy == 2:
            extra = bytes(rng.randrange(256) for _ in range(rng.randrange(1, 5)))
            mutated = base_payload + extra
        elif strategy == 3:
            if len(base_payload) == 0:
                mutated = b""
            else:
                new_length = rng.randrange(len(base_payload))
                mutated = base_payload[:new_length]
        elif strategy == 4:
            random_length = rng.randrange(0, len(base_payload) + 5)
            mutated = bytes(rng.randrange(256) for _ in range(random_length))
        else:
            mutated_bytes = bytearray(base_payload)
            if len(mutated_bytes) >= 2:
                first = rng.randrange(len(mutated_bytes))
                second = rng.randrange(len(mutated_bytes))
                mutated_bytes[first], mutated_bytes[second] = (
                    mutated_bytes[second],
                    mutated_bytes[first],
                )
            else:
                mutated_bytes.append(rng.randrange(256))
            mutated = bytes(mutated_bytes)

        cases.append((type_name, mutated, f"{type_name}:fuzz:{index}:strategy:{strategy}"))

    return cases


portable_variant = load_variant("portable", PORTABLE_OUT, PORTABLE_PKG, "pure")
assert portable_variant.backend == "pure", portable_variant.backend

fast_variant = load_variant("fast", FAST_OUT, FAST_PKG, "pure")
assert fast_variant.backend == "pure", fast_variant.backend

accel_variant = None
if HAS_ACCEL:
    accel_variant = load_variant("accel", PORTABLE_OUT, PORTABLE_PKG, "accel")
    assert accel_variant.backend == "accel", accel_variant.backend
else:
    failed = False
    try:
        load_variant("accel-missing", PORTABLE_OUT, PORTABLE_PKG, "accel")
    except RuntimeError:
        failed = True
    assert failed
    assert not REQUIRE_ACCEL

baseline_payloads = build_baseline_payloads(portable_variant)
for type_name, payload in baseline_payloads.items():
    portable_ok = decode_result(portable_variant, type_name, payload)
    fast_ok = decode_result(fast_variant, type_name, payload)
    assert portable_ok[0] == "ok", ("baseline portable decode failed", type_name)
    assert fast_ok[0] == "ok", ("baseline fast decode failed", type_name)
    assert portable_ok[1] == payload, ("portable baseline payload mismatch", type_name)
    assert fast_ok[1] == payload, ("fast baseline payload mismatch", type_name)
    if accel_variant is not None:
        accel_ok = decode_result(accel_variant, type_name, payload)
        assert accel_ok[0] == "ok", ("baseline accel decode failed", type_name)
        assert accel_ok[1] == payload, ("accel baseline payload mismatch", type_name)

fuzz_cases = build_fuzz_cases(baseline_payloads, FUZZ_SEED, FUZZ_CASES)
for type_name, payload, case_label in fuzz_cases:
    portable_result = decode_result(portable_variant, type_name, payload)
    if portable_result[0] == "err":
        assert isinstance(portable_result[1], ValueError), (
            case_label,
            f"portable raised unexpected exception class: {type(portable_result[1]).__name__}",
        )

    fast_result = decode_result(fast_variant, type_name, payload)
    assert_subset_semantics("portable", portable_result, "fast", fast_result, case_label)

    if accel_variant is not None:
        accel_result = decode_result(accel_variant, type_name, payload)
        assert_subset_semantics("portable", portable_result, "accel", accel_result, case_label)

print(
    f"python-malformed-decode-fuzz-parity-ok "
    f"cases={len(fuzz_cases)} seed={FUZZ_SEED} "
    f"accel={'on' if accel_variant is not None else 'off'}"
)
]=]
)

file(READ "${fuzz_script}" fuzz_script_content)
string(REPLACE "@PORTABLE_OUT@" "${portable_out}" fuzz_script_content "${fuzz_script_content}")
string(REPLACE "@FAST_OUT@" "${fast_out}" fuzz_script_content "${fuzz_script_content}")
string(REPLACE "@PORTABLE_PKG@" "${portable_pkg}" fuzz_script_content "${fuzz_script_content}")
string(REPLACE "@FAST_PKG@" "${fast_pkg}" fuzz_script_content "${fuzz_script_content}")
if(has_accel)
  string(REPLACE "@HAS_ACCEL@" "True" fuzz_script_content "${fuzz_script_content}")
else()
  string(REPLACE "@HAS_ACCEL@" "False" fuzz_script_content "${fuzz_script_content}")
endif()
if(require_accel)
  string(REPLACE "@REQUIRE_ACCEL@" "True" fuzz_script_content "${fuzz_script_content}")
else()
  string(REPLACE "@REQUIRE_ACCEL@" "False" fuzz_script_content "${fuzz_script_content}")
endif()
string(REPLACE "@FUZZ_SEED@" "${FUZZ_SEED}" fuzz_script_content "${fuzz_script_content}")
string(REPLACE "@FUZZ_CASES@" "${FUZZ_CASES}" fuzz_script_content "${fuzz_script_content}")
file(WRITE "${fuzz_script}" "${fuzz_script_content}")

execute_process(
  COMMAND "${PYTHON_EXECUTABLE}" "${fuzz_script}"
  RESULT_VARIABLE fuzz_result
  OUTPUT_VARIABLE fuzz_stdout
  ERROR_VARIABLE fuzz_stderr
)
if(NOT fuzz_result EQUAL 0)
  message(STATUS "malformed decode fuzz parity stdout:\n${fuzz_stdout}")
  message(STATUS "malformed decode fuzz parity stderr:\n${fuzz_stderr}")
  message(FATAL_ERROR "Python malformed decode fuzz parity test failed")
endif()

message(STATUS "Python malformed decode fuzz parity passed")
