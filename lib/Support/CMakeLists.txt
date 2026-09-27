#===----------------------------------------------------------------------===#
##
## @file
## Build rules for shared support utilities.
##
## This target packages foundational helpers such as diagnostics, rational arithmetic, and source location formatting.
##
#===----------------------------------------------------------------------===#

add_library(llvmdsdlSupport
  BodyNaming.cpp
  CliPath.cpp
  DefinitionNaming.cpp
  ImportNameScope.cpp
  Diagnostics.cpp
  LanguageTraits.cpp
  NameCanonicalization.cpp
  NamingPolicy.cpp
  PlanSymbol.cpp
  ScalarStorage.cpp
  SectionScopes.cpp
  SurfacePlan.cpp
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
