# Coverage-guided fuzz lane over the generated Go deserializers.
#
# Generates Go for the UAVCAN corpus, drops the FuzzDecoders harness
# (test/integration/GoDecoderFuzzTest.go) into the module, and runs Go's native
# fuzzer over the adversarial type set. Go is memory-safe, so the caught class is
# a PANIC on attacker-controlled wire bytes (out-of-range slice, nil deref) — a
# real robustness / DoS defect in a generated decoder.
#
# Default: `go test` replays the committed seed corpus only (deterministic, fast).
# Pass -DFUZZTIME=<dur> (e.g. 30s) to run the coverage-guided loop for the deep /
# cron lane.
cmake_minimum_required(VERSION 3.24)

foreach(var DSDLC UAVCAN_ROOT OUT_DIR GO_EXECUTABLE SOURCE_ROOT)
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

set(fuzz_harness "${SOURCE_ROOT}/test/integration/GoDecoderFuzzTest.go")
if(NOT EXISTS "${fuzz_harness}")
  message(FATAL_ERROR "Go decoder fuzz harness missing: ${fuzz_harness}")
endif()

file(REMOVE_RECURSE "${OUT_DIR}")
file(MAKE_DIRECTORY "${OUT_DIR}")

# As with the C/Go parity lane: the newest-version default removes the collision that forced
# versioned names on this corpus, so the fuzz harness runs at the default.
include("${SOURCE_ROOT}/cmake/HarnessTypeNameTokens.cmake")
llvmdsdl_harness_naming_scheme(C_DEFAULT "unversioned" OTHER_DEFAULT "unversioned")

execute_process(
  COMMAND
    "${DSDLC}" --target-language go ${other_scheme_args}
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

# Drop the fuzz harness into its own package within the generated module.
file(MAKE_DIRECTORY "${OUT_DIR}/decoderfuzz")

configure_file("${fuzz_harness}" "${OUT_DIR}/decoderfuzz/fuzz_test.go" @ONLY)

set(go_cache "${OUT_DIR}/.gocache")
set(go_mod_cache "${OUT_DIR}/.gomodcache")

if(DEFINED FUZZTIME AND NOT "${FUZZTIME}" STREQUAL "")
  message(STATUS "Go decoder fuzz: coverage-guided run, fuzztime=${FUZZTIME}")
  set(go_test_args test ./decoderfuzz/ "-run=^$" -fuzz=FuzzDecoders "-fuzztime=${FUZZTIME}")
else()
  message(STATUS "Go decoder fuzz: seed-corpus replay (pass -DFUZZTIME=30s for the deep run)")
  set(go_test_args test ./decoderfuzz/)
endif()

execute_process(
  COMMAND
    "${CMAKE_COMMAND}" -E env
      "GOCACHE=${go_cache}"
      "GOMODCACHE=${go_mod_cache}"
      "GOFLAGS=-mod=mod"
      "${GO_EXECUTABLE}" ${go_test_args}
  WORKING_DIRECTORY "${OUT_DIR}"
  RESULT_VARIABLE go_result
  OUTPUT_VARIABLE go_stdout
  ERROR_VARIABLE go_stderr
)
message(STATUS "go test stdout:\n${go_stdout}")
if(NOT go_stderr STREQUAL "")
  message(STATUS "go test stderr:\n${go_stderr}")
endif()
if(NOT go_result EQUAL 0)
  message(FATAL_ERROR "generated Go decoder fuzz harness reported a failure (a decoder panicked on fuzzed input)")
endif()

message(STATUS "Go decoder fuzz lane passed")
