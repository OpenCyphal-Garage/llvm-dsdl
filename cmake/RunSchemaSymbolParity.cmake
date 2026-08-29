# Generates the C and the schema for the same corpus under one naming mode and holds the symbols
# the schema names against the ones the headers declare.

if(NOT DEFINED DSDLC OR NOT DEFINED OUT_DIR OR NOT DEFINED PYTHON_EXECUTABLE)
  message(FATAL_ERROR "DSDLC, OUT_DIR and PYTHON_EXECUTABLE are required")
endif()

set(naming_args)
if(VERSIONED_TYPE_NAMES)
  set(naming_args --versioned-type-names)
endif()

file(REMOVE_RECURSE "${OUT_DIR}")
file(MAKE_DIRECTORY "${OUT_DIR}")

execute_process(
  COMMAND "${DSDLC}" --target-language c ${naming_args} --outdir "${OUT_DIR}/c" +uavcan
  RESULT_VARIABLE c_result OUTPUT_VARIABLE c_output ERROR_VARIABLE c_output)
if(NOT c_result EQUAL 0)
  message(FATAL_ERROR "C generation failed:\n${c_output}")
endif()

execute_process(
  COMMAND "${DSDLC}" --target-language mlir ${naming_args} +uavcan
  RESULT_VARIABLE m_result OUTPUT_FILE "${OUT_DIR}/schema.mlir" ERROR_VARIABLE m_output)
if(NOT m_result EQUAL 0)
  message(FATAL_ERROR "schema generation failed:\n${m_output}")
endif()

execute_process(
  COMMAND "${PYTHON_EXECUTABLE}" "${PARITY_SCRIPT}"
          --c-root "${OUT_DIR}/c"
          --mlir "${OUT_DIR}/schema.mlir"
  RESULT_VARIABLE check_result OUTPUT_VARIABLE check_output ERROR_VARIABLE check_output)
message(STATUS "${check_output}")
if(NOT check_result EQUAL 0)
  message(FATAL_ERROR "schema symbol parity failed")
endif()
