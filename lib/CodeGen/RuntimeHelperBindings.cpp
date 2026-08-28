//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Shared helper-binding lookup utilities for scripted runtime emitters.
///
/// The implementation maps lowered helper symbols into emitter-local helper
/// names and centralizes per-field helper descriptor resolution.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/CodeGen/RuntimeHelperBindings.h"

#include "llvmdsdl/CodeGen/HelperSymbolResolver.h"
#include "llvmdsdl/CodeGen/MlirLoweredFacts.h"
#include "llvmdsdl/CodeGen/RuntimeLoweredPlan.h"
#include "llvmdsdl/CodeGen/SectionHelperBindingPlan.h"
#include "llvmdsdl/Semantics/Model.h"
#include <cstdint>
#include <optional>
#include <string>

namespace llvmdsdl
{
namespace
{

const SemanticField* findSemanticFieldByName(const SemanticSection& section, const std::string& fieldName)
{
    for (const auto& field : section.fields)
    {
        if (field.name == fieldName)
        {
            return &field;
        }
    }
    return nullptr;
}

}  // namespace

std::optional<std::uint32_t> runtimeArrayPrefixOverride(const RuntimeFieldPlan& field)
{
    if (field.arrayKind != RuntimeArrayKind::Variable || field.arrayLengthPrefixBits <= 0)
    {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(field.arrayLengthPrefixBits);
}

RuntimeSectionHelperNames resolveRuntimeSectionHelperNames(const LoweredSectionFacts*       sectionFacts,
                                                           const RuntimeHelperNameResolver& helperNameResolver)
{
    RuntimeSectionHelperNames out;
    if (const auto symbol = resolveSectionCapacityCheckHelperSymbol(sectionFacts); !symbol.empty())
    {
        out.capacityCheck = helperNameResolver(symbol);
    }
    if (const auto symbol = resolveSectionUnionTagValidateHelperSymbol(sectionFacts); !symbol.empty())
    {
        out.unionTagValidate = helperNameResolver(symbol);
    }
    if (const auto symbol = resolveSectionUnionTagMaskHelperSymbol(sectionFacts, HelperBindingDirection::Serialize);
        !symbol.empty())
    {
        out.serUnionTagMask = helperNameResolver(symbol);
    }
    if (const auto symbol = resolveSectionUnionTagMaskHelperSymbol(sectionFacts, HelperBindingDirection::Deserialize);
        !symbol.empty())
    {
        out.deserUnionTagMask = helperNameResolver(symbol);
    }
    return out;
}

RuntimeFieldHelperNames resolveRuntimeFieldHelperNames(const SemanticSection&             section,
                                                       const LoweredSectionFacts*         sectionFacts,
                                                       const RuntimeFieldPlan&            field,
                                                       const std::optional<std::uint32_t> prefixBitsOverride,
                                                       const RuntimeHelperNameResolver&   helperNameResolver)
{
    RuntimeFieldHelperNames out;
    const auto* const       semanticField = findSemanticFieldByName(section, field.semanticFieldName);
    if (semanticField == nullptr)
    {
        return out;
    }
    const auto* const fieldFacts = findLoweredFieldFacts(sectionFacts, semanticField->name);

    if (semanticField->resolvedType.scalarCategory == SemanticScalarCategory::UnsignedInt ||
        semanticField->resolvedType.scalarCategory == SemanticScalarCategory::SignedInt ||
        semanticField->resolvedType.scalarCategory == SemanticScalarCategory::Float ||
        semanticField->resolvedType.scalarCategory == SemanticScalarCategory::Byte ||
        semanticField->resolvedType.scalarCategory == SemanticScalarCategory::Utf8)
    {
        const auto serScalarSymbol =
            resolveScalarHelperSymbol(semanticField->resolvedType, fieldFacts, HelperBindingDirection::Serialize);
        const auto deserScalarSymbol =
            resolveScalarHelperSymbol(semanticField->resolvedType, fieldFacts, HelperBindingDirection::Deserialize);
        if (!serScalarSymbol.empty())
        {
            out.serScalar = helperNameResolver(serScalarSymbol);
        }
        if (!deserScalarSymbol.empty())
        {
            out.deserScalar = helperNameResolver(deserScalarSymbol);
        }
    }

    const auto serArrayDescriptor = resolveArrayLengthHelperDescriptor(semanticField->resolvedType,
                                                                       fieldFacts,
                                                                       prefixBitsOverride,
                                                                       HelperBindingDirection::Serialize);
    if (serArrayDescriptor)
    {
        if (!serArrayDescriptor->prefixSymbol.empty())
        {
            out.serArrayPrefix = helperNameResolver(serArrayDescriptor->prefixSymbol);
        }
        if (!serArrayDescriptor->validateSymbol.empty())
        {
            out.arrayValidate = helperNameResolver(serArrayDescriptor->validateSymbol);
        }
    }

    const auto deserArrayDescriptor = resolveArrayLengthHelperDescriptor(semanticField->resolvedType,
                                                                         fieldFacts,
                                                                         prefixBitsOverride,
                                                                         HelperBindingDirection::Deserialize);
    if (deserArrayDescriptor)
    {
        if (!deserArrayDescriptor->prefixSymbol.empty())
        {
            out.deserArrayPrefix = helperNameResolver(deserArrayDescriptor->prefixSymbol);
        }
        if (out.arrayValidate.empty() && !deserArrayDescriptor->validateSymbol.empty())
        {
            out.arrayValidate = helperNameResolver(deserArrayDescriptor->validateSymbol);
        }
    }

    const auto delimiterSymbol = resolveDelimiterValidateHelperSymbol(semanticField->resolvedType, fieldFacts);
    if (!delimiterSymbol.empty())
    {
        out.delimiterValidate = helperNameResolver(delimiterSymbol);
    }
    return out;
}

}  // namespace llvmdsdl
