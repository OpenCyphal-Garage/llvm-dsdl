add_executable(llvmdsdl-unit-tests
  UnitMain.cpp
  LexerTests.cpp
  ParserTests.cpp
  ConformanceTests.cpp
  BitLengthSetTests.cpp
  FlatSetTests.cpp
  EvaluatorTests.cpp
  DepfileRenderTests.cpp
  DepfilePlannerTests.cpp
  AnalyzerTests.cpp
  RuntimeTests.cpp
  ArrayWirePlanTests.cpp
  CHeaderRenderTests.cpp
  CodegenDiagnosticTextTests.cpp
  ConstantLiteralRenderTests.cpp
  CompositeImportGraphTests.cpp
  DefinitionDependenciesTests.cpp
  DefinitionIndexTests.cpp
  DefinitionPathProjectionTests.cpp
  NativeHelperContractTests.cpp
  LoweredBodyPlanTests.cpp
  LoweredFactsLookupTests.cpp
  LoweredRenderIRTests.cpp
  NativeEmitterTraversalTests.cpp
  SectionHelperBindingPlanTests.cpp
  SerDesStatementPlanTests.cpp
  ScriptedBodyPlanTests.cpp
  ScriptedOperationPlanTests.cpp
  HelperBindingNamingTests.cpp
  HelperBindingRenderTests.cpp
  RuntimeHelperBindingsTests.cpp
  NamingPolicyTests.cpp
  RuntimeLoweredPlanTests.cpp
  RuntimeLoweredOrderingTests.cpp
  HelperSymbolResolverTests.cpp
  WireLayoutFactsTests.cpp
  TypeStorageTests.cpp
  StorageTypeTokensTests.cpp
  LoweredContractVersionTests.cpp
  LoweredMetadataHardeningTests.cpp
  UavcanEmbeddedCatalogTests.cpp
  LspDocumentStoreTests.cpp
  LspRequestSchedulerTests.cpp
  LspAnalysisTests.cpp
  LspIndexTests.cpp
  LspLintTests.cpp
  LspRankingTests.cpp
  LspServerTests.cpp
  LspTelemetryAuditTests.cpp
  LspRobustnessTests.cpp
  LspJsonRpcFuzzTests.cpp
  LexerFuzzTests.cpp
  ParserFuzzTests.cpp
)

target_link_libraries(llvmdsdl-unit-tests
  PRIVATE
  llvmdsdlCodeGen
  llvmdsdlLSP
)

target_include_directories(llvmdsdl-unit-tests
  PRIVATE
  ${LLVMDSDL_SOURCE_DIR}/runtime
)

add_test(NAME llvmdsdl-unit-tests COMMAND llvmdsdl-unit-tests)

find_program(PYTHON_EXECUTABLE python3 QUIET)
if(PYTHON_EXECUTABLE)
  add_test(
    NAME llvmdsdl-python-unit-tests
    COMMAND
      ${CMAKE_COMMAND}
        -DDSDLC=$<TARGET_FILE:dsdlc>
        -DPYTHON_EXECUTABLE=${PYTHON_EXECUTABLE}
        -DFIXTURES_ROOT=${LLVMDSDL_SOURCE_DIR}/test/lit/fixtures
        -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/python-unit-tests-out
        -P ${CMAKE_CURRENT_SOURCE_DIR}/RunPythonUnitTests.cmake
  )

  set_tests_properties(
    llvmdsdl-python-unit-tests
    PROPERTIES
      LABELS "unit;python"
  )
else()
  message(STATUS "Skipping Python unit tests: python3 unavailable")
endif()
