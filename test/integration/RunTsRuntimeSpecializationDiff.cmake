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

set(portable_out "${OUT_DIR}/ts-portable")
set(fast_out "${OUT_DIR}/ts-fast")

execute_process(
  COMMAND
    "${DSDLC}" --target-language ts
      "${UAVCAN_ROOT}"
      ${dsdlc_extra_args}
      --outdir "${portable_out}"
      --ts-module "uavcan_dsdl_generated_ts"
      --ts-runtime-specialization "portable"
  RESULT_VARIABLE portable_result
  OUTPUT_VARIABLE portable_stdout
  ERROR_VARIABLE portable_stderr
)
if(NOT portable_result EQUAL 0)
  message(STATUS "portable generation stdout:\n${portable_stdout}")
  message(STATUS "portable generation stderr:\n${portable_stderr}")
  message(FATAL_ERROR "failed to generate TypeScript portable runtime specialisation output")
endif()

execute_process(
  COMMAND
    "${DSDLC}" --target-language ts
      "${UAVCAN_ROOT}"
      ${dsdlc_extra_args}
      --outdir "${fast_out}"
      --ts-module "uavcan_dsdl_generated_ts"
      --ts-runtime-specialization "fast"
  RESULT_VARIABLE fast_result
  OUTPUT_VARIABLE fast_stdout
  ERROR_VARIABLE fast_stderr
)
if(NOT fast_result EQUAL 0)
  message(STATUS "fast generation stdout:\n${fast_stdout}")
  message(STATUS "fast generation stderr:\n${fast_stderr}")
  message(FATAL_ERROR "failed to generate TypeScript fast runtime specialisation output")
endif()

foreach(required
    "${portable_out}/package.json"
    "${portable_out}/index.ts"
    "${portable_out}/dsdl_runtime.ts"
    "${fast_out}/package.json"
    "${fast_out}/index.ts"
    "${fast_out}/dsdl_runtime.ts")
  if(NOT EXISTS "${required}")
    message(FATAL_ERROR "Missing required generated file: ${required}")
  endif()
endforeach()

file(READ "${portable_out}/package.json" portable_package_json)
file(READ "${fast_out}/package.json" fast_package_json)
if(NOT portable_package_json MATCHES
      "\"tsRuntimeSpecialization\"[ \t\r\n]*:[ \t\r\n]*\"portable\"")
  message(FATAL_ERROR "expected portable package.json metadata to record portable specialisation")
endif()
if(NOT fast_package_json MATCHES
      "\"tsRuntimeSpecialization\"[ \t\r\n]*:[ \t\r\n]*\"fast\"")
  message(FATAL_ERROR "expected fast package.json metadata to record fast specialisation")
endif()

set(portable_src "${portable_out}")
set(fast_src "${fast_out}")

file(GLOB_RECURSE portable_ts "${portable_src}/*.ts")
file(GLOB_RECURSE fast_ts "${fast_src}/*.ts")

set(portable_semantic_files "")
foreach(path IN LISTS portable_ts)
  file(RELATIVE_PATH rel "${portable_src}" "${path}")
  if(rel STREQUAL "dsdl_runtime.ts")
    continue()
  endif()
  list(APPEND portable_semantic_files "${rel}")
endforeach()

set(fast_semantic_files "")
foreach(path IN LISTS fast_ts)
  file(RELATIVE_PATH rel "${fast_src}" "${path}")
  if(rel STREQUAL "dsdl_runtime.ts")
    continue()
  endif()
  list(APPEND fast_semantic_files "${rel}")
endforeach()

list(SORT portable_semantic_files)
list(SORT fast_semantic_files)

if(NOT portable_semantic_files STREQUAL fast_semantic_files)
  message(FATAL_ERROR
    "TypeScript runtime specialization semantic file inventory mismatch between portable and fast outputs")
endif()

foreach(rel IN LISTS portable_semantic_files)
  file(READ "${portable_src}/${rel}" portable_text)
  file(READ "${fast_src}/${rel}" fast_text)
  if(NOT portable_text STREQUAL fast_text)
    message(FATAL_ERROR
      "TypeScript runtime specialization semantic mismatch in generated file: ${rel}")
  endif()
endforeach()

file(READ "${portable_out}/dsdl_runtime.ts" portable_runtime_text)
file(READ "${fast_out}/dsdl_runtime.ts" fast_runtime_text)
if(portable_runtime_text STREQUAL fast_runtime_text)
  message(FATAL_ERROR
    "TypeScript runtime specialization expected runtime helper differences between portable and fast outputs")
endif()
if(NOT fast_runtime_text MATCHES "dst\\.set\\(")
  message(FATAL_ERROR
    "TypeScript fast runtime specialization missing byte-aligned copy fast-path marker")
endif()
if(NOT fast_runtime_text MATCHES "src\\.slice\\(")
  message(FATAL_ERROR
    "TypeScript fast runtime specialization missing byte-aligned extract fast-path marker")
endif()

message(STATUS
  "TypeScript runtime specialization semantic diff passed: semantic files are identical while runtime helper implementation differs")
