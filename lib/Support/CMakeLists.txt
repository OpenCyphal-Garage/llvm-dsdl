#===----------------------------------------------------------------------===#
##
## @file
## Build rules for shared support utilities.
##
## This target packages foundational helpers such as diagnostics, rational arithmetic, and source location formatting.
##
#===----------------------------------------------------------------------===#

add_library(llvmdsdlSupport
  Diagnostics.cpp
  Rational.cpp
  ReservedIdentifiers.cpp
  SourceLocation.cpp
)

target_link_libraries(llvmdsdlSupport
  PUBLIC
  LLVMSupport
)

target_include_directories(llvmdsdlSupport PUBLIC
  ${LLVMDSDL_SOURCE_DIR}/include
  ${LLVMDSDL_BINARY_DIR}/include
)
