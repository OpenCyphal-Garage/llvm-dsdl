//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Implements shared native-emitter helper-contract validation.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/CodeGen/NativeHelperContract.h"

#include <unordered_set>

#include "llvmdsdl/CodeGen/HelperSymbolResolver.h"
#include "llvmdsdl/CodeGen/MlirLoweredFacts.h"
#include "llvmdsdl/CodeGen/SectionHelperBindingPlan.h"
#include "llvmdsdl/CodeGen/TypeStorage.h"
#include "llvmdsdl/Semantics/Model.h"

namespace llvmdsdl
{

namespace
{

template <typename Descriptor>
std::unordered_set<std::string> collectSymbols(const std::vector<Descriptor>& descriptors)
{
    std::unordered_set<std::string> out;
    out.reserve(descriptors.size());
    for (const auto& descriptor : descriptors)
    {
        out.insert(descriptor.symbol);
    }
    return out;
}

bool failContract(std::string* const missingRequirement, const std::string& requirement)
{
    if (missingRequirement != nullptr)
    {
        *missingRequirement = requirement;
    }
    return false;
}

}  // namespace

bool validateNativeSectionHelperContract(const SemanticSection&           section,
                                         const LoweredSectionFacts* const sectionFacts,
                                         const HelperBindingDirection     direction,
                                         const SectionHelperBindingPlan&  helperBindings,
                                         std::string* const               missingRequirement)
{
    const auto scalarSymbols        = collectSymbols(helperBindings.scalarBindings);
    const auto arrayPrefixSymbols   = collectSymbols(helperBindings.arrayPrefixBindings);
    const auto arrayValidateSymbols = collectSymbols(helperBindings.arrayValidateBindings);
    const auto delimiterSymbols     = collectSymbols(helperBindings.delimiterValidateBindings);

    if (!helperBindings.capacityCheck)
    {
        return failContract(missingRequirement, "capacity-check");
    }
    if (!section.isUnion)
    {
        // Continue validating field-level helper requirements.
    }
    else if (!helperBindings.unionTagValidate)
    {
        return failContract(missingRequirement, "union-tag-validate");
    }
    else if (!helperBindings.unionTagMask)
    {
        return failContract(missingRequirement, "union-tag-mask");
    }

    for (const auto& field : section.fields)
    {
        if (field.isPadding)
        {
            continue;
        }

        const auto* const fieldFacts = findLoweredFieldFacts(sectionFacts, field.name);

        const auto scalarSymbol = resolveScalarHelperSymbol(field.resolvedType, fieldFacts, direction);
        switch (field.resolvedType.scalarCategory)
        {
        case SemanticScalarCategory::Byte:
        case SemanticScalarCategory::Utf8:
        case SemanticScalarCategory::UnsignedInt:
        case SemanticScalarCategory::SignedInt:
        case SemanticScalarCategory::Float:
            if (scalarSymbol.empty())
            {
                return failContract(missingRequirement, "scalar-helper:" + field.name);
            }
            if (!scalarSymbols.contains(scalarSymbol))
            {
                return failContract(missingRequirement, "scalar-binding:" + field.name + ":" + scalarSymbol);
            }
            break;
        default:
            break;
        }

        if (isVariableArray(field.resolvedType.arrayKind))
        {
            const auto descriptor =
                resolveArrayLengthHelperDescriptor(field.resolvedType,
                                                   fieldFacts,
                                                   loweredFieldArrayPrefixBits(sectionFacts, field.name),
                                                   direction);
            if (!descriptor)
            {
                return failContract(missingRequirement, "array-helper:" + field.name);
            }
            if (descriptor->prefixSymbol.empty())
            {
                return failContract(missingRequirement, "array-prefix-helper:" + field.name);
            }
            if (descriptor->validateSymbol.empty())
            {
                return failContract(missingRequirement, "array-validate-helper:" + field.name);
            }
            if (!arrayPrefixSymbols.contains(descriptor->prefixSymbol))
            {
                return failContract(missingRequirement,
                                    "array-prefix-binding:" + field.name + ":" + descriptor->prefixSymbol);
            }
            if (!arrayValidateSymbols.contains(descriptor->validateSymbol))
            {
                return failContract(missingRequirement,
                                    "array-validate-binding:" + field.name + ":" + descriptor->validateSymbol);
            }
        }

        if (field.resolvedType.scalarCategory == SemanticScalarCategory::Composite &&
            !field.resolvedType.compositeSealed)
        {
            const auto delimiterSymbol = resolveDelimiterValidateHelperSymbol(field.resolvedType, fieldFacts);
            if (delimiterSymbol.empty())
            {
                return failContract(missingRequirement, "delimiter-helper:" + field.name);
            }
            if (!delimiterSymbols.contains(delimiterSymbol))
            {
                return failContract(missingRequirement, "delimiter-binding:" + field.name + ":" + delimiterSymbol);
            }
        }
    }

    return true;
}

}  // namespace llvmdsdl
