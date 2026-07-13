#===----------------------------------------------------------------------===#
##
## @file
## Build rules for language emitters and lowering planners.
##
## The target aggregates shared code-generation utilities and links MLIR conversion components required by emitters.
##
#===----------------------------------------------------------------------===#

add_library(llvmdsdlCodeGen
  ArrayWirePlan.cpp
  CHeaderRender.cpp
  CompositeImportGraph.cpp
  ConstantLiteralRender.cpp
  DefinitionDependencies.cpp
  DefinitionIndex.cpp
  DefinitionPathProjection.cpp
  EmitCommon.cpp
  EmitStepRender.cpp
  CEmitter.cpp
  CppEmitter.cpp
  CppObjectAbiEmitter.cpp
  GoEmitter.cpp
  CodegenDiagnosticText.cpp
  HelperBindingNaming.cpp
  HelperBindingRender.cpp
  HelperSymbolResolver.cpp
  LoweredBodyPlan.cpp
  LoweredFactsLookup.cpp
  LoweredRenderIR.cpp
  NativeHelperContract.cpp
  NativeEmitterTraversal.cpp
  NativeFunctionSkeleton.cpp
  NamingPolicy.cpp
  MlirLoweredFacts.cpp
  ObjectEmitter.cpp
  RuntimeHelperBindings.cpp
  RuntimeLoweredPlan.cpp
  ScriptedBodyPlan.cpp
  ScriptedOperationPlan.cpp
  StorageTypeTokens.cpp
  PythonEmitter.cpp
  RustEmitter.cpp
  SectionHelperBindingPlan.cpp
  SerDesStatementPlan.cpp
  SerDesHelperDescriptors.cpp
  TsEmitter.cpp
  TypeStorage.cpp
  WireLayoutFacts.cpp
  UavcanEmbeddedCatalog.cpp
)

target_link_libraries(llvmdsdlCodeGen
  PUBLIC
  llvmdsdlLowering
  llvmdsdlTransforms
  MLIRFuncToEmitC
  MLIRArithToEmitC
  MLIRSCFToEmitC
  MLIRParser
  MLIRTargetCpp
  MLIRTransforms
)

target_include_directories(llvmdsdlCodeGen PUBLIC
  ${LLVMDSDL_SOURCE_DIR}/include
  ${LLVMDSDL_BINARY_DIR}/include
)

target_compile_definitions(llvmdsdlCodeGen
  PRIVATE
  LLVMDSDL_SOURCE_DIR="${LLVMDSDL_SOURCE_DIR}"
)
