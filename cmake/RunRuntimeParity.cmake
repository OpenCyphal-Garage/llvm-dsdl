# Builds the serialisation primitives as IR, then holds them against the runtime header's own.
#
# The primitives are built from the wire format rather than compiled from the header, so the two
# are separate implementations of one contract and only a comparison keeps them together.

if(NOT DEFINED DSDL_OPT OR NOT DEFINED OUT_DIR OR NOT DEFINED C_COMPILER)
  message(FATAL_ERROR "DSDL_OPT, OUT_DIR and C_COMPILER are required")
endif()

file(REMOVE_RECURSE "${OUT_DIR}")
file(MAKE_DIRECTORY "${OUT_DIR}")
file(WRITE "${OUT_DIR}/empty.mlir" "module {}\n")

execute_process(
  COMMAND "${DSDL_OPT}" --emit-dsdl-runtime "${OUT_DIR}/empty.mlir"
  OUTPUT_FILE "${OUT_DIR}/rt.mlir" RESULT_VARIABLE r ERROR_VARIABLE e)
if(NOT r EQUAL 0)
  message(FATAL_ERROR "emitting the primitives failed:\n${e}")
endif()

execute_process(
  COMMAND "${MLIR_OPT}" --convert-scf-to-cf --convert-cf-to-llvm --convert-arith-to-llvm
          --convert-func-to-llvm --reconcile-unrealized-casts "${OUT_DIR}/rt.mlir"
  OUTPUT_FILE "${OUT_DIR}/rt.llvm.mlir" RESULT_VARIABLE r ERROR_VARIABLE e)
if(NOT r EQUAL 0)
  message(FATAL_ERROR "lowering the primitives failed:\n${e}")
endif()

execute_process(
  COMMAND "${MLIR_TRANSLATE}" --mlir-to-llvmir "${OUT_DIR}/rt.llvm.mlir"
  OUTPUT_FILE "${OUT_DIR}/rt.ll" RESULT_VARIABLE r ERROR_VARIABLE e)
if(NOT r EQUAL 0)
  message(FATAL_ERROR "translating the primitives failed:\n${e}")
endif()

# The header's own definitions are `static inline`, so the driver carries a copy of each under
# its own name. Renaming what came from IR is what lets both be called from one program.
file(READ "${OUT_DIR}/rt.ll" ir_text)
string(REPLACE "@dsdl_runtime_" "@ir_dsdl_runtime_" ir_text "${ir_text}")
# An object keeps its own copy and exports none, as `static inline` does. The driver has to be
# able to call them, so the copy under test is exported here and only here.
string(REPLACE "define internal " "define " ir_text "${ir_text}")
file(WRITE "${OUT_DIR}/rt.ll" "${ir_text}")

execute_process(
  COMMAND "${LLC}" -filetype=obj -o "${OUT_DIR}/rt.o" "${OUT_DIR}/rt.ll"
  RESULT_VARIABLE r ERROR_VARIABLE e)
if(NOT r EQUAL 0)
  message(FATAL_ERROR "assembling the primitives failed:\n${e}")
endif()

foreach(part integers floats)
  execute_process(
    COMMAND "${C_COMPILER}" -std=c11 "-I${RUNTIME_DIR}" -o "${OUT_DIR}/${part}"
            "${DRIVER_DIR}/${part}.c" "${OUT_DIR}/rt.o"
    RESULT_VARIABLE r ERROR_VARIABLE e)
  if(NOT r EQUAL 0)
    message(FATAL_ERROR "the ${part} driver failed to build:\n${e}")
  endif()
  execute_process(COMMAND "${OUT_DIR}/${part}" RESULT_VARIABLE r OUTPUT_VARIABLE out ERROR_VARIABLE out)
  message(STATUS "${out}")
  if(NOT r EQUAL 0)
    message(FATAL_ERROR "the primitives built as IR do not match the runtime header")
  endif()
endforeach()
