#===----------------------------------------------------------------------===#
##
## @file
## Build rules for the serialisation helper shape vocabulary.
##
## The target sits below the MLIR lowering and the language emitters, which share its shape decisions.
##
#===----------------------------------------------------------------------===#

add_library(llvmdsdlSerDes
  HelperBodyPlan.cpp
)

target_include_directories(llvmdsdlSerDes PUBLIC
  ${LLVMDSDL_SOURCE_DIR}/include
  ${LLVMDSDL_BINARY_DIR}/include
)
