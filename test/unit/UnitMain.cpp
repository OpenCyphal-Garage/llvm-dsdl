//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

#include <iostream>

#include "UnitTests.h"

int main()
{
    bool ok = true;
    ok      = runLexerTests() && ok;
    ok      = runParserTests() && ok;
    ok      = runConformanceTests() && ok;
    ok      = runBitLengthSetTests() && ok;
    ok      = runIntegerMathTests() && ok;
    ok      = runTargetLanguagesTests() && ok;
    ok      = runCliPathTests() && ok;
    ok      = runFlatSetTests() && ok;
    ok      = runEvaluatorTests() && ok;
    ok      = runDepfileRenderTests() && ok;
    ok      = runDepfilePlannerTests() && ok;
    ok      = runTargetResolutionTests() && ok;
    ok      = runSupportGenerationTests() && ok;
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
    ok      = runNamingGoldenTests() && ok;
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
    ok      = runLspPositionEncodingTests() && ok;
    ok      = runLspStructuredLoggingTests() && ok;
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
