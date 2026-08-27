//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Resolves optional helper symbol names from lowered section facts.
///
/// These helpers guard against absent lowering metadata and provide stable symbol lookup behaviour for emitters.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/CodeGen/HelperSymbolResolver.h"

#include "llvmdsdl/CodeGen/MlirLoweredFacts.h"
#include "llvmdsdl/CodeGen/SectionHelperBindingPlan.h"

namespace llvmdsdl
{
struct SemanticFieldType;

std::string resolveSectionCapacityCheckHelperSymbol(const LoweredSectionFacts* const sectionFacts)
{
    if (sectionFacts == nullptr)
    {
        return {};
    }
    return sectionFacts->capacityCheckHelper;
}

std::string resolveSectionUnionTagValidateHelperSymbol(const LoweredSectionFacts* const sectionFacts)
{
    if (sectionFacts == nullptr)
    {
        return {};
    }
    return sectionFacts->unionTagValidateHelper;
}

std::string resolveSectionUnionTagMaskHelperSymbol(const LoweredSectionFacts* const sectionFacts,
                                                   const HelperBindingDirection     direction)
{
    if (sectionFacts == nullptr)
    {
        return {};
    }
    if (direction == HelperBindingDirection::Serialize)
    {
        return sectionFacts->serUnionTagHelper;
    }
    return sectionFacts->deserUnionTagHelper;
}

std::string resolveScalarHelperSymbol(const SemanticFieldType&       type,
                                      const LoweredFieldFacts* const fieldFacts,
                                      const HelperBindingDirection   direction)
{
    const auto descriptor =
        buildScalarHelperDescriptor(type,
                                    ScalarHelperSymbols{fieldFacts ? fieldFacts->serUnsignedHelper : std::string{},
                                                        fieldFacts ? fieldFacts->deserUnsignedHelper : std::string{},
                                                        fieldFacts ? fieldFacts->serSignedHelper : std::string{},
                                                        fieldFacts ? fieldFacts->deserSignedHelper : std::string{},
                                                        fieldFacts ? fieldFacts->serFloatHelper : std::string{},
                                                        fieldFacts ? fieldFacts->deserFloatHelper : std::string{}});
    if (!descriptor)
    {
        return {};
    }
    return (direction == HelperBindingDirection::Serialize) ? descriptor->serSymbol : descriptor->deserSymbol;
}

std::optional<ArrayLengthHelperDescriptor> resolveArrayLengthHelperDescriptor(
    const SemanticFieldType&           type,
    const LoweredFieldFacts* const     fieldFacts,
    const std::optional<std::uint32_t> prefixBitsOverride,
    const HelperBindingDirection       direction)
{
    return buildArrayLengthHelperDescriptor(type,
                                            prefixBitsOverride,
                                            arrayLengthPrefixHelper(direction, fieldFacts),
                                            fieldFacts ? fieldFacts->arrayLengthValidateHelper : std::string{});
}

std::string resolveDelimiterValidateHelperSymbol(const SemanticFieldType&       type,
                                                 const LoweredFieldFacts* const fieldFacts)
{
    const auto descriptor =
        buildDelimiterValidateHelperDescriptor(type, fieldFacts ? fieldFacts->delimiterValidateHelper : std::string{});
    if (!descriptor)
    {
        return {};
    }
    return descriptor->symbol;
}

}  // namespace llvmdsdl
