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
if(NOT DEFINED OUTPUT_JSON OR OUTPUT_JSON STREQUAL "")
  message(FATAL_ERROR "OUTPUT_JSON must be provided")
endif()
if(NOT DEFINED OUTPUT_MD OR OUTPUT_MD STREQUAL "")
  message(FATAL_ERROR "OUTPUT_MD must be provided")
endif()
if(NOT DEFINED PARITY_MATRIX_SCRIPT OR PARITY_MATRIX_SCRIPT STREQUAL "")
  message(FATAL_ERROR "PARITY_MATRIX_SCRIPT must be provided")
endif()
if(NOT DEFINED PARITY_BASELINE_JSON OR PARITY_BASELINE_JSON STREQUAL "")
  message(FATAL_ERROR "PARITY_BASELINE_JSON must be provided")
endif()
if(NOT EXISTS "${PARITY_BASELINE_JSON}")
  message(FATAL_ERROR "parity baseline file does not exist: ${PARITY_BASELINE_JSON}")
endif()

set(PARITY_MATRIX_ARGS
  "${PYTHON_EXECUTABLE}"
  "${PARITY_MATRIX_SCRIPT}"
  --repo-root
  "${REPO_ROOT}"
  --output-json
  "${OUTPUT_JSON}"
  --output-md
  "${OUTPUT_MD}"
  --baseline
  "${PARITY_BASELINE_JSON}"
  --check-regressions
)

if(DEFINED CTEST_TEST_DIR AND NOT CTEST_TEST_DIR STREQUAL "")
  list(APPEND PARITY_MATRIX_ARGS --ctest-test-dir "${CTEST_TEST_DIR}")
endif()

# Behavioral gating: when a JUnit results file is supplied, a matrix cell is covered
# only if a matching test executed and passed. Fail loudly if it is requested but
# absent (never silently degrade "no results" into "everything passed").
if(DEFINED CTEST_JUNIT AND NOT CTEST_JUNIT STREQUAL "")
  if(NOT EXISTS "${CTEST_JUNIT}")
    message(FATAL_ERROR
      "behavioral parity gate requested but ctest JUnit results not found: ${CTEST_JUNIT}\n"
      "run the test suite first, e.g. `ctest --preset <preset> --output-junit ${CTEST_JUNIT}`")
  endif()
  list(APPEND PARITY_MATRIX_ARGS --ctest-junit "${CTEST_JUNIT}")
endif()

if(DEFINED CTEST_CONFIG AND NOT CTEST_CONFIG STREQUAL "")
  list(APPEND PARITY_MATRIX_ARGS --ctest-config "${CTEST_CONFIG}")
endif()

execute_process(
  COMMAND ${PARITY_MATRIX_ARGS}
  RESULT_VARIABLE parity_status
  OUTPUT_VARIABLE parity_stdout
  ERROR_VARIABLE parity_stderr
)

if(NOT parity_status EQUAL 0)
  if(NOT parity_stdout STREQUAL "")
    message(STATUS "parity matrix stdout:\n${parity_stdout}")
  endif()
  if(NOT parity_stderr STREQUAL "")
    message(STATUS "parity matrix stderr:\n${parity_stderr}")
  endif()
  message(FATAL_ERROR "parity matrix coverage check failed")
endif()

message(STATUS "parity matrix report generated at ${OUTPUT_JSON}")
message(STATUS "parity matrix markdown generated at ${OUTPUT_MD}")
