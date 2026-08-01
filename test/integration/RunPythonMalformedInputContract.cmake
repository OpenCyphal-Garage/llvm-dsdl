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

set(has_accel FALSE)
if(DEFINED ACCEL_MODULE AND NOT "${ACCEL_MODULE}" STREQUAL "" AND EXISTS "${ACCEL_MODULE}")
  set(has_accel TRUE)
endif()

set(portable_out "${OUT_DIR}/portable")
set(fast_out "${OUT_DIR}/fast")
set(portable_pkg "llvmdsdl_py_contract_portable")
set(fast_pkg "llvmdsdl_py_contract_fast")

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
  message(FATAL_ERROR "portable malformed-input contract generation failed")
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
  message(FATAL_ERROR "fast malformed-input contract generation failed")
endif()

if(has_accel)
  file(COPY "${ACCEL_MODULE}" DESTINATION "${portable_out}/${portable_pkg_path}")
  file(COPY "${ACCEL_MODULE}" DESTINATION "${fast_out}/${fast_pkg_path}")
endif()

set(contract_script "${OUT_DIR}/python_malformed_input_contract.py")
file(WRITE
  "${contract_script}"
  [=[
from __future__ import annotations

import importlib
import os
import sys
from pathlib import Path

PORTABLE_OUT = Path("@PORTABLE_OUT@")
FAST_OUT = Path("@FAST_OUT@")
PORTABLE_PKG = "@PORTABLE_PKG@"
FAST_PKG = "@FAST_PKG@"
HAS_ACCEL = "@HAS_ACCEL@" == "True"


def clear_package_modules(prefix: str) -> None:
    for name in list(sys.modules):
        if name == prefix or name.startswith(prefix + "."):
            del sys.modules[name]


def load_runtime(out_dir: Path, package: str, mode: str):
    out_dir_str = str(out_dir)
    if out_dir_str not in sys.path:
        sys.path.insert(0, out_dir_str)
    os.environ["LLVMDSDL_PY_RUNTIME_MODE"] = mode
    clear_package_modules(package)
    loader = importlib.import_module(f"{package}._runtime_loader")
    return loader.BACKEND, loader.runtime


def expect_value_error(context: str, action: str, needle: str, fn) -> None:
    try:
        fn()
    except ValueError as ex:
        text = str(ex)
        assert needle in text, (context, action, text, needle)
    else:
        assert False, (context, action, "expected ValueError")


def assert_zero_extend_extract(backend: str, runtime_module, context: str) -> None:
    payload = runtime_module.extract_bits(bytes([0xAA]), 0, 16)
    assert payload == bytes([0xAA, 0x00]), (context, backend, payload)


def assert_reject_extract(backend: str, runtime_module, context: str) -> None:
    rejected = False
    try:
        runtime_module.extract_bits(bytes([0xAA]), 0, 16)
    except ValueError:
        rejected = True
    assert rejected, (context, backend)


def assert_semantic_malformed_contract(package: str, runtime_module, context: str) -> None:
    union_type = importlib.import_module(
        f"{package}.fixtures.vendor.union_tag_1_0"
    ).UnionTag_1_0
    helpers_type = importlib.import_module(
        f"{package}.fixtures.vendor.helpers_1_0"
    ).Helpers_1_0
    delimited_type = importlib.import_module(
        f"{package}.fixtures.vendor.delimited_1_0"
    ).Delimited_1_0
    uses_delimited_type = importlib.import_module(
        f"{package}.fixtures.vendor.uses_delimited_1_0"
    ).UsesDelimited_1_0

    union_payload = bytearray(union_type(_tag=1, second=1027).serialize())
    union_payload[0] = 3
    expect_value_error(
        context,
        "union-invalid-tag",
        "decoded invalid union tag",
        lambda: union_type.deserialize(bytes(union_payload)),
    )

    helpers_payload = bytearray(helpers_type(a=0, b=0.0, c=[1]).serialize())
    runtime_module.write_unsigned(helpers_payload, 29, 8, 6, False)
    expect_value_error(
        context,
        "helpers-array-length",
        "decoded length for field 'c' exceeds max length 5",
        lambda: helpers_type.deserialize(bytes(helpers_payload)),
    )

    delimited_payload = bytearray(
        uses_delimited_type(nested=delimited_type(value=42)).serialize()
    )
    delimited_payload[0] = 6
    delimited_payload[1] = 0
    delimited_payload[2] = 0
    delimited_payload[3] = 0
    expect_value_error(
        context,
        "uses-delimited-invalid-header",
        "decoded payload size for composite field 'nested' exceeds remaining buffer space",
        lambda: uses_delimited_type.deserialize(bytes(delimited_payload)),
    )


# portable + pure contract: tolerant malformed reads (zero-extend).
backend, runtime_module = load_runtime(PORTABLE_OUT, PORTABLE_PKG, "pure")
assert backend == "pure", backend
assert_zero_extend_extract(backend, runtime_module, "portable/pure")
assert_semantic_malformed_contract(PORTABLE_PKG, runtime_module, "portable/pure")

# fast + pure contract: strict byte-aligned out-of-range extract (ValueError).
backend, runtime_module = load_runtime(FAST_OUT, FAST_PKG, "pure")
assert backend == "pure", backend
assert_reject_extract(backend, runtime_module, "fast/pure")
assert_semantic_malformed_contract(FAST_PKG, runtime_module, "fast/pure")

if HAS_ACCEL:
    # accel contract currently uses accelerator runtime behaviour:
    # tolerant extract_bits for out-of-range read windows.
    backend, runtime_module = load_runtime(PORTABLE_OUT, PORTABLE_PKG, "accel")
    assert backend == "accel", backend
    assert_zero_extend_extract(backend, runtime_module, "portable/accel")
    assert_semantic_malformed_contract(PORTABLE_PKG, runtime_module, "portable/accel")

    backend, runtime_module = load_runtime(FAST_OUT, FAST_PKG, "accel")
    assert backend == "accel", backend
    assert_zero_extend_extract(backend, runtime_module, "fast/accel")
    assert_semantic_malformed_contract(FAST_PKG, runtime_module, "fast/accel")
else:
    accel_failed = False
    try:
        load_runtime(PORTABLE_OUT, PORTABLE_PKG, "accel")
    except RuntimeError:
        accel_failed = True
    assert accel_failed

    accel_failed = False
    try:
        load_runtime(FAST_OUT, FAST_PKG, "accel")
    except RuntimeError:
        accel_failed = True
    assert accel_failed

print("python-malformed-input-contract-ok")
]=]
)

