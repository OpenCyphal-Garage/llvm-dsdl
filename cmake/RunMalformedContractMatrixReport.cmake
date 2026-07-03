#===----------------------------------------------------------------------===#
#
# Part of the OpenCyphal project, under the MIT licence
# SPDX-License-Identifier: MIT
#
#===----------------------------------------------------------------------===#

if(NOT DEFINED PYTHON_EXECUTABLE OR PYTHON_EXECUTABLE STREQUAL "")
  message(FATAL_ERROR "PYTHON_EXECUTABLE must be provided")
endif()
if(NOT DEFINED REPO_ROOT OR REPO_ROOT STREQUAL "")
  message(FATAL_ERROR "REPO_ROOT must be provided")
endif()
if(NOT DEFINED CTEST_TEST_DIR OR CTEST_TEST_DIR STREQUAL "")
  message(FATAL_ERROR "CTEST_TEST_DIR must be provided")
endif()
if(NOT DEFINED OUTPUT_JSON OR OUTPUT_JSON STREQUAL "")
  message(FATAL_ERROR "OUTPUT_JSON must be provided")
endif()
if(NOT DEFINED OUTPUT_MD OR OUTPUT_MD STREQUAL "")
  message(FATAL_ERROR "OUTPUT_MD must be provided")
endif()
if(NOT DEFINED MALFORMED_CONTRACT_SCRIPT OR MALFORMED_CONTRACT_SCRIPT STREQUAL "")
  message(FATAL_ERROR "MALFORMED_CONTRACT_SCRIPT must be provided")
endif()
if(NOT DEFINED MALFORMED_CONTRACT_BASELINE_JSON OR MALFORMED_CONTRACT_BASELINE_JSON STREQUAL "")
  message(FATAL_ERROR "MALFORMED_CONTRACT_BASELINE_JSON must be provided")
endif()
if(NOT EXISTS "${MALFORMED_CONTRACT_BASELINE_JSON}")
  message(FATAL_ERROR "malformed contract baseline file does not exist: ${MALFORMED_CONTRACT_BASELINE_JSON}")
endif()

set(MALFORMED_CONTRACT_ARGS
  "${PYTHON_EXECUTABLE}"
  "${MALFORMED_CONTRACT_SCRIPT}"
  --repo-root
  "${REPO_ROOT}"
  --ctest-test-dir
  "${CTEST_TEST_DIR}"
  --output-json
  "${OUTPUT_JSON}"
  --output-md
  "${OUTPUT_MD}"
  --baseline
  "${MALFORMED_CONTRACT_BASELINE_JSON}"
  --check-regressions
)

# Behavioral gating: when a JUnit results file is supplied, a matrix cell is covered
# only if a matching test executed and passed. Fail loudly if it is requested but
# absent (never silently degrade "no results" into "everything passed").
if(DEFINED CTEST_JUNIT AND NOT CTEST_JUNIT STREQUAL "")
  if(NOT EXISTS "${CTEST_JUNIT}")
    message(FATAL_ERROR
      "behavioral malformed-contract gate requested but ctest JUnit results not found: ${CTEST_JUNIT}\n"
      "run the test suite first, e.g. `ctest --preset <preset> --output-junit ${CTEST_JUNIT}`")
  endif()
  list(APPEND MALFORMED_CONTRACT_ARGS --ctest-junit "${CTEST_JUNIT}")
endif()

if(DEFINED CTEST_CONFIG AND NOT CTEST_CONFIG STREQUAL "")
  list(APPEND MALFORMED_CONTRACT_ARGS --ctest-config "${CTEST_CONFIG}")
endif()

execute_process(
  COMMAND ${MALFORMED_CONTRACT_ARGS}
  RESULT_VARIABLE matrix_status
  OUTPUT_VARIABLE matrix_stdout
  ERROR_VARIABLE matrix_stderr
)

if(NOT matrix_status EQUAL 0)
  if(NOT matrix_stdout STREQUAL "")
    message(STATUS "malformed contract matrix stdout:\n${matrix_stdout}")
  endif()
  if(NOT matrix_stderr STREQUAL "")
    message(STATUS "malformed contract matrix stderr:\n${matrix_stderr}")
  endif()
  message(FATAL_ERROR "malformed contract matrix check failed")
endif()

message(STATUS "malformed contract matrix report generated at ${OUTPUT_JSON}")
message(STATUS "malformed contract matrix markdown generated at ${OUTPUT_MD}")
