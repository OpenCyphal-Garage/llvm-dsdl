foreach(var PYTHON_EXECUTABLE REPO_ROOT)
  if(NOT DEFINED ${var} OR "${${var}}" STREQUAL "")
    message(FATAL_ERROR "${var} is required for embedded UAVCAN catalogue guard self-test")
  endif()
endforeach()

set(selftest_script "${REPO_ROOT}/tools/dsdlc/test_generate_embedded_uavcan_mlir.py")
if(NOT EXISTS "${selftest_script}")
  message(FATAL_ERROR "embedded UAVCAN generator self-test script not found: ${selftest_script}")
endif()

execute_process(
  COMMAND
    "${PYTHON_EXECUTABLE}" "${selftest_script}"
      --repo-root "${REPO_ROOT}"
  RESULT_VARIABLE selftest_result
  OUTPUT_VARIABLE selftest_stdout
  ERROR_VARIABLE selftest_stderr
)

if(selftest_stdout)
  message(STATUS "embedded UAVCAN generator self-test output:\n${selftest_stdout}")
endif()

if(selftest_result AND NOT selftest_result EQUAL 0)
  message(FATAL_ERROR
    "embedded UAVCAN generator self-test failed with exit code ${selftest_result}\n${selftest_stderr}")
endif()

if(selftest_stderr)
  message(STATUS "embedded UAVCAN generator self-test diagnostics:\n${selftest_stderr}")
endif()

message(STATUS "embedded UAVCAN catalogue guard self-test passed")
