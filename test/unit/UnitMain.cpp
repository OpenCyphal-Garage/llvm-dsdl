//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

#include <iostream>

bool runLexerTests();
bool runParserTests();
bool runConformanceTests();
bool runBitLengthSetTests();
bool runEvaluatorTests();
bool runDepfileRenderTests();
bool runDepfilePlannerTests();
bool runAnalyzerTests();
bool runRuntimeTests();
bool runArrayWirePlanTests();
bool runCHeaderRenderTests();
bool runCodegenDiagnosticTextTests();
bool runConstantLiteralRenderTests();
bool runCompositeImportGraphTests();
bool runDefinitionDependenciesTests();
bool runDefinitionIndexTests();
bool runDefinitionPathProjectionTests();
bool runNativeHelperContractTests();
bool runLoweredBodyPlanTests();
bool runLoweredFactsLookupTests();
bool runLoweredRenderIRTests();
bool runNativeEmitterTraversalTests();
bool runSectionHelperBindingPlanTests();
bool runSerDesStatementPlanTests();
bool runScriptedBodyPlanTests();
bool runScriptedOperationPlanTests();
bool runHelperBindingRenderTests();
bool runHelperBindingNamingTests();
bool runRuntimeHelperBindingsTests();
bool runNamingPolicyTests();
bool runRuntimeLoweredPlanTests();
bool runRuntimeLoweredOrderingTests();
bool runHelperSymbolResolverTests();
bool runWireLayoutFactsTests();
bool runTypeStorageTests();
bool runStorageTypeTokensTests();
bool runLoweredContractVersionTests();
bool runLoweredMetadataHardeningTests();
bool runUavcanEmbeddedCatalogTests();
bool runLspDocumentStoreTests();
bool runLspRequestSchedulerTests();
bool runLspAnalysisTests();
bool runLspIndexTests();
bool runLspLintTests();
bool runLspRankingTests();
bool runLspServerTests();
bool runLspTelemetryAuditTests();
bool runLspRobustnessTests();
bool runLspAdversarialRequestTests();
bool runLspJsonRpcFuzzTests();
bool runLexerFuzzTests();
bool runParserFuzzTests();

int main()
{
    bool ok = true;
    ok      = runLexerTests() && ok;
    ok      = runParserTests() && ok;
    ok      = runConformanceTests() && ok;
    ok      = runBitLengthSetTests() && ok;
    ok      = runEvaluatorTests() && ok;
    ok      = runDepfileRenderTests() && ok;
    ok      = runDepfilePlannerTests() && ok;
    ok      = runAnalyzerTests() && ok;
    ok      = runRuntimeTests() && ok;
    ok      = runArrayWirePlanTests() && ok;
    ok      = runCHeaderRenderTests() && ok;
    ok      = runCodegenDiagnosticTextTests() && ok;
    ok      = runConstantLiteralRenderTests() && ok;
    ok      = runCompositeImportGraphTests() && ok;
    ok      = runDefinitionDependenciesTests() && ok;
    ok      = runDefinitionIndexTests() && ok;
    ok      = runDefinitionPathProjectionTests() && ok;
    ok      = runNativeHelperContractTests() && ok;
    ok      = runLoweredBodyPlanTests() && ok;
    ok      = runLoweredFactsLookupTests() && ok;
    ok      = runLoweredRenderIRTests() && ok;
    ok      = runNativeEmitterTraversalTests() && ok;
    ok      = runSectionHelperBindingPlanTests() && ok;
    ok      = runSerDesStatementPlanTests() && ok;
    ok      = runScriptedBodyPlanTests() && ok;
    ok      = runScriptedOperationPlanTests() && ok;
    ok      = runHelperBindingRenderTests() && ok;
    ok      = runHelperBindingNamingTests() && ok;
    ok      = runRuntimeHelperBindingsTests() && ok;
    ok      = runNamingPolicyTests() && ok;
    ok      = runRuntimeLoweredPlanTests() && ok;
    ok      = runRuntimeLoweredOrderingTests() && ok;
    ok      = runHelperSymbolResolverTests() && ok;
    ok      = runWireLayoutFactsTests() && ok;
    ok      = runTypeStorageTests() && ok;
    ok      = runStorageTypeTokensTests() && ok;
    ok      = runLoweredContractVersionTests() && ok;
    ok      = runLoweredMetadataHardeningTests() && ok;
    ok      = runUavcanEmbeddedCatalogTests() && ok;
    ok      = runLspDocumentStoreTests() && ok;
    ok      = runLspRequestSchedulerTests() && ok;
    ok      = runLspAnalysisTests() && ok;
    ok      = runLspIndexTests() && ok;
    ok      = runLspLintTests() && ok;
    ok      = runLspRankingTests() && ok;
    ok      = runLspServerTests() && ok;
    ok      = runLspTelemetryAuditTests() && ok;
    ok      = runLspRobustnessTests() && ok;
    ok      = runLspAdversarialRequestTests() && ok;
    ok      = runLspJsonRpcFuzzTests() && ok;
    ok      = runLexerFuzzTests() && ok;
    ok      = runParserFuzzTests() && ok;
    if (!ok)
    {
        std::cerr << "unit tests failed\n";
        return 1;
    }
    std::cout << "unit tests passed\n";
    return 0;
}
