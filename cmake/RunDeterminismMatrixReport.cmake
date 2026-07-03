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
if(NOT DEFINED DETERMINISM_MATRIX_SCRIPT OR DETERMINISM_MATRIX_SCRIPT STREQUAL "")
  message(FATAL_ERROR "DETERMINISM_MATRIX_SCRIPT must be provided")
endif()
if(NOT DEFINED DETERMINISM_BASELINE_JSON OR DETERMINISM_BASELINE_JSON STREQUAL "")
  message(FATAL_ERROR "DETERMINISM_BASELINE_JSON must be provided")
endif()
if(NOT EXISTS "${DETERMINISM_BASELINE_JSON}")
  message(FATAL_ERROR "determinism baseline file does not exist: ${DETERMINISM_BASELINE_JSON}")
endif()

set(DETERMINISM_MATRIX_ARGS
  "${PYTHON_EXECUTABLE}"
  "${DETERMINISM_MATRIX_SCRIPT}"
  --repo-root
  "${REPO_ROOT}"
  --output-json
  "${OUTPUT_JSON}"
  --output-md
  "${OUTPUT_MD}"
  --baseline
  "${DETERMINISM_BASELINE_JSON}"
  --check-regressions
)

if(DEFINED CTEST_TEST_DIR AND NOT CTEST_TEST_DIR STREQUAL "")
  list(APPEND DETERMINISM_MATRIX_ARGS --ctest-test-dir "${CTEST_TEST_DIR}")
endif()

# Behavioral gating: when a JUnit results file is supplied, a backend is covered only
# if its determinism test executed and passed. Fail loudly if requested but absent.
if(DEFINED CTEST_JUNIT AND NOT CTEST_JUNIT STREQUAL "")
  if(NOT EXISTS "${CTEST_JUNIT}")
    message(FATAL_ERROR
      "behavioral determinism gate requested but ctest JUnit results not found: ${CTEST_JUNIT}\n"
      "run the test suite first, e.g. `ctest --preset <preset> --output-junit ${CTEST_JUNIT}`")
  endif()
  list(APPEND DETERMINISM_MATRIX_ARGS --ctest-junit "${CTEST_JUNIT}")
endif()

if(DEFINED CTEST_CONFIG AND NOT CTEST_CONFIG STREQUAL "")
  list(APPEND DETERMINISM_MATRIX_ARGS --ctest-config "${CTEST_CONFIG}")
endif()

execute_process(
  COMMAND ${DETERMINISM_MATRIX_ARGS}
  RESULT_VARIABLE determinism_status
  OUTPUT_VARIABLE determinism_stdout
  ERROR_VARIABLE determinism_stderr
)

if(NOT determinism_status EQUAL 0)
  if(NOT determinism_stdout STREQUAL "")
    message(STATUS "determinism matrix stdout:\n${determinism_stdout}")
  endif()
  if(NOT determinism_stderr STREQUAL "")
    message(STATUS "determinism matrix stderr:\n${determinism_stderr}")
  endif()
  message(FATAL_ERROR "determinism matrix coverage check failed")
endif()

message(STATUS "determinism matrix report generated at ${OUTPUT_JSON}")
message(STATUS "determinism matrix markdown generated at ${OUTPUT_MD}")
