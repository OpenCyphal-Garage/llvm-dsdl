//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Builds the section scopes from a section of the semantic model.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/CodeGen/SectionNaming.h"

#include <optional>

#include <llvm/ADT/StringRef.h>

#include "llvmdsdl/Frontend/AST.h"
#include "llvmdsdl/CodeGen/TypeStorage.h"
#include "llvmdsdl/Semantics/Model.h"
#include "llvmdsdl/Support/DefinitionNaming.h"
#include "llvmdsdl/Support/Language.h"
#include "llvmdsdl/Support/NamingPolicy.h"
#include "llvmdsdl/Support/SectionScopes.h"
#include "llvmdsdl/Support/SurfacePlan.h"

namespace llvmdsdl
{

SectionParts sectionParts(const SemanticSection& section)
{
    SectionParts parts;
    parts.isUnion = section.isUnion;
    parts.fields.reserve(section.fields.size());
    for (const SemanticField& field : section.fields)
    {
        const std::optional<SemanticTypeRef>& composite = field.resolvedType.compositeType;
        parts.fields.push_back(
            FieldParts{.name             = field.name,
                       .padding          = field.isPadding,
                       .array            = field.resolvedType.arrayKind != ArrayKind::None,
                       .variableLength   = isVariableArray(field.resolvedType.arrayKind),
                       .unionOptionIndex = field.unionOptionIndex,
                       .composite        = composite ? std::optional(definitionRef(*composite)) : std::nullopt,
                       .view             = field.heldAsView});
    }
    parts.constants.reserve(section.constants.size());
    for (const SemanticConstant& constant : section.constants)
    {
        parts.constants.push_back(constant.name);
    }
    return parts;
}

DefinitionRef definitionRef(const SemanticTypeRef& ref)
{
    return DefinitionRef{.namespaceComponents = ref.namespaceComponents,
                         .shortName           = ref.shortName,
                         .majorVersion        = ref.majorVersion,
                         .minorVersion        = ref.minorVersion};
}

DefinitionRef definitionRef(const DiscoveredDefinition& info)
{
    return DefinitionRef{.namespaceComponents = info.namespaceComponents,
                         .shortName           = info.shortName,
                         .majorVersion        = info.majorVersion,
                         .minorVersion        = info.minorVersion};
}

DefinitionParts definitionParts(const SemanticDefinition& definition)
{
    return DefinitionParts{.ref         = definitionRef(definition.info),
                           .fixedPortId = definition.info.fixedPortId,
                           .service     = definition.isService,
                           .deprecated  = definition.request.deprecated,
                           .request     = sectionParts(definition.request),
                           .response    = definition.response
                                              ? std::optional<SectionParts>(sectionParts(*definition.response))
                                              : std::nullopt,
                           .bodies      = {}};
}

NamingScope makeSectionFieldScope(const Language         language,
                                  const SemanticSection& section,
                                  const llvm::StringRef  declaredTypeName)
{
    return makeSectionFieldScope(language, sectionParts(section), declaredTypeName);
}

NamingScope makeSectionConstantScope(const Language         language,
                                     const SemanticSection& section,
                                     const llvm::StringRef  declaredTypeName)
{
    return makeSectionConstantScope(language, sectionParts(section), declaredTypeName);
}

NamingScope makeGoConstantScope(const SemanticSection& section, const llvm::StringRef typeName)
{
    return makeGoConstantScope(sectionParts(section), typeName);
}

}  // namespace llvmdsdl
