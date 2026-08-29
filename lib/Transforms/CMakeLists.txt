#===----------------------------------------------------------------------===#
##
## @file
## Build rules for DSDL transformation passes.
##
## The transforms target compiles pass pipelines that prepare dialect IR for language-specific emission stages.
##
#===----------------------------------------------------------------------===#

add_library(llvmdsdlTransforms
  LoweredSerDesContractValidation.cpp
  Passes.cpp
  ConvertDSDLToEmitC.cpp
)

target_link_libraries(llvmdsdlTransforms
  PUBLIC
  llvmdsdlIR
  llvmdsdlSerDes
  MLIRIR
  MLIRPass
  MLIRTransforms
  MLIREmitCDialect
  MLIRFuncDialect
  MLIRArithDialect
  MLIRSCFDialect
)

target_include_directories(llvmdsdlTransforms PUBLIC
  ${LLVMDSDL_SOURCE_DIR}/include
  ${LLVMDSDL_BINARY_DIR}/include
)
