# Generates the C and the schema for the same corpus and holds their member orders against each
# other, then asks the compiler what the layout actually is.

if(NOT DEFINED DSDLC OR NOT DEFINED OUT_DIR OR NOT DEFINED PYTHON_EXECUTABLE)
  message(FATAL_ERROR "DSDLC, OUT_DIR and PYTHON_EXECUTABLE are required")
endif()

file(REMOVE_RECURSE "${OUT_DIR}")
file(MAKE_DIRECTORY "${OUT_DIR}")

execute_process(
  COMMAND "${DSDLC}" --target-language c --outdir "${OUT_DIR}/c" +uavcan
  RESULT_VARIABLE c_result OUTPUT_VARIABLE c_output ERROR_VARIABLE c_output)
if(NOT c_result EQUAL 0)
  message(FATAL_ERROR "C generation failed:\n${c_output}")
endif()

execute_process(
  COMMAND "${DSDLC}" --target-language mlir +uavcan
  RESULT_VARIABLE m_result OUTPUT_FILE "${OUT_DIR}/schema.mlir" ERROR_VARIABLE m_output)
if(NOT m_result EQUAL 0)
  message(FATAL_ERROR "schema generation failed:\n${m_output}")
endif()

execute_process(
  COMMAND "${PYTHON_EXECUTABLE}" "${CROSSCHECK_SCRIPT}"
          --dsdlc "${DSDLC}"
          --c-root "${OUT_DIR}/c"
          --mlir "${OUT_DIR}/schema.mlir"
          --cc "${C_COMPILER}"
          --workdir "${OUT_DIR}/work"
  RESULT_VARIABLE check_result OUTPUT_VARIABLE check_output ERROR_VARIABLE check_output)
message(STATUS "${check_output}")
if(NOT check_result EQUAL 0)
  message(FATAL_ERROR "member layout cross-check failed")
endif()
