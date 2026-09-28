#===----------------------------------------------------------------------===#
#
# Part of the OpenCyphal project, under the MIT licence
# SPDX-License-Identifier: MIT
#
#===----------------------------------------------------------------------===#
#
# The object target's entry points, called through the header it wrote, answer their error codes
# as the C ABI extends an `int8_t`. The probe is compiled at -O2, where the caller relies on the
# callee's extension rather than repeating it.
#
cmake_minimum_required(VERSION 3.24)

foreach(var DSDLC SOURCE_ROOT OUT_DIR C_COMPILER)
  if(NOT DEFINED ${var} OR "${${var}}" STREQUAL "")
    message(FATAL_ERROR "Missing required variable: ${var}")
  endif()
endforeach()

file(REMOVE_RECURSE "${OUT_DIR}")
file(MAKE_DIRECTORY "${OUT_DIR}")
set(obj_out "${OUT_DIR}/obj")

execute_process(
  COMMAND "${DSDLC}" --target-language obj "${SOURCE_ROOT}/test/lit/fixtures_aliasable" --outdir "${obj_out}"
  RESULT_VARIABLE gen_result
  OUTPUT_VARIABLE gen_stdout
  ERROR_VARIABLE gen_stderr
)
if(NOT gen_result EQUAL 0)
  message(STATUS "dsdlc stdout:\n${gen_stdout}")
  message(STATUS "dsdlc stderr:\n${gen_stderr}")
  message(FATAL_ERROR "object generation failed")
endif()
file(GLOB_RECURSE objects "${obj_out}/*.o")
if(objects STREQUAL "")
  message(FATAL_ERROR "the object target wrote no object")
endif()

execute_process(
  COMMAND "${C_COMPILER}" -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror -I "${obj_out}"
          "${SOURCE_ROOT}/test/integration/ObjectCallingConventionProbe.c" ${objects} -o "${OUT_DIR}/probe"
  RESULT_VARIABLE build_result
  OUTPUT_VARIABLE build_stdout
  ERROR_VARIABLE build_stderr
)
if(NOT build_result EQUAL 0)
  message(STATUS "${build_stdout}${build_stderr}")
  message(FATAL_ERROR "the probe did not build")
endif()

execute_process(
  COMMAND "${OUT_DIR}/probe"
  RESULT_VARIABLE run_result
  OUTPUT_VARIABLE run_out
  ERROR_VARIABLE run_err
)
message(STATUS "${run_out}${run_err}")
if(NOT run_result EQUAL 0)
  message(FATAL_ERROR "an entry point's answer did not arrive as the C ABI extends it")
endif()
