cmake_minimum_required(VERSION 3.24)

foreach(var DSDLC UAVCAN_ROOT OUT_DIR GO_EXECUTABLE)
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

if(NOT EXISTS "${GO_EXECUTABLE}")
  message(FATAL_ERROR "go executable not found: ${GO_EXECUTABLE}")
endif()

file(REMOVE_RECURSE "${OUT_DIR}")
file(MAKE_DIRECTORY "${OUT_DIR}")

# Runs at the default -- unversioned type names, newest version of each type. That combination used
# to be impossible for Go on this corpus: a DSDL namespace is one Go package, and uavcan carries 20
# types at more than one version, so dsdlc refused. Generating only the newest of each removed the
# collision, and this lane running with no naming flags at all is the most direct evidence of it.

execute_process(
  COMMAND
    "${DSDLC}" --target-language go
      "${UAVCAN_ROOT}"
      --outdir "${OUT_DIR}"
      --go-module "uavcan_dsdl_generated"
  RESULT_VARIABLE gen_result
  OUTPUT_VARIABLE gen_stdout
  ERROR_VARIABLE gen_stderr
)
if(NOT gen_result EQUAL 0)
  message(STATUS "dsdlc stdout:\n${gen_stdout}")
  message(STATUS "dsdlc stderr:\n${gen_stderr}")
  message(FATAL_ERROR "uavcan Go generation failed")
endif()

set(go_cache "${OUT_DIR}/.gocache")
set(go_mod_cache "${OUT_DIR}/.gomodcache")
execute_process(
  COMMAND
    "${CMAKE_COMMAND}" -E env
      "GOCACHE=${go_cache}"
      "GOMODCACHE=${go_mod_cache}"
      "${GO_EXECUTABLE}" test ./...
  WORKING_DIRECTORY "${OUT_DIR}"
  RESULT_VARIABLE go_result
  OUTPUT_VARIABLE go_stdout
  ERROR_VARIABLE go_stderr
)
if(NOT go_result EQUAL 0)
  message(STATUS "go test stdout:\n${go_stdout}")
  message(STATUS "go test stderr:\n${go_stderr}")
  message(FATAL_ERROR "generated uavcan go module failed go test")
endif()

message(STATUS "uavcan go build check passed")
