//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Implements the section metadata model of TypeMetadata.h.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/CodeGen/TypeMetadata.h"

#include <cstdint>
#include <optional>

#include <llvm/ADT/StringRef.h>
#include <llvm/Support/Casting.h>
#include <mlir/IR/Operation.h>

#include "llvmdsdl/CodeGen/SchemaLookup.h"
#include "llvmdsdl/Frontend/AST.h"
#include "llvmdsdl/IR/DSDLOps.h"
#include "llvmdsdl/Semantics/Model.h"

namespace llvmdsdl
{

SectionMetadata sectionMetadata(const DiscoveredDefinition& info,
                                const SemanticSection&      section,
                                mlir::dsdl::SchemaOp        schema,
                                const llvm::StringRef       sectionName)
{
    SectionMetadata out;
    // A service's sections are named after the service: `uavcan.node.ExecuteCommand.Request`. The
    // DSDL has no such name -- a section is not a type in the source -- but the generated type is a
    // type, and this is what it is called.
    out.fullName = info.fullName;
    if (sectionName == "request")
    {
        out.fullName += ".Request";
    }
    else if (sectionName == "response")
    {
        out.fullName += ".Response";
    }
    out.majorVersion                 = info.majorVersion;
    out.minorVersion                 = info.minorVersion;
    out.extentBytes                  = static_cast<std::uint64_t>(section.extentBits.value_or(0) / 8);
    out.serializationBufferSizeBytes = static_cast<std::uint64_t>((section.serializationBufferSizeBits + 7) / 8);
    out.deprecated                   = section.deprecated;
    out.alias                        = aliasVerdict(sectionPlan(schema, sectionName));
    out.unionTagBits                 = unionTagBits(sectionPlan(schema, sectionName));
    out.isUnion                      = section.isUnion;
    out.declaresPortId               = sectionName.empty();
    out.fixedPortId                  = out.declaresPortId ? info.fixedPortId : std::nullopt;

    if (!out.isUnion || !schema || schema.getBody().empty())
    {
        return out;
    }
    for (mlir::Operation& op : schema.getBody().front())
    {
        auto field = llvm::dyn_cast<mlir::dsdl::FieldOp>(op);
        if (!field || field.getPadding() || (field.getSection().value_or(llvm::StringRef{}) != sectionName))
        {
            continue;
        }
        out.unionOptions.push_back({field.getName().str(), field.getUnionOptionIndex().value_or(0)});
    }
    return out;
}

}  // namespace llvmdsdl
