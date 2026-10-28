//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Entry points of the unit-test suites, one per translation unit.
///
/// Each suite defines its own entry point and UnitMain.cpp calls them in the order declared here.
/// The declarations live in a header so the definitions keep external linkage on purpose rather
/// than by omission, which is also what tells clang-tidy's misc-use-internal-linkage that these
/// are reached from another translation unit.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_TEST_UNIT_TESTS_H
#define LLVMDSDL_TEST_UNIT_TESTS_H

/// @brief Runs the LexerTests suite.
/// @return True when every case in the suite passed.
bool runLexerTests();

/// @brief Runs the ParserTests suite.
/// @return True when every case in the suite passed.
bool runParserTests();

/// @brief Runs the ConformanceTests suite.
/// @return True when every case in the suite passed.
bool runConformanceTests();

/// @brief Runs the BitLengthSetTests suite.
/// @return True when every case in the suite passed.
bool runBitLengthSetTests();

/// @brief Runs the IntegerMathTests suite.
/// @return True when every case in the suite passed.
bool runIntegerMathTests();

/// @brief Runs the CliPathTests suite.
/// @return True when every case in the suite passed.
bool runCliPathTests();

/// @brief Runs the FlatSetTests suite.
/// @return True when every case in the suite passed.
bool runFlatSetTests();

/// @brief Runs the EvaluatorTests suite.
/// @return True when every case in the suite passed.
bool runEvaluatorTests();

/// @brief Runs the DepfilePlannerTests suite.
/// @return True when every case in the suite passed.
bool runDepfilePlannerTests();

/// @brief Runs the TargetResolutionTests suite.
/// @return True when every case in the suite passed.
bool runTargetResolutionTests();

/// @brief Runs the AnalyzerTests suite.
/// @return True when every case in the suite passed.
bool runAnalyzerTests();

/// @brief Runs the RuntimeTests suite.
/// @return True when every case in the suite passed.
bool runRuntimeTests();

/// @brief Runs the LexerFuzzTests suite.
/// @return True when every case in the suite passed.
bool runLexerFuzzTests();

/// @brief Runs the ParserFuzzTests suite.
/// @return True when every case in the suite passed.
bool runParserFuzzTests();

#endif  // LLVMDSDL_TEST_UNIT_TESTS_H
