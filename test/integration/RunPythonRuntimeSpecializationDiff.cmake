cmake_minimum_required(VERSION 3.24)

foreach(var DSDLC UAVCAN_ROOT OUT_DIR)
  if(NOT DEFINED ${var} OR "${${var}}" STREQUAL "")
    message(FATAL_ERROR "Missing required variable: ${var}")
  endif()
endforeach()

if(NOT EXISTS "${DSDLC}")
  message(FATAL_ERROR "dsdlc executable not found: ${DSDLC}")
endif()
if(NOT EXISTS "${UAVCAN_ROOT}")
  message(FATAL_ERROR "uavcan root not found: ${UAVCAN_ROOT}")
endif()

set(dsdlc_extra_args "")
if(DEFINED DSDLC_EXTRA_ARGS AND NOT "${DSDLC_EXTRA_ARGS}" STREQUAL "")
  separate_arguments(dsdlc_extra_args NATIVE_COMMAND "${DSDLC_EXTRA_ARGS}")
endif()

file(REMOVE_RECURSE "${OUT_DIR}")
file(MAKE_DIRECTORY "${OUT_DIR}")

set(portable_out "${OUT_DIR}/py-portable")
set(fast_out "${OUT_DIR}/py-fast")
set(py_package "uavcan_dsdl_generated_py")
set(py_package_path "${py_package}")
string(REPLACE "." "/" py_package_path "${py_package_path}")

execute_process(
  COMMAND
    "${DSDLC}" --target-language python
      "${UAVCAN_ROOT}"
      ${dsdlc_extra_args}
      --outdir "${portable_out}"
      --py-package "${py_package}"
      --py-runtime-specialization "portable"
  RESULT_VARIABLE portable_result
  OUTPUT_VARIABLE portable_stdout
  ERROR_VARIABLE portable_stderr
)
if(NOT portable_result EQUAL 0)
  message(STATUS "portable generation stdout:\n${portable_stdout}")
  message(STATUS "portable generation stderr:\n${portable_stderr}")
  message(FATAL_ERROR "failed to generate Python portable runtime specialisation output")
endif()

execute_process(
  COMMAND
    "${DSDLC}" --target-language python
      "${UAVCAN_ROOT}"
      ${dsdlc_extra_args}
      --outdir "${fast_out}"
      --py-package "${py_package}"
      --py-runtime-specialization "fast"
  RESULT_VARIABLE fast_result
  OUTPUT_VARIABLE fast_stdout
  ERROR_VARIABLE fast_stderr
)
if(NOT fast_result EQUAL 0)
  message(STATUS "fast generation stdout:\n${fast_stdout}")
  message(STATUS "fast generation stderr:\n${fast_stderr}")
  message(FATAL_ERROR "failed to generate Python fast runtime specialisation output")
endif()

set(portable_package_root "${portable_out}/${py_package_path}")
set(fast_package_root "${fast_out}/${py_package_path}")

foreach(required
    "${portable_package_root}/llvmdsdl_codegen.json"
    "${portable_package_root}/_dsdl_runtime.py"
    "${portable_package_root}/_runtime_loader.py"
    "${fast_package_root}/llvmdsdl_codegen.json"
    "${fast_package_root}/_dsdl_runtime.py"
    "${fast_package_root}/_runtime_loader.py")
  if(NOT EXISTS "${required}")
    message(FATAL_ERROR "Missing required generated file: ${required}")
  endif()
endforeach()

file(READ "${portable_package_root}/llvmdsdl_codegen.json" portable_metadata)
file(READ "${fast_package_root}/llvmdsdl_codegen.json" fast_metadata)
if(NOT portable_metadata MATCHES
      "\"pythonRuntimeSpecialization\"[ \t\r\n]*:[ \t\r\n]*\"portable\"")
  message(FATAL_ERROR
    "expected portable llvmdsdl_codegen.json metadata to record portable specialization")
endif()
if(NOT fast_metadata MATCHES
      "\"pythonRuntimeSpecialization\"[ \t\r\n]*:[ \t\r\n]*\"fast\"")
  message(FATAL_ERROR
    "expected fast llvmdsdl_codegen.json metadata to record fast specialization")
endif()

set(portable_src "${portable_package_root}")
set(fast_src "${fast_package_root}")

file(GLOB_RECURSE portable_py "${portable_src}/*.py")
file(GLOB_RECURSE fast_py "${fast_src}/*.py")

set(portable_semantic_files "")
foreach(path IN LISTS portable_py)
  file(RELATIVE_PATH rel "${portable_src}" "${path}")
  if(rel STREQUAL "_dsdl_runtime.py")
    continue()
  endif()
  list(APPEND portable_semantic_files "${rel}")
endforeach()

set(fast_semantic_files "")
foreach(path IN LISTS fast_py)
  file(RELATIVE_PATH rel "${fast_src}" "${path}")
  if(rel STREQUAL "_dsdl_runtime.py")
    continue()
  endif()
  list(APPEND fast_semantic_files "${rel}")
endforeach()

list(SORT portable_semantic_files)
list(SORT fast_semantic_files)

if(NOT portable_semantic_files STREQUAL fast_semantic_files)
  message(FATAL_ERROR
    "Python runtime specialization semantic file inventory mismatch between portable and fast outputs")
endif()

foreach(rel IN LISTS portable_semantic_files)
  file(READ "${portable_src}/${rel}" portable_text)
  file(READ "${fast_src}/${rel}" fast_text)
  if(NOT portable_text STREQUAL fast_text)
    message(FATAL_ERROR
      "Python runtime specialization semantic mismatch in generated file: ${rel}")
  endif()
endforeach()

file(READ "${portable_package_root}/_dsdl_runtime.py" portable_runtime_text)
file(READ "${fast_package_root}/_dsdl_runtime.py" fast_runtime_text)
if(portable_runtime_text STREQUAL fast_runtime_text)
  message(FATAL_ERROR
    "Python runtime specialization expected runtime helper differences between portable and fast outputs")
endif()
if(NOT fast_runtime_text MATCHES
      "dst_start = dst_off_bits // 8")
  message(FATAL_ERROR
    "Python fast runtime specialization missing byte-aligned copy fast-path marker")
endif()
if(portable_runtime_text MATCHES
      "dst_start = dst_off_bits // 8")
  message(FATAL_ERROR
    "Python portable runtime specialization unexpectedly contains byte-aligned copy fast-path marker")
endif()

message(STATUS
  "Python runtime specialization semantic diff passed: semantic files are identical while runtime helper implementation differs")
