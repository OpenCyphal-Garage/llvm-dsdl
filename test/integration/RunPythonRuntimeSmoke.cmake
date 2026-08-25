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

set(PY_PACKAGE "llvmdsdl_py_smoke")
if(NOT DEFINED PY_RUNTIME_SPECIALIZATION OR "${PY_RUNTIME_SPECIALIZATION}" STREQUAL "")
  set(PY_RUNTIME_SPECIALIZATION "portable")
endif()
if(NOT "${PY_RUNTIME_SPECIALIZATION}" STREQUAL "portable" AND
   NOT "${PY_RUNTIME_SPECIALIZATION}" STREQUAL "fast")
  message(FATAL_ERROR "Invalid PY_RUNTIME_SPECIALIZATION value: ${PY_RUNTIME_SPECIALIZATION}")
endif()
set(py_package_path "${PY_PACKAGE}")
string(REPLACE "." "/" py_package_path "${py_package_path}")

file(REMOVE_RECURSE "${OUT_DIR}")
file(MAKE_DIRECTORY "${OUT_DIR}")

set(dsdlc_args
  --target-language python
  # The smoke harness below is written against versioned type names.
  --versioned-type-names
  "${FIXTURES_ROOT}"
  --outdir "${OUT_DIR}"
  --py-package "${PY_PACKAGE}"
  --py-runtime-specialization "${PY_RUNTIME_SPECIALIZATION}"
)
if(DEFINED DSDLC_EXTRA_ARGS AND NOT "${DSDLC_EXTRA_ARGS}" STREQUAL "")
  separate_arguments(extra_args NATIVE_COMMAND "${DSDLC_EXTRA_ARGS}")
  list(INSERT dsdlc_args 2 ${extra_args})
endif()

execute_process(
  COMMAND "${DSDLC}" ${dsdlc_args}
  RESULT_VARIABLE gen_result
  OUTPUT_VARIABLE gen_stdout
  ERROR_VARIABLE gen_stderr
)
if(NOT gen_result EQUAL 0)
  message(STATUS "dsdlc stdout:\n${gen_stdout}")
  message(STATUS "dsdlc stderr:\n${gen_stderr}")
  message(FATAL_ERROR "fixtures Python runtime smoke generation failed")
endif()

set(smoke_script "${OUT_DIR}/python_runtime_smoke.py")
file(WRITE
  "${smoke_script}"
  "from __future__ import annotations\n"
  "\n"
  "from llvmdsdl_py_smoke.fixtures.vendor.delimited_1_0 import Delimited_1_0\n"
  "from llvmdsdl_py_smoke.fixtures.vendor.empty_service_1_0 import EmptyService_1_0_Request, EmptyService_1_0_Response\n"
  "from llvmdsdl_py_smoke.fixtures.vendor.helpers_1_0 import Helpers_1_0\n"
  "from llvmdsdl_py_smoke.fixtures.vendor.widget_1_0 import Widget_1_0\n"
  "from llvmdsdl_py_smoke.fixtures.vendor.union_tag_1_0 import UnionTag_1_0\n"
  "from llvmdsdl_py_smoke.fixtures.vendor.uses_delimited_1_0 import UsesDelimited_1_0\n"
  "\n"
  "msg = Widget_1_0(foo=10, bar=513)\n"
  "msg_roundtrip = Widget_1_0.deserialize(msg.serialize())\n"
  "assert msg_roundtrip.foo == 10\n"
  "assert msg_roundtrip.bar == 513\n"
  "\n"
  "helpers = Helpers_1_0(a=-7, b=1.5, c=[1, 2, 3, 4, 5])\n"
  "helpers_roundtrip = Helpers_1_0.deserialize(helpers.serialize())\n"
  "assert helpers_roundtrip.a == -7\n"
  "assert abs(helpers_roundtrip.b - 1.5) < 0.2\n"
  "assert helpers_roundtrip.c == [1, 2, 3, 4, 5]\n"
  "\n"
  "u = UnionTag_1_0(_tag=1, second=1027)\n"
  "u_roundtrip = UnionTag_1_0.deserialize(u.serialize())\n"
  "assert u_roundtrip._tag == 1\n"
  "assert u_roundtrip.second == 1027\n"
  "\n"
  "nested = UsesDelimited_1_0(nested=Delimited_1_0(value=42))\n"
  "nested_roundtrip = UsesDelimited_1_0.deserialize(nested.serialize())\n"
  "assert nested_roundtrip.nested.value == 42\n"
  "\n"
  "req = EmptyService_1_0_Request(request_value=9)\n"
  "req_roundtrip = EmptyService_1_0_Request.deserialize(req.serialize())\n"
  "assert req_roundtrip.request_value == 9\n"
  "\n"
  "resp = EmptyService_1_0_Response()\n"
  "resp_roundtrip = EmptyService_1_0_Response.deserialize(resp.serialize())\n"
  "assert isinstance(resp_roundtrip, EmptyService_1_0_Response)\n"
  "\n"
  "print('python-runtime-smoke-ok')\n"
)

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
    "PYTHONPATH=${OUT_DIR}"
    "LLVMDSDL_PY_RUNTIME_MODE=pure"
    "${PYTHON_EXECUTABLE}" "${smoke_script}"
  RESULT_VARIABLE smoke_result
  OUTPUT_VARIABLE smoke_stdout
  ERROR_VARIABLE smoke_stderr
)
if(NOT smoke_result EQUAL 0)
  message(STATUS "python smoke stdout:\n${smoke_stdout}")
  message(STATUS "python smoke stderr:\n${smoke_stderr}")
  message(FATAL_ERROR "fixtures Python runtime smoke failed")
endif()

message(STATUS "fixtures Python runtime smoke passed")
