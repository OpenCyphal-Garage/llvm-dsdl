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
    ok      = runCliPathTests() && ok;
    ok      = runFlatSetTests() && ok;
    ok      = runEvaluatorTests() && ok;
    ok      = runDepfilePlannerTests() && ok;
    ok      = runTargetResolutionTests() && ok;
    ok      = runAnalyzerTests() && ok;
    ok      = runRuntimeTests() && ok;
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
