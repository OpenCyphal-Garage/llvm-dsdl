#===----------------------------------------------------------------------===#
##
## @file
## Build rules for language emitters and lowering planners.
##
## The target aggregates shared code-generation utilities and links MLIR conversion components required by emitters.
##
#===----------------------------------------------------------------------===#

# Embed the language-runtime support sources (runtime/) into this library so the
# C/C++/Rust/Go/Python emitters resolve them from the binary itself rather than
# from an absolute source path baked in at compile time -- which is what lets a
# packaged/installed dsdlc run outside a source checkout. Unlike the embedded
# UAVCAN MLIR catalog (which needs a built dsdlc to produce, hence is committed),
# this embed only reads files, so it is regenerated at build time from the
# runtime sources and never committed. See tools/runtime/generate_embedded_runtime.py.
if(NOT LLVMDSDL_PYTHON3_EXE)
  message(FATAL_ERROR
    "python3 is required to embed the runtime support sources "
    "(tools/runtime/generate_embedded_runtime.py); none was found.")
endif()

set(LLVMDSDL_EMBEDDED_RUNTIME_GENERATOR
    "${LLVMDSDL_SOURCE_DIR}/tools/runtime/generate_embedded_runtime.py")
set(LLVMDSDL_EMBEDDED_RUNTIME_INC
    "${CMAKE_CURRENT_BINARY_DIR}/EmbeddedRuntimeSources.inc")

# The generator owns the manifest of embedded files; ask it for that list so the
# custom command depends on exactly the files it embeds (single source of truth,
# no drift between the DEPENDS list here and what actually gets baked in).
execute_process(
  COMMAND "${LLVMDSDL_PYTHON3_EXE}" "${LLVMDSDL_EMBEDDED_RUNTIME_GENERATOR}" --print-manifest
  OUTPUT_VARIABLE LLVMDSDL_EMBEDDED_RUNTIME_MANIFEST_RAW
  RESULT_VARIABLE LLVMDSDL_EMBEDDED_RUNTIME_MANIFEST_RESULT
  ERROR_VARIABLE LLVMDSDL_EMBEDDED_RUNTIME_MANIFEST_ERR
  OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT LLVMDSDL_EMBEDDED_RUNTIME_MANIFEST_RESULT EQUAL 0)
  message(FATAL_ERROR
    "failed to read embedded runtime manifest via "
    "${LLVMDSDL_EMBEDDED_RUNTIME_GENERATOR}:\n${LLVMDSDL_EMBEDDED_RUNTIME_MANIFEST_ERR}")
endif()
string(REPLACE "\n" ";" LLVMDSDL_EMBEDDED_RUNTIME_RELPATHS
       "${LLVMDSDL_EMBEDDED_RUNTIME_MANIFEST_RAW}")
set(LLVMDSDL_EMBEDDED_RUNTIME_DEPS "")
foreach(rel IN LISTS LLVMDSDL_EMBEDDED_RUNTIME_RELPATHS)
  list(APPEND LLVMDSDL_EMBEDDED_RUNTIME_DEPS
       "${LLVMDSDL_SOURCE_DIR}/runtime/${rel}")
endforeach()

add_custom_command(
  OUTPUT "${LLVMDSDL_EMBEDDED_RUNTIME_INC}"
  COMMAND "${LLVMDSDL_PYTHON3_EXE}" "${LLVMDSDL_EMBEDDED_RUNTIME_GENERATOR}"
          --runtime-root "${LLVMDSDL_SOURCE_DIR}/runtime"
          --output "${LLVMDSDL_EMBEDDED_RUNTIME_INC}"
  DEPENDS
    "${LLVMDSDL_EMBEDDED_RUNTIME_GENERATOR}"
    ${LLVMDSDL_EMBEDDED_RUNTIME_DEPS}
  COMMENT "Embedding language-runtime support sources into llvmdsdlCodeGen"
  VERBATIM)
add_custom_target(llvmdsdl-embedded-runtime-sources
  DEPENDS "${LLVMDSDL_EMBEDDED_RUNTIME_INC}")

add_library(llvmdsdlCodeGen
  EmbeddedRuntimeSources.cpp
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
  SectionHelperBodies.cpp
  HelperSymbolResolver.cpp
  LoweredBodyPlan.cpp
  LoweredFactsLookup.cpp
  LoweredRenderIR.cpp
  NativeHelperContract.cpp
  NativeEmitterTraversal.cpp
  NativeFunctionSkeleton.cpp
  MlirLoweredFacts.cpp
  ObjectEmitter.cpp
  RuntimeHelperBindings.cpp
  RuntimeLoweredPlan.cpp
  ScriptedBodyPlan.cpp
  NamingManifest.cpp
  SectionNaming.cpp
  ScriptedOperationPlan.cpp
  StorageTypeTokens.cpp
  PythonEmitter.cpp
  RustEmitter.cpp
  SectionHelperBindingPlan.cpp
  SerDesStatementPlan.cpp
  SerDesHelperDescriptors.cpp
  SourceWriter.cpp
  TsEmitter.cpp
  TypeStorage.cpp
  WireLayoutFacts.cpp
  UavcanEmbeddedCatalog.cpp
)

target_link_libraries(llvmdsdlCodeGen
  PUBLIC
  llvmdsdlLowering
  llvmdsdlTransforms
  llvmdsdlSerDes
  MLIRFuncToEmitC
  MLIRArithToEmitC
  MLIRSCFToEmitC
  MLIRParser
  MLIRTargetCpp
  MLIRTransforms
)

# The generated EmbeddedRuntimeSources.inc lands in the build tree; the compiler
# depfile then makes EmbeddedRuntimeSources.cpp recompile whenever a runtime
# source (and thus the .inc) changes. add_dependencies guarantees the .inc is
# generated before any object in this library is compiled.
add_dependencies(llvmdsdlCodeGen llvmdsdl-embedded-runtime-sources)

target_include_directories(llvmdsdlCodeGen PUBLIC
  ${LLVMDSDL_SOURCE_DIR}/include
  ${LLVMDSDL_BINARY_DIR}/include
)
target_include_directories(llvmdsdlCodeGen PRIVATE
  ${CMAKE_CURRENT_BINARY_DIR}
)
