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
        parts.fields.push_back(FieldParts{.name             = field.name,
                                          .padding          = field.isPadding,
                                          .array            = field.resolvedType.arrayKind != ArrayKind::None,
                                          .unionOptionIndex = field.unionOptionIndex});
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

DefinitionParts definitionParts(const SemanticDefinition& definition)
{
    return DefinitionParts{.ref         = DefinitionRef{.namespaceComponents = definition.info.namespaceComponents,
                                                        .shortName           = definition.info.shortName,
                                                        .majorVersion        = definition.info.majorVersion,
                                                        .minorVersion        = definition.info.minorVersion},
                           .fixedPortId = definition.info.fixedPortId,
                           .service     = definition.isService,
                           .deprecated  = definition.request.deprecated,
                           .request     = sectionParts(definition.request),
                           .response    = definition.response
                                              ? std::optional<SectionParts>(sectionParts(*definition.response))
                                              : std::nullopt,
                           .bodies      = {}};
}

NamingScope makeSectionFieldScope(const Language language, const SemanticSection& section)
{
    return makeSectionFieldScope(language, sectionParts(section));
}

NamingScope makeSectionConstantScope(const Language         language,
                                     const SemanticSection& section,
                                     const llvm::StringRef  typeConstantPrefix)
{
    return makeSectionConstantScope(language, sectionParts(section), typeConstantPrefix);
}

NamingScope makeGoConstantScope(const SemanticSection& section, const llvm::StringRef typeName)
{
    return makeGoConstantScope(sectionParts(section), typeName);
}

}  // namespace llvmdsdl
