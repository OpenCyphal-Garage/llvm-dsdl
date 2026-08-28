//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

#include <iostream>
#include <optional>
#include <vector>

#include "llvmdsdl/CodeGen/WireLayoutFacts.h"
#include "llvmdsdl/CodeGen/MlirLoweredFacts.h"
#include "llvmdsdl/Semantics/Model.h"

#include "UnitTests.h"

bool runWireLayoutFactsTests()
{
    {
        llvmdsdl::SemanticSection section;
        section.isUnion = true;
        llvmdsdl::SemanticField f;
        f.unionTagBits = 8;
        f.isPadding    = false;
        section.fields.push_back(f);
        if (llvmdsdl::resolveUnionTagBits(section, nullptr) != 8)
        {
            std::cerr << "union tag bits default resolution mismatch\n";
            return false;
        }
    }

    {
        llvmdsdl::SemanticSection section;
        section.isUnion = true;
        llvmdsdl::LoweredSectionFacts facts;
        facts.unionTagBits = 16;
        if (llvmdsdl::resolveUnionTagBits(section, &facts) != 16)
        {
            std::cerr << "union tag bits lowered override mismatch\n";
            return false;
        }
    }

    return true;
}
