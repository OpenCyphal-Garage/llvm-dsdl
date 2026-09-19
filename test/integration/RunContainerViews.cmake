#===----------------------------------------------------------------------===#
#
# Part of the OpenCyphal project, under the MIT licence
# SPDX-License-Identifier: MIT
#
#===----------------------------------------------------------------------===#
#
# A container's view of an @aliasable record, on C and on the object target.
#
# `--aliasable-views` holds a composite field of an @aliasable type as a view of the buffer the
# holder was deserialised from. This lane generates the view fixture in that mode for both C
# targets, builds a probe against the output and nothing else under warnings-as-errors, and holds
# the view to its contract: it points into the buffer, the record's accessors read it, serialising
# reproduces the wire, a short buffer leaves a short view read as zeros and serialised zero-filled,
# and an initialised object serialises an empty view as zeros.
#
cmake_minimum_required(VERSION 3.24)

foreach(var DSDLC SOURCE_ROOT OUT_DIR C_COMPILER)
  if(NOT DEFINED ${var} OR "${${var}}" STREQUAL "")
    message(FATAL_ERROR "Missing required variable: ${var}")
  endif()
endforeach()
foreach(tool "${DSDLC}" "${C_COMPILER}")
  if(NOT EXISTS "${tool}")
    message(FATAL_ERROR "tool not found: ${tool}")
  endif()
endforeach()

set(fixture_root "${SOURCE_ROOT}/test/lit/fixtures_views")
set(fixture_include "${SOURCE_ROOT}/test/lit/fixtures_aliasable")
set(probe "${SOURCE_ROOT}/test/integration/ContainerViewsProbe.c")
file(REMOVE_RECURSE "${OUT_DIR}")
file(MAKE_DIRECTORY "${OUT_DIR}")

# Generates the fixture for one target under the mode, then compiles and runs the probe against
# the output. ARGN holds the generated inputs the probe links with.
function(_views_leg language label)
  set(out "${OUT_DIR}/${language}")
  execute_process(
    COMMAND "${DSDLC}" --target-language ${language} --aliasable-views "${fixture_root}" -I "${fixture_include}"
      --outdir "${out}"
    RESULT_VARIABLE gen_result
    OUTPUT_VARIABLE gen_stdout
    ERROR_VARIABLE gen_stderr
  )
  if(NOT gen_result EQUAL 0)
    message(STATUS "dsdlc stdout:\n${gen_stdout}")
    message(STATUS "dsdlc stderr:\n${gen_stderr}")
    message(FATAL_ERROR "${label} generation under --aliasable-views failed")
  endif()
  file(GLOB_RECURSE inputs ${ARGN})
  if("${inputs}" STREQUAL "")
    message(FATAL_ERROR "${label} generation wrote nothing to link the probe with")
  endif()
  execute_process(
    COMMAND "${C_COMPILER}" -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror "-DTARGET_NAME=\"${label}\""
      -I "${out}" "${probe}" ${inputs} -o "${OUT_DIR}/probe_${language}"
    RESULT_VARIABLE build_result
    OUTPUT_VARIABLE build_stdout
    ERROR_VARIABLE build_stderr
  )
  if(NOT build_result EQUAL 0)
    message(FATAL_ERROR "${label} probe build failed:\n${build_stdout}\n${build_stderr}")
  endif()
  execute_process(
    COMMAND "${OUT_DIR}/probe_${language}"
    RESULT_VARIABLE run_result
    OUTPUT_VARIABLE run_out
    ERROR_VARIABLE run_err
  )
  message(STATUS "${label}:\n${run_out}")
  if(NOT run_err STREQUAL "")
    message(STATUS "${label} stderr:\n${run_err}")
  endif()
  if(NOT run_result EQUAL 0 OR NOT run_out MATCHES "container-views ${label}: ok")
    message(FATAL_ERROR "${label} probe failed")
  endif()
endfunction()

_views_leg(c "C" "${OUT_DIR}/c/*.c")
_views_leg(obj "object" "${OUT_DIR}/obj/*.o")

message(STATUS "container views passed on C and the object target")
