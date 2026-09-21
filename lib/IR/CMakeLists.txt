#===----------------------------------------------------------------------===#
##
## @file
## Build rules for the DSDL MLIR dialect library.
##
## This target compiles generated dialect artifacts and links the MLIR dialect infrastructure dependencies.
##
#===----------------------------------------------------------------------===#

add_library(llvmdsdlIR
  DSDLDialect.cpp
  DSDLOps.cpp
)
add_dependencies(llvmdsdlIR LLVMDSDLIRIncGen)

target_link_libraries(llvmdsdlIR
  PUBLIC
  MLIRIR
  MLIRFuncDialect
)

target_include_directories(llvmdsdlIR PUBLIC
  ${LLVMDSDL_SOURCE_DIR}/include
  ${LLVMDSDL_BINARY_DIR}/include
)
