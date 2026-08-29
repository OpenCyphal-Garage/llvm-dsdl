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

/// @brief Runs the TargetLanguagesTests suite.
/// @return True when every case in the suite passed.
bool runTargetLanguagesTests();

/// @brief Runs the CliPathTests suite.
/// @return True when every case in the suite passed.
bool runCliPathTests();

/// @brief Runs the FlatSetTests suite.
/// @return True when every case in the suite passed.
bool runFlatSetTests();

/// @brief Runs the EvaluatorTests suite.
/// @return True when every case in the suite passed.
bool runEvaluatorTests();

/// @brief Runs the DepfileRenderTests suite.
/// @return True when every case in the suite passed.
bool runDepfileRenderTests();

/// @brief Runs the DepfilePlannerTests suite.
/// @return True when every case in the suite passed.
bool runDepfilePlannerTests();

/// @brief Runs the TargetResolutionTests suite.
/// @return True when every case in the suite passed.
bool runTargetResolutionTests();

/// @brief Runs the SupportGenerationTests suite.
/// @return True when every case in the suite passed.
bool runSupportGenerationTests();

/// @brief Runs the AnalyzerTests suite.
/// @return True when every case in the suite passed.
bool runAnalyzerTests();

/// @brief Runs the RuntimeTests suite.
/// @return True when every case in the suite passed.
bool runRuntimeTests();

/// @brief Runs the ArrayWirePlanTests suite.
/// @return True when every case in the suite passed.
bool runArrayWirePlanTests();

/// @brief Runs the CHeaderRenderTests suite.
/// @return True when every case in the suite passed.
bool runCHeaderRenderTests();

/// @brief Runs the CodegenDiagnosticTextTests suite.
/// @return True when every case in the suite passed.
bool runCodegenDiagnosticTextTests();

/// @brief Runs the ConstantLiteralRenderTests suite.
/// @return True when every case in the suite passed.
bool runConstantLiteralRenderTests();

/// @brief Runs the CompositeImportGraphTests suite.
/// @return True when every case in the suite passed.
bool runCompositeImportGraphTests();

/// @brief Runs the DefinitionDependenciesTests suite.
/// @return True when every case in the suite passed.
bool runDefinitionDependenciesTests();

/// @brief Runs the DefinitionIndexTests suite.
/// @return True when every case in the suite passed.
bool runDefinitionIndexTests();

/// @brief Runs the DefinitionPathProjectionTests suite.
/// @return True when every case in the suite passed.
bool runDefinitionPathProjectionTests();

/// @brief Runs the NativeHelperContractTests suite.
/// @return True when every case in the suite passed.
bool runNativeHelperContractTests();

/// @brief Runs the LoweredBodyPlanTests suite.
/// @return True when every case in the suite passed.
bool runLoweredBodyPlanTests();

/// @brief Runs the LoweredFactsLookupTests suite.
/// @return True when every case in the suite passed.
bool runLoweredFactsLookupTests();

/// @brief Runs the LoweredRenderIRTests suite.
/// @return True when every case in the suite passed.
bool runLoweredRenderIRTests();

/// @brief Runs the NativeEmitterTraversalTests suite.
/// @return True when every case in the suite passed.
bool runNativeEmitterTraversalTests();

/// @brief Runs the SectionHelperBindingPlanTests suite.
/// @return True when every case in the suite passed.
bool runSectionHelperBindingPlanTests();

/// @brief Runs the SerDesStatementPlanTests suite.
/// @return True when every case in the suite passed.
bool runSerDesStatementPlanTests();

/// @brief Runs the ScriptedBodyPlanTests suite.
/// @return True when every case in the suite passed.
bool runScriptedBodyPlanTests();

/// @brief Runs the ScriptedOperationPlanTests suite.
/// @return True when every case in the suite passed.
bool runScriptedOperationPlanTests();

/// @brief Runs the HelperBodyPlanTests suite.
/// @return True when every case in the suite passed.
bool runHelperBodyPlanTests();

