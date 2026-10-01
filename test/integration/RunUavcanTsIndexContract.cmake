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

file(REMOVE_RECURSE "${OUT_DIR}")
file(MAKE_DIRECTORY "${OUT_DIR}")

execute_process(
  COMMAND
    "${DSDLC}" --target-language ts
      "${UAVCAN_ROOT}"
      --outdir "${OUT_DIR}"
      --ts-module "uavcan_dsdl_generated_ts"
  RESULT_VARIABLE gen_result
  OUTPUT_VARIABLE gen_stdout
  ERROR_VARIABLE gen_stderr
)
if(NOT gen_result EQUAL 0)
  message(STATUS "dsdlc stdout:\n${gen_stdout}")
  message(STATUS "dsdlc stderr:\n${gen_stderr}")
  message(FATAL_ERROR "uavcan TypeScript generation failed")
endif()

# Each directory's index re-exports its namespaces' indexes and its modules, once each and under its
# own name, so a module's public path is its directory path. Walked from the root, the indexes reach
# every module the run wrote.
file(GLOB_RECURSE generated_ts RELATIVE "${OUT_DIR}" "${OUT_DIR}/*.ts")
set(expected_modules "")
foreach(ts_rel IN LISTS generated_ts)
  get_filename_component(name "${ts_rel}" NAME)
  if(NOT name STREQUAL "index.ts" AND NOT ts_rel STREQUAL "dsdl_runtime.ts")
    string(REGEX REPLACE "\\.ts$" "" module_rel "${ts_rel}")
    list(APPEND expected_modules "${module_rel}")
  endif()
endforeach()

set(pending "")
list(APPEND pending ".")
set(reached_modules "")
set(index_count 0)
while(pending)
  list(POP_FRONT pending directory)
  if(directory STREQUAL ".")
    set(index_file "${OUT_DIR}/index.ts")
    set(prefix "")
  else()
    set(index_file "${OUT_DIR}/${directory}/index.ts")
    set(prefix "${directory}/")
  endif()
  if(NOT EXISTS "${index_file}")
    message(FATAL_ERROR "Missing generated index file: ${index_file}")
  endif()
  math(EXPR index_count "${index_count} + 1")

  file(READ "${index_file}" index_text)
  if(index_text MATCHES "export \\* from ")
    message(FATAL_ERROR "Unexpected wildcard re-export in ${index_file}")
  endif()
  file(STRINGS "${index_file}" export_lines REGEX "^export ")
  if(export_lines STREQUAL "")
    message(FATAL_ERROR "Expected re-exports in ${index_file}")
  endif()

  set(names "")
  foreach(line IN LISTS export_lines)
    if(NOT line MATCHES "^export \\* as ([A-Za-z_][A-Za-z0-9_]*) from \"\\./([^\"]+)\";$")
      message(FATAL_ERROR "Failed to parse ${index_file} export line: ${line}")
    endif()
    set(name "${CMAKE_MATCH_1}")
    set(target "${CMAKE_MATCH_2}")
    list(APPEND names "${name}")
    if(target STREQUAL "${name}/index")
      list(APPEND pending "${prefix}${name}")
    elseif(target STREQUAL name)
      if(NOT EXISTS "${OUT_DIR}/${prefix}${name}.ts")
        message(FATAL_ERROR "${index_file} exports missing module ${prefix}${name}")
      endif()
      list(APPEND reached_modules "${prefix}${name}")
    else()
      message(FATAL_ERROR "${index_file} exports ${target} under another name, ${name}")
    endif()
  endforeach()

  set(unique_names ${names})
  list(REMOVE_DUPLICATES unique_names)
  list(LENGTH names name_count)
  list(LENGTH unique_names unique_count)
  if(NOT name_count EQUAL unique_count)
    message(FATAL_ERROR "Duplicate names exported by ${index_file}")
  endif()
endwhile()

list(SORT expected_modules)
list(SORT reached_modules)
string(JOIN "\n" expected_manifest ${expected_modules})
string(JOIN "\n" reached_manifest ${reached_modules})
if(NOT expected_manifest STREQUAL reached_manifest)
  file(WRITE "${OUT_DIR}/expected-index-modules.txt" "${expected_manifest}\n")
  file(WRITE "${OUT_DIR}/actual-index-modules.txt" "${reached_manifest}\n")
  message(FATAL_ERROR
    "The indexes do not reach every generated module. "
    "See ${OUT_DIR}/expected-index-modules.txt and ${OUT_DIR}/actual-index-modules.txt.")
endif()

list(LENGTH reached_modules module_count)
message(STATUS
  "uavcan TypeScript index contract check passed: ${index_count} indexes reach all ${module_count} type modules")
