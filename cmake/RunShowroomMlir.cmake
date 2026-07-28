#===----------------------------------------------------------------------===#
#
# Part of the OpenCyphal project, under the MIT licence
# SPDX-License-Identifier: MIT
#
#===----------------------------------------------------------------------===#
#
# The mlir backend writes the module to stdout rather than into --outdir, so the showroom captures it
# through this shim. add_custom_command cannot portably redirect, and execute_process can.

if(NOT DEFINED DSDLC OR DSDLC STREQUAL "")
  message(FATAL_ERROR "DSDLC must be provided")
endif()
if(NOT DEFINED DSDL_ROOT OR DSDL_ROOT STREQUAL "")
  message(FATAL_ERROR "DSDL_ROOT must be provided")
endif()
if(NOT DEFINED OUTPUT_FILE OR OUTPUT_FILE STREQUAL "")
  message(FATAL_ERROR "OUTPUT_FILE must be provided")
endif()

get_filename_component(_showroom_mlir_dir "${OUTPUT_FILE}" DIRECTORY)
file(MAKE_DIRECTORY "${_showroom_mlir_dir}")

execute_process(
  COMMAND "${DSDLC}" --target-language mlir "${DSDL_ROOT}"
  OUTPUT_FILE "${OUTPUT_FILE}"
  ERROR_VARIABLE _showroom_mlir_stderr
  RESULT_VARIABLE _showroom_mlir_result)

# @print directives and other notes land on stderr; surface them rather than swallowing them, since
# making compile-time diagnostics visible is part of what the showroom is demonstrating.
if(NOT _showroom_mlir_stderr STREQUAL "")
  message(STATUS "${_showroom_mlir_stderr}")
endif()

if(NOT _showroom_mlir_result EQUAL 0)
  message(FATAL_ERROR "dsdlc mlir backend failed with status ${_showroom_mlir_result}")
endif()