/// @brief Runs the HelperBindingNamingTests suite.
/// @return True when every case in the suite passed.
bool runHelperBindingNamingTests();

/// @brief Runs the RuntimeHelperBindingsTests suite.
/// @return True when every case in the suite passed.
bool runRuntimeHelperBindingsTests();

/// @brief Runs the NamingPolicyTests suite.
/// @return True when every case in the suite passed.
bool runNamingPolicyTests();

/// @brief Runs the NamingGoldenTests suite.
/// @return True when every case in the suite passed.
bool runNamingGoldenTests();

/// @brief Runs the RuntimeLoweredPlanTests suite.
/// @return True when every case in the suite passed.
bool runRuntimeLoweredPlanTests();

/// @brief Runs the RuntimeLoweredOrderingTests suite.
/// @return True when every case in the suite passed.
bool runRuntimeLoweredOrderingTests();

/// @brief Runs the HelperSymbolResolverTests suite.
/// @return True when every case in the suite passed.
bool runHelperSymbolResolverTests();

/// @brief Runs the WireLayoutFactsTests suite.
/// @return True when every case in the suite passed.
bool runWireLayoutFactsTests();

/// @brief Runs the TypeStorageTests suite.
/// @return True when every case in the suite passed.
bool runTypeStorageTests();

/// @brief Runs the StorageTypeTokensTests suite.
/// @return True when every case in the suite passed.
bool runStorageTypeTokensTests();

/// @brief Runs the LoweredContractVersionTests suite.
/// @return True when every case in the suite passed.
bool runLoweredContractVersionTests();

/// @brief Runs the LoweredMetadataHardeningTests suite.
/// @return True when every case in the suite passed.
bool runLoweredMetadataHardeningTests();

/// @brief Runs the UavcanEmbeddedCatalogTests suite.
/// @return True when every case in the suite passed.
bool runUavcanEmbeddedCatalogTests();

/// @brief Runs the LspDocumentStoreTests suite.
/// @return True when every case in the suite passed.
bool runLspDocumentStoreTests();

/// @brief Runs the LspRequestSchedulerTests suite.
/// @return True when every case in the suite passed.
bool runLspRequestSchedulerTests();

/// @brief Runs the LspAnalysisTests suite.
/// @return True when every case in the suite passed.
bool runLspAnalysisTests();

/// @brief Runs the LspIndexTests suite.
/// @return True when every case in the suite passed.
bool runLspIndexTests();

/// @brief Runs the LspLintTests suite.
/// @return True when every case in the suite passed.
bool runLspLintTests();

/// @brief Runs the LspRankingTests suite.
/// @return True when every case in the suite passed.
bool runLspRankingTests();

/// @brief Runs the LspServerTests suite.
/// @return True when every case in the suite passed.
bool runLspServerTests();

/// @brief Runs the LspTelemetryAuditTests suite.
/// @return True when every case in the suite passed.
bool runLspTelemetryAuditTests();

/// @brief Runs the LspRobustnessTests suite.
/// @return True when every case in the suite passed.
bool runLspRobustnessTests();

/// @brief Runs the LspPositionEncodingTests suite.
/// @return True when every case in the suite passed.
bool runLspPositionEncodingTests();

/// @brief Runs the LspStructuredLoggingTests suite.
/// @return True when every case in the suite passed.
bool runLspStructuredLoggingTests();

/// @brief Runs the LspAdversarialRequestTests suite.
/// @return True when every case in the suite passed.
bool runLspAdversarialRequestTests();

/// @brief Runs the LspJsonRpcFuzzTests suite.
/// @return True when every case in the suite passed.
bool runLspJsonRpcFuzzTests();

/// @brief Runs the LexerFuzzTests suite.
/// @return True when every case in the suite passed.
bool runLexerFuzzTests();

/// @brief Runs the ParserFuzzTests suite.
/// @return True when every case in the suite passed.
bool runParserFuzzTests();

#endif  // LLVMDSDL_TEST_UNIT_TESTS_H
