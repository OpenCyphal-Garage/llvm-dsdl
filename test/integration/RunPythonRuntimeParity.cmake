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

set(PY_PACKAGE "llvmdsdl_py_parity")
if(NOT DEFINED PY_RUNTIME_SPECIALIZATION OR "${PY_RUNTIME_SPECIALIZATION}" STREQUAL "")
  set(PY_RUNTIME_SPECIALIZATION "portable")
endif()
if(NOT "${PY_RUNTIME_SPECIALIZATION}" STREQUAL "portable" AND
   NOT "${PY_RUNTIME_SPECIALIZATION}" STREQUAL "fast")
  message(FATAL_ERROR "Invalid PY_RUNTIME_SPECIALIZATION value: ${PY_RUNTIME_SPECIALIZATION}")
endif()
set(py_package_path "${PY_PACKAGE}")
string(REPLACE "." "/" py_package_path "${py_package_path}")
set(package_root "${OUT_DIR}/${py_package_path}")

file(REMOVE_RECURSE "${OUT_DIR}")
file(MAKE_DIRECTORY "${OUT_DIR}")

execute_process(
  COMMAND "${DSDLC}" --target-language python --versioned-type-names
    "${FIXTURES_ROOT}"
    --outdir "${OUT_DIR}"
    --py-package "${PY_PACKAGE}"
    --py-runtime-specialization "${PY_RUNTIME_SPECIALIZATION}"
  RESULT_VARIABLE gen_result
  OUTPUT_VARIABLE gen_stdout
  ERROR_VARIABLE gen_stderr
)
if(NOT gen_result EQUAL 0)
  message(STATUS "dsdlc stdout:\n${gen_stdout}")
  message(STATUS "dsdlc stderr:\n${gen_stderr}")
  message(FATAL_ERROR "Python runtime parity generation failed")
endif()

set(has_accel FALSE)
if(DEFINED ACCEL_MODULE AND NOT "${ACCEL_MODULE}" STREQUAL "" AND EXISTS "${ACCEL_MODULE}")
  set(has_accel TRUE)
  file(COPY "${ACCEL_MODULE}" DESTINATION "${package_root}")
endif()

set(require_accel FALSE)
if(DEFINED REQUIRE_ACCEL)
  if(REQUIRE_ACCEL)
    set(require_accel TRUE)
  endif()
endif()

if(require_accel AND NOT has_accel)
  message(FATAL_ERROR
    "Python runtime parity test requires accelerator module, but ACCEL_MODULE is missing.")
endif()

set(parity_script "${OUT_DIR}/python_runtime_parity.py")
file(WRITE
  "${parity_script}"
  "from __future__ import annotations\n"
  "\n"
  "import importlib\n"
  "import os\n"
  "from typing import Dict\n"
  "\n"
  "PKG = 'llvmdsdl_py_parity'\n"
  "\n"
  "def reset_modules() -> None:\n"
  "    for name in list(importlib.sys.modules):\n"
  "        if name.startswith(PKG):\n"
  "            del importlib.sys.modules[name]\n"
  "\n"
  "def run_fixture_corpus(mode: str) -> Dict[str, bytes]:\n"
  "    os.environ['LLVMDSDL_PY_RUNTIME_MODE'] = mode\n"
  "    reset_modules()\n"
  "\n"
  "    Widget_1_0 = importlib.import_module(f'{PKG}.fixtures.vendor.widget_1_0').Widget_1_0\n"
  "    Helpers_1_0 = importlib.import_module(f'{PKG}.fixtures.vendor.helpers_1_0').Helpers_1_0\n"
  "    UnionTag_1_0 = importlib.import_module(f'{PKG}.fixtures.vendor.union_tag_1_0').UnionTag_1_0\n"
  "    Delimited_1_0 = importlib.import_module(f'{PKG}.fixtures.vendor.delimited_1_0').Delimited_1_0\n"
  "    UsesDelimited_1_0 = importlib.import_module(f'{PKG}.fixtures.vendor.uses_delimited_1_0').UsesDelimited_1_0\n"
  "    EmptyServiceReq = importlib.import_module(\n"
  "        f'{PKG}.fixtures.vendor.empty_service_1_0').EmptyService_1_0_Request\n"
  "    EmptyServiceResp = importlib.import_module(\n"
  "        f'{PKG}.fixtures.vendor.empty_service_1_0').EmptyService_1_0_Response\n"
  "\n"
  "    values = {\n"
  "        'type': Widget_1_0(foo=10, bar=513),\n"
  "        'helpers': Helpers_1_0(a=-7, b=1.5, c=[1, 2, 3, 4, 5]),\n"
  "        'union': UnionTag_1_0(_tag=1, second=1027),\n"
  "        'nested': UsesDelimited_1_0(nested=Delimited_1_0(value=42)),\n"
  "        'service_req': EmptyServiceReq(request_value=9),\n"
  "        'service_resp': EmptyServiceResp(),\n"
  "    }\n"
  "\n"
  "    encoded: Dict[str, bytes] = {}\n"
  "    for name, obj in values.items():\n"
  "        payload = obj.serialize()\n"
  "        restored = obj.__class__.deserialize(payload)\n"
  "        assert restored.serialize() == payload, (mode, name)\n"
  "        encoded[name] = payload\n"
  "    return encoded\n"
  "\n"
  "pure_outputs = run_fixture_corpus('pure')\n"
  "\n"
)

if(has_accel)
  file(APPEND
    "${parity_script}"
    "accel_outputs = run_fixture_corpus('accel')\n"
    "assert pure_outputs.keys() == accel_outputs.keys()\n"
    "for key in pure_outputs:\n"
    "    assert pure_outputs[key] == accel_outputs[key], key\n"
    "print('python-runtime-parity-ok accel')\n"
  )
else()
  file(APPEND
    "${parity_script}"
    "reset_modules()\n"
    "os.environ['LLVMDSDL_PY_RUNTIME_MODE'] = 'accel'\n"
    "failed = False\n"
    "try:\n"
    "    importlib.import_module(f'{PKG}._runtime_loader')\n"
    "except RuntimeError:\n"
    "    failed = True\n"
    "assert failed\n"
    "print('python-runtime-parity-ok pure-only')\n"
  )
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
    "PYTHONPATH=${OUT_DIR}"
    "${PYTHON_EXECUTABLE}" "${parity_script}"
  RESULT_VARIABLE parity_result
  OUTPUT_VARIABLE parity_stdout
  ERROR_VARIABLE parity_stderr
)
if(NOT parity_result EQUAL 0)
  message(STATUS "python parity stdout:\n${parity_stdout}")
  message(STATUS "python parity stderr:\n${parity_stderr}")
  message(FATAL_ERROR "Python runtime parity test failed")
endif()

message(STATUS "Python runtime parity passed")
