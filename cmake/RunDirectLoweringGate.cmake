# Runs one direct-lowering acceptance gate. Set LLVMDSDL_DIRECT_LOWERING_STRICT to make an
# absent lowering a failure rather than a reported gap.

if(NOT DEFINED GATE)
  message(FATAL_ERROR "GATE is required")
endif()

set(gate_args
    "${GATE_SCRIPT}"
    "--gate" "${GATE}"
    "--dsdlc" "${DSDLC}"
    "--dsdl-opt" "${DSDL_OPT}"
    "--fixtures" "${FIXTURES_ROOT}"
    "--workdir" "${WORK_DIR}")

if(CC_COMPILER)
  list(APPEND gate_args "--cc" "${CC_COMPILER}")
endif()

if(STRICT)
  list(APPEND gate_args "--strict")
endif()

execute_process(
  COMMAND "${PYTHON_EXECUTABLE}" ${gate_args}
  RESULT_VARIABLE gate_result
  OUTPUT_VARIABLE gate_output
  ERROR_VARIABLE gate_error)

message(STATUS "${gate_output}${gate_error}")

if(NOT gate_result EQUAL 0)
  message(FATAL_ERROR "direct-lowering gate ${GATE} failed")
endif()
