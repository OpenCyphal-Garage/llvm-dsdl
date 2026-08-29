# Holds the struct an object addresses members within against the one a C compiler lays out, for a
# named target rather than only for this host.
#
# The width the lane resolved is read back from its own report, so what is checked is what it
# actually derived and not a width supplied here.

if(NOT DEFINED DSDLC OR NOT DEFINED DSDL_OPT OR NOT DEFINED OUT_DIR OR NOT DEFINED PYTHON_EXECUTABLE)
  message(FATAL_ERROR "DSDLC, DSDL_OPT, OUT_DIR and PYTHON_EXECUTABLE are required")
endif()

set(triple_args)
if(TARGET_TRIPLE)
  set(triple_args --target-triple ${TARGET_TRIPLE})
endif()

file(REMOVE_RECURSE "${OUT_DIR}")
file(MAKE_DIRECTORY "${OUT_DIR}")

execute_process(
  COMMAND "${DSDLC}" --target-language c --outdir "${OUT_DIR}/c" "${FIXTURES}"
  RESULT_VARIABLE r ERROR_VARIABLE e)
if(NOT r EQUAL 0)
  message(FATAL_ERROR "C generation failed:\n${e}")
endif()

execute_process(
  COMMAND "${DSDLC}" --target-language mlir "${FIXTURES}"
  OUTPUT_FILE "${OUT_DIR}/schema.mlir" RESULT_VARIABLE r ERROR_VARIABLE e)
if(NOT r EQUAL 0)
  message(FATAL_ERROR "schema generation failed:\n${e}")
endif()

# The lane says what the target spells size_t at; that is the width whose consequences are checked.
execute_process(
  COMMAND "${DSDLC}" --target-language obj -v ${triple_args} --outdir "${OUT_DIR}/obj" "${FIXTURES}"
  RESULT_VARIABLE r OUTPUT_VARIABLE lane_output ERROR_VARIABLE lane_output)
if(NOT r EQUAL 0)
  message(FATAL_ERROR "object emission failed:\n${lane_output}")
endif()
string(REGEX MATCH "target size_t: ([0-9]+) bits" _matched "${lane_output}")
if(NOT _matched)
  message(FATAL_ERROR "the obj lane did not report the target's size_t width:\n${lane_output}")
endif()
set(size_bits "${CMAKE_MATCH_1}")
message(STATUS "obj lane resolved size_t to ${size_bits} bits for ${TARGET_TRIPLE}")

execute_process(
  COMMAND "${DSDL_OPT}" --lower-dsdl-exec --build-dsdl-plan-bodies
          "--convert-dsdl-to-llvm=size-bits=${size_bits}" "${OUT_DIR}/schema.mlir"
  OUTPUT_FILE "${OUT_DIR}/converted.mlir" RESULT_VARIABLE r ERROR_VARIABLE e)
if(NOT r EQUAL 0)
  message(FATAL_ERROR "conversion failed:\n${e}")
endif()

execute_process(
  COMMAND "${PYTHON_EXECUTABLE}" "${CROSSCHECK_SCRIPT}"
          --c-root "${OUT_DIR}/c"
          --schema "${OUT_DIR}/schema.mlir"
          --converted "${OUT_DIR}/converted.mlir"
          --target "${PROBE_TRIPLE}"
          --clang "${CLANG}"
          --workdir "${OUT_DIR}/work"
  RESULT_VARIABLE r OUTPUT_VARIABLE out ERROR_VARIABLE out)
message(STATUS "${out}")
if(NOT r EQUAL 0)
  message(FATAL_ERROR "target layout cross-check failed")
endif()
