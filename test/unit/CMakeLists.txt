add_executable(llvmdsdl-unit-tests
  UnitMain.cpp
  LexerTests.cpp
  ParserTests.cpp
  ConformanceTests.cpp
  BitLengthSetTests.cpp
  IntegerMathTests.cpp
  FlatSetTests.cpp
  CliPathTests.cpp
  EvaluatorTests.cpp
  DepfilePlannerTests.cpp
  TargetResolutionTests.cpp
  AnalyzerTests.cpp
  RuntimeTests.cpp
  LexerFuzzTests.cpp
  ParserFuzzTests.cpp
)

target_link_libraries(llvmdsdl-unit-tests
  PRIVATE
  llvmdsdlSemantics
)

target_include_directories(llvmdsdl-unit-tests
  PRIVATE
  ${LLVMDSDL_SOURCE_DIR}/runtime
)

add_test(NAME llvmdsdl-unit-tests COMMAND llvmdsdl-unit-tests)
set_tests_properties(llvmdsdl-unit-tests PROPERTIES LABELS "t0;unit")

add_executable(llvmdsdl-bls-exactness-tests BlsExactnessTests.cpp)
target_link_libraries(llvmdsdl-bls-exactness-tests PRIVATE llvmdsdlSemantics)
add_test(NAME llvmdsdl-bls-exactness-tests COMMAND llvmdsdl-bls-exactness-tests)
set_tests_properties(llvmdsdl-bls-exactness-tests PROPERTIES LABELS "t0;unit")

if(LLVMDSDL_PYTHON3_EXE)
  add_test(
    NAME llvmdsdl-prdt-corpus-tool-tests
    COMMAND ${LLVMDSDL_PYTHON3_EXE} ${LLVMDSDL_SOURCE_DIR}/tools/test_generate_prdt_corpus.py
  )
  set_tests_properties(llvmdsdl-prdt-corpus-tool-tests PROPERTIES LABELS "t0;unit;python")
endif()
