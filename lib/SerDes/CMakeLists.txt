#===----------------------------------------------------------------------===#
##
## @file
## Build rules for the serialisation helper shape vocabulary.
##
## Sits below both the MLIR lowering and the language emitters: each has to answer
## the same question about the same field, so the answer lives where both can reach
## it. Depends on nothing but the standard library.
##
#===----------------------------------------------------------------------===#

add_library(llvmdsdlSerDes
  HelperBodyPlan.cpp
)

target_include_directories(llvmdsdlSerDes PUBLIC
  ${LLVMDSDL_SOURCE_DIR}/include
  ${LLVMDSDL_BINARY_DIR}/include
)
