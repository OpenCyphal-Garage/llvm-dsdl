cmake_minimum_required(VERSION 3.24)

foreach(var DSDLC FIXTURES_ROOT OUT_DIR)
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

if(NOT DEFINED PY_PACKAGE OR "${PY_PACKAGE}" STREQUAL "")
  set(PY_PACKAGE "llvmdsdl_py_generated")
endif()

set(py_package_path "${PY_PACKAGE}")
string(REPLACE "." "/" py_package_path "${py_package_path}")

file(REMOVE_RECURSE "${OUT_DIR}")
file(MAKE_DIRECTORY "${OUT_DIR}")

set(dsdlc_args
  --target-language python
  "${FIXTURES_ROOT}"
  --outdir "${OUT_DIR}"
  --py-package "${PY_PACKAGE}"
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
  message(FATAL_ERROR "fixtures Python generation failed")
endif()

set(package_root "${OUT_DIR}/${py_package_path}")
foreach(required
    "${OUT_DIR}/pyproject.toml"
    "${package_root}/__init__.py"
    "${package_root}/_dsdl_runtime.py"
    "${package_root}/_runtime_loader.py"
    "${package_root}/py.typed")
  if(NOT EXISTS "${required}")
    message(FATAL_ERROR "Missing required generated Python runtime file: ${required}")
  endif()
endforeach()

file(READ "${OUT_DIR}/pyproject.toml" pyproject_toml)
if(NOT pyproject_toml MATCHES "\\[project\\]")
  message(FATAL_ERROR "Generated pyproject.toml is missing [project]")
endif()
if(NOT pyproject_toml MATCHES "\\[tool\\.setuptools\\.package-data\\]")
  message(FATAL_ERROR "Generated pyproject.toml is missing [tool.setuptools.package-data]")
endif()
if(NOT pyproject_toml MATCHES "_dsdl_runtime_accel\\*\\.so")
  message(FATAL_ERROR "Generated pyproject.toml is missing accelerator .so package-data pattern")
endif()
if(NOT pyproject_toml MATCHES "_dsdl_runtime_accel\\*\\.dylib")
  message(FATAL_ERROR "Generated pyproject.toml is missing accelerator .dylib package-data pattern")
endif()
if(NOT pyproject_toml MATCHES "_dsdl_runtime_accel\\*\\.pyd")
  message(FATAL_ERROR "Generated pyproject.toml is missing accelerator .pyd package-data pattern")
endif()

file(GLOB_RECURSE dsdl_files "${FIXTURES_ROOT}/*.dsdl")
list(LENGTH dsdl_files dsdl_count)

file(GLOB_RECURSE py_files "${package_root}/*.py")
set(type_py_files "")
foreach(py IN LISTS py_files)
  get_filename_component(name "${py}" NAME)
  if(NOT name STREQUAL "__init__.py" AND
     NOT name STREQUAL "_dsdl_runtime.py" AND
     NOT name STREQUAL "_runtime_loader.py")
    list(APPEND type_py_files "${py}")
  endif()
endforeach()
list(LENGTH type_py_files type_py_count)

if(NOT dsdl_count EQUAL type_py_count)
  message(FATAL_ERROR
    "Python type file count mismatch: dsdl=${dsdl_count}, generated=${type_py_count}")
endif()

set(found_dataclass FALSE)
set(found_composite FALSE)
set(found_runtime_loader_env FALSE)
foreach(py IN LISTS type_py_files)
  file(READ "${py}" text)
  if(text MATCHES "@dataclass\\(slots=True\\)")
    set(found_dataclass TRUE)
  endif()
  if(text MATCHES "class [A-Za-z0-9_]+\\(CompositeObject\\):")
    set(found_composite TRUE)
  endif()
endforeach()

file(READ "${package_root}/_runtime_loader.py" runtime_loader_text)
if(runtime_loader_text MATCHES "LLVMDSDL_PY_RUNTIME_MODE")
  set(found_runtime_loader_env TRUE)
endif()

if(NOT found_dataclass)
  message(FATAL_ERROR "Generated Python files are missing dataclass declarations")
endif()
if(NOT found_composite)
  message(FATAL_ERROR "Generated Python classes do not derive from the runtime's CompositeObject")
endif()
file(READ "${package_root}/_runtime_loader.py" composite_text)
if(NOT composite_text MATCHES "class CompositeObject:"
   OR NOT composite_text MATCHES "def serialize\\(self\\) -> bytes:"
   OR NOT composite_text MATCHES "def deserialize\\(")
  message(FATAL_ERROR "The runtime's CompositeObject does not define serialize() and deserialize()")
endif()
if(NOT found_runtime_loader_env)
  message(FATAL_ERROR "Generated Python runtime loader is missing runtime mode selection")
endif()

message(STATUS
  "fixtures Python generation hardening passed: ${dsdl_count} DSDL -> ${type_py_count} Python files")
