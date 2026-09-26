#===----------------------------------------------------------------------===#
##
## @file
## Build rules for language emitters and lowering planners.
##
## The target aggregates shared code-generation utilities and links MLIR conversion components required by emitters.
##
#===----------------------------------------------------------------------===#

# The runtime scaffolds and the built-in vocabulary bindings are embedded into
# the library as raw string literals, so a packaged dsdlc carries them. The
# embed only reads files, so it is regenerated at build time and never
# committed. See tools/generate_embedded_sources.py.
if(NOT LLVMDSDL_PYTHON3_EXE)
  message(FATAL_ERROR
    "python3 is required to embed the runtime and vocabulary sources "
    "(tools/generate_embedded_sources.py); none was found.")
endif()

set(LLVMDSDL_EMBEDDED_SOURCES_GENERATOR
    "${LLVMDSDL_SOURCE_DIR}/tools/generate_embedded_sources.py")
set(LLVMDSDL_EMBEDDED_SOURCES_INC
    "${CMAKE_CURRENT_BINARY_DIR}/EmbeddedSources.inc")

# The generator owns the manifest of embedded files; ask it for that list so the
# custom command depends on exactly the files it embeds (single source of truth,
# no drift between the DEPENDS list here and what gets baked in).
execute_process(
  COMMAND "${LLVMDSDL_PYTHON3_EXE}" "${LLVMDSDL_EMBEDDED_SOURCES_GENERATOR}" --print-manifest
  OUTPUT_VARIABLE LLVMDSDL_EMBEDDED_SOURCES_MANIFEST_RAW
  RESULT_VARIABLE LLVMDSDL_EMBEDDED_SOURCES_MANIFEST_RESULT
  ERROR_VARIABLE LLVMDSDL_EMBEDDED_SOURCES_MANIFEST_ERR
  OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT LLVMDSDL_EMBEDDED_SOURCES_MANIFEST_RESULT EQUAL 0)
  message(FATAL_ERROR
    "failed to read the embedded sources manifest via "
    "${LLVMDSDL_EMBEDDED_SOURCES_GENERATOR}:\n${LLVMDSDL_EMBEDDED_SOURCES_MANIFEST_ERR}")
endif()
string(REPLACE "\n" ";" LLVMDSDL_EMBEDDED_SOURCES_RELPATHS
       "${LLVMDSDL_EMBEDDED_SOURCES_MANIFEST_RAW}")
set(LLVMDSDL_EMBEDDED_SOURCES_DEPS "")
foreach(rel IN LISTS LLVMDSDL_EMBEDDED_SOURCES_RELPATHS)
  list(APPEND LLVMDSDL_EMBEDDED_SOURCES_DEPS "${LLVMDSDL_SOURCE_DIR}/${rel}")
endforeach()

add_custom_command(
  OUTPUT "${LLVMDSDL_EMBEDDED_SOURCES_INC}"
  COMMAND "${LLVMDSDL_PYTHON3_EXE}" "${LLVMDSDL_EMBEDDED_SOURCES_GENERATOR}"
          --source-root "${LLVMDSDL_SOURCE_DIR}"
          --output "${LLVMDSDL_EMBEDDED_SOURCES_INC}"
  DEPENDS
    "${LLVMDSDL_EMBEDDED_SOURCES_GENERATOR}"
    ${LLVMDSDL_EMBEDDED_SOURCES_DEPS}
  COMMENT "Embedding the runtime and vocabulary sources into llvmdsdlCodeGen"
  VERBATIM)
add_custom_target(llvmdsdl-embedded-sources
  DEPENDS "${LLVMDSDL_EMBEDDED_SOURCES_INC}")

add_library(llvmdsdlCodeGen
  EmbeddedSources.cpp
  BodyTranslator.cpp
  CompositeImportGraph.cpp
  ConstantLiteralRender.cpp
  DefinitionDependencies.cpp
  DefinitionIndex.cpp
  DefinitionPathProjection.cpp
  EmitCommon.cpp
  ImportSet.cpp
  CodegenDiagnosticText.cpp
  HelperBindingNaming.cpp
  InitializerRender.cpp
  NamingManifest.cpp
  SchemaLookup.cpp
  SchemaNaming.cpp
  SectionNaming.cpp
  StorageTypeTokens.cpp
  SourceWriter.cpp
  TypeMetadata.cpp
  TypeStorage.cpp
  UavcanEmbeddedCatalog.cpp
  Vocabulary.cpp
  emitter/C.cpp
  emitter/CHeaderRender.cpp
  emitter/Cpp.cpp
  emitter/Go.cpp
  emitter/Python.cpp
  emitter/Rust.cpp
  emitter/Ts.cpp
)

target_link_libraries(llvmdsdlCodeGen
  PUBLIC
  llvmdsdlLowering
  llvmdsdlTransforms
  llvmdsdlSerDes
  MLIRReconcileUnrealizedCasts
  MLIRParser
  MLIRTransforms
  # The object lane finishes the lowering the plan bodies are built into, then assembles it. The
  # target backends are linked whole so that emitting for a target is a matter of naming it.
  MLIRSCFToControlFlow
  MLIRControlFlowToLLVM
  MLIRArithToLLVM
  MLIRFuncToLLVM
  MLIRTargetLLVMIRExport
  MLIRBuiltinToLLVMIRTranslation
  MLIRLLVMToLLVMIRTranslation
)

# The object lane needs the target backends and the optimisation pipeline. Where the build
# provides a single shared LLVM, link it: adding the static component archives beside
# it puts two copies of LLVM in one binary, and analysis keys are per-copy statics -- the pass
# manager then looks up an analysis registered in the other copy and follows a null.
if(LLVM_LINK_LLVM_DYLIB)
  target_link_libraries(llvmdsdlCodeGen PUBLIC LLVM)
else()
  llvm_map_components_to_libnames(LLVMDSDL_OBJECT_COMPONENTS
    AllTargetsCodeGens AllTargetsAsmParsers AllTargetsDescs AllTargetsInfos
    Target CodeGen MC Core Support Passes
  )
  target_link_libraries(llvmdsdlCodeGen PUBLIC ${LLVMDSDL_OBJECT_COMPONENTS})
endif()

# The generated EmbeddedSources.inc lands in the build tree; the compiler depfile
# then makes EmbeddedSources.cpp recompile whenever an embedded source (and thus
# the .inc) changes. add_dependencies guarantees the .inc is generated before any
# object in this library is compiled.
add_dependencies(llvmdsdlCodeGen llvmdsdl-embedded-sources)

target_include_directories(llvmdsdlCodeGen PUBLIC
  ${LLVMDSDL_SOURCE_DIR}/include
  ${LLVMDSDL_BINARY_DIR}/include
)
target_include_directories(llvmdsdlCodeGen PRIVATE
  ${CMAKE_CURRENT_BINARY_DIR}
)
