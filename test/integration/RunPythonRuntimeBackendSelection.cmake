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

set(PY_PACKAGE "llvmdsdl_py_backend")
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
  COMMAND "${DSDLC}" --target-language python --experimental-languages
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
  message(FATAL_ERROR "backend selection test generation failed")
endif()

set(has_accel FALSE)
if(DEFINED ACCEL_MODULE AND NOT "${ACCEL_MODULE}" STREQUAL "" AND EXISTS "${ACCEL_MODULE}")
  set(has_accel TRUE)
  get_filename_component(accel_name "${ACCEL_MODULE}" NAME)
  file(COPY "${ACCEL_MODULE}" DESTINATION "${package_root}")
  message(STATUS "Copied Python accelerator module: ${accel_name}")
endif()

set(require_accel FALSE)
if(DEFINED REQUIRE_ACCEL)
  if(REQUIRE_ACCEL)
    set(require_accel TRUE)
  endif()
endif()

if(require_accel AND NOT has_accel)
  message(FATAL_ERROR
    "Python runtime backend selection test requires accelerator module, but ACCEL_MODULE is missing.")
endif()

set(backend_script "${OUT_DIR}/python_runtime_backend_selection.py")
if(has_accel)
  set(expect_auto "accel")
else()
  set(expect_auto "pure")
endif()

file(WRITE
  "${backend_script}"
  "from __future__ import annotations\n"
  "\n"
  "import os\n"
  "\n"
  "def check_backend(mode: str, expect: str) -> None:\n"
  "    os.environ['LLVMDSDL_PY_RUNTIME_MODE'] = mode\n"
  "    import importlib\n"
  "    if 'llvmdsdl_py_backend._runtime_loader' in importlib.sys.modules:\n"
  "        del importlib.sys.modules['llvmdsdl_py_backend._runtime_loader']\n"
  "    mod = importlib.import_module('llvmdsdl_py_backend._runtime_loader')\n"
  "    assert mod.BACKEND == expect, (mode, mod.BACKEND, expect)\n"
  "\n"
  "check_backend('pure', 'pure')\n"
  "check_backend('auto', '${expect_auto}')\n"
)

if(has_accel)
  file(APPEND
    "${backend_script}"
    "check_backend('accel', 'accel')\n"
  )
else()
  file(APPEND
    "${backend_script}"
    "import importlib\n"
    "import os\n"
    "os.environ['LLVMDSDL_PY_RUNTIME_MODE'] = 'accel'\n"
    "if 'llvmdsdl_py_backend._runtime_loader' in importlib.sys.modules:\n"
    "    del importlib.sys.modules['llvmdsdl_py_backend._runtime_loader']\n"
    "failed = False\n"
    "try:\n"
    "    importlib.import_module('llvmdsdl_py_backend._runtime_loader')\n"
    "except RuntimeError:\n"
    "    failed = True\n"
    "assert failed\n"
  )
endif()

file(APPEND "${backend_script}" "print('python-runtime-backend-selection-ok')\n")

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
    "PYTHONPATH=${OUT_DIR}"
    "${PYTHON_EXECUTABLE}" "${backend_script}"
  RESULT_VARIABLE run_result
  OUTPUT_VARIABLE run_stdout
  ERROR_VARIABLE run_stderr
)
if(NOT run_result EQUAL 0)
  message(STATUS "backend selection stdout:\n${run_stdout}")
  message(STATUS "backend selection stderr:\n${run_stderr}")
  message(FATAL_ERROR "Python runtime backend selection test failed")
endif()

message(STATUS "Python runtime backend selection passed")
