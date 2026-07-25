foreach(var PYTHON_EXECUTABLE REPO_ROOT)
  if(NOT DEFINED ${var} OR "${${var}}" STREQUAL "")
    message(FATAL_ERROR "${var} is required for the Debian packaging checker self-test")
  endif()
endforeach()

set(selftest_script "${REPO_ROOT}/packaging/verify/test_check_deb_config.py")
if(NOT EXISTS "${selftest_script}")
  message(FATAL_ERROR "packaging checker self-test script not found: ${selftest_script}")
endif()

execute_process(
  COMMAND "${PYTHON_EXECUTABLE}" "${selftest_script}"
  RESULT_VARIABLE selftest_result
  OUTPUT_VARIABLE selftest_stdout
  ERROR_VARIABLE selftest_stderr
)

if(selftest_stdout)
  message(STATUS "packaging checker self-test output:\n${selftest_stdout}")
endif()

if(selftest_result AND NOT selftest_result EQUAL 0)
  message(FATAL_ERROR
    "packaging checker self-test failed with exit code ${selftest_result}\n${selftest_stderr}")
endif()

if(selftest_stderr)
  message(STATUS "packaging checker self-test diagnostics:\n${selftest_stderr}")
endif()

message(STATUS "packaging checker self-test passed")