file(READ "${contract_script}" contract_script_content)
string(REPLACE "@PORTABLE_OUT@" "${portable_out}" contract_script_content "${contract_script_content}")
string(REPLACE "@FAST_OUT@" "${fast_out}" contract_script_content "${contract_script_content}")
string(REPLACE "@PORTABLE_PKG@" "${portable_pkg}" contract_script_content "${contract_script_content}")
string(REPLACE "@FAST_PKG@" "${fast_pkg}" contract_script_content "${contract_script_content}")
if(has_accel)
  string(REPLACE "@HAS_ACCEL@" "True" contract_script_content "${contract_script_content}")
else()
  string(REPLACE "@HAS_ACCEL@" "False" contract_script_content "${contract_script_content}")
endif()
file(WRITE "${contract_script}" "${contract_script_content}")

execute_process(
  COMMAND "${PYTHON_EXECUTABLE}" "${contract_script}"
  RESULT_VARIABLE contract_result
  OUTPUT_VARIABLE contract_stdout
  ERROR_VARIABLE contract_stderr
)
if(NOT contract_result EQUAL 0)
  message(STATUS "malformed-input contract stdout:\n${contract_stdout}")
  message(STATUS "malformed-input contract stderr:\n${contract_stderr}")
  message(FATAL_ERROR "Python malformed-input contract test failed")
endif()

message(STATUS "Python malformed-input contract test passed")
