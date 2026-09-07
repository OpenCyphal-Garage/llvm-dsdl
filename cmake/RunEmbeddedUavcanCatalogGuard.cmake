foreach(var PYTHON_EXECUTABLE REPO_ROOT DSDLC)
  if(NOT DEFINED ${var} OR "${${var}}" STREQUAL "")
    message(FATAL_ERROR "${var} is required for embedded UAVCAN catalogue guard")
  endif()
endforeach()

if(NOT DEFINED UAVCAN_ROOT OR "${UAVCAN_ROOT}" STREQUAL "")
  set(UAVCAN_ROOT "${REPO_ROOT}/submodules/public_regulated_data_types/uavcan")
endif()

set(generator_script "${REPO_ROOT}/tools/dsdlc/generate_embedded_uavcan_mlir.py")
if(NOT EXISTS "${generator_script}")
  message(FATAL_ERROR "embedded UAVCAN generator script not found: ${generator_script}")
endif()

if(NOT EXISTS "${UAVCAN_ROOT}")
  message(FATAL_ERROR "embedded UAVCAN catalogue root not found: ${UAVCAN_ROOT}")
endif()

execute_process(
  COMMAND
    "${PYTHON_EXECUTABLE}" "${generator_script}"
      --repo-root "${REPO_ROOT}"
      --uavcan-root "${UAVCAN_ROOT}"
      --dsdlc "${DSDLC}"
      --check
  RESULT_VARIABLE guard_result
  OUTPUT_VARIABLE guard_stdout
  ERROR_VARIABLE guard_stderr
)

if(guard_stdout)
  message(STATUS "embedded UAVCAN catalogue guard output:\n${guard_stdout}")
endif()

if(guard_result AND NOT guard_result EQUAL 0)
  message(FATAL_ERROR
    "embedded UAVCAN catalogue guard failed with exit code ${guard_result}\n${guard_stderr}")
endif()

if(guard_stderr)
  message(STATUS "embedded UAVCAN catalogue guard diagnostics:\n${guard_stderr}")
endif()

message(STATUS "embedded UAVCAN catalogue guard passed")
