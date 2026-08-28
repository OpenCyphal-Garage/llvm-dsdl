//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

#include <iostream>
#include <utility>

#include "llvmdsdl/CodeGen/LoweredFactsLookup.h"

#include "UnitTests.h"
#include "llvmdsdl/CodeGen/MlirLoweredFacts.h"
#include "llvmdsdl/Semantics/Model.h"

bool runLoweredFactsLookupTests()
{
    llvmdsdl::SemanticDefinition def;
    def.info.fullName     = "uavcan.node.Heartbeat";
    def.info.majorVersion = 1;
    def.info.minorVersion = 0;

    llvmdsdl::LoweredFactsMap        facts;
    llvmdsdl::LoweredDefinitionFacts definitionFacts;
    llvmdsdl::LoweredSectionFacts    requestFacts;
    requestFacts.capacityCheckHelper = "mlir_cap";
    definitionFacts.emplace("request", requestFacts);
    llvmdsdl::LoweredSectionFacts messageFacts;
    messageFacts.capacityCheckHelper = "mlir_msg_cap";
    definitionFacts.emplace("", messageFacts);
    facts.emplace(llvmdsdl::loweredTypeKey(def.info.fullName, def.info.majorVersion, def.info.minorVersion),
                  std::move(definitionFacts));

    const auto* request = llvmdsdl::lookupLoweredSectionFacts(facts, def, "request");
    if (request == nullptr || request->capacityCheckHelper != "mlir_cap")
    {
        std::cerr << "lookupLoweredSectionFacts failed to resolve request section\n";
        return false;
    }

    const auto* message = llvmdsdl::lookupLoweredSectionFacts(facts, def, "");
    if (message == nullptr || message->capacityCheckHelper != "mlir_msg_cap")
    {
        std::cerr << "lookupLoweredSectionFacts failed to resolve message section\n";
        return false;
    }

    if (llvmdsdl::lookupLoweredSectionFacts(facts, def, "response") != nullptr)
    {
        std::cerr << "lookupLoweredSectionFacts should return null for missing section\n";
        return false;
    }

    llvmdsdl::SemanticDefinition missing;
    missing.info.fullName     = "uavcan.node.Other";
    missing.info.majorVersion = 1;
    missing.info.minorVersion = 0;
    if (llvmdsdl::lookupLoweredSectionFacts(facts, missing, "") != nullptr)
    {
        std::cerr << "lookupLoweredSectionFacts should return null for missing definition\n";
        return false;
    }

    return true;
}
