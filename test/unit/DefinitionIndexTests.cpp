//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

#include <iostream>

#include "llvmdsdl/CodeGen/DefinitionIndex.h"
#include "llvmdsdl/Semantics/Model.h"

#include "UnitTests.h"

bool runDefinitionIndexTests()
{
    llvmdsdl::SemanticModule module;

    llvmdsdl::SemanticDefinition alpha;
    alpha.info.fullName     = "vendor.alpha.Widget";
    alpha.info.majorVersion = 1;
    alpha.info.minorVersion = 0;
    alpha.info.shortName    = "Type";
    module.definitions.push_back(alpha);

    llvmdsdl::SemanticDefinition beta;
    beta.info.fullName     = "vendor.beta.Other";
    beta.info.majorVersion = 2;
    beta.info.minorVersion = 1;
    beta.info.shortName    = "Other";
    module.definitions.push_back(beta);

    llvmdsdl::DefinitionIndex const index(module);

    llvmdsdl::SemanticTypeRef alphaRef;
    alphaRef.fullName     = "vendor.alpha.Widget";
    alphaRef.majorVersion = 1;
    alphaRef.minorVersion = 0;

    const auto* const foundAlpha = index.find(alphaRef);
    if (foundAlpha == nullptr || foundAlpha->info.fullName != "vendor.alpha.Widget")
    {
        std::cerr << "definition index failed to resolve existing definition\n";
        return false;
    }

    llvmdsdl::SemanticTypeRef missingRef;
    missingRef.fullName     = "vendor.gamma.Missing";
    missingRef.majorVersion = 1;
    missingRef.minorVersion = 0;
    if (index.find(missingRef) != nullptr)
    {
        std::cerr << "definition index should return null for missing definitions\n";
        return false;
    }

    return true;
}
