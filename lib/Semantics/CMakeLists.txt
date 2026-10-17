#===----------------------------------------------------------------------===#
##
## @file
## Build rules for semantic analysis components.
##
## The semantics target links evaluation and bit-length reasoning modules that validate parsed DSDL definitions.
##
#===----------------------------------------------------------------------===#

add_library(llvmdsdlSemantics
  BitLengthSet.cpp
  RunSet.cpp
  Evaluator.cpp
  Analyzer.cpp
)

target_link_libraries(llvmdsdlSemantics
  PUBLIC
  llvmdsdlFrontend
)

target_include_directories(llvmdsdlSemantics PUBLIC
  ${LLVMDSDL_SOURCE_DIR}/include
  ${LLVMDSDL_BINARY_DIR}/include
)
