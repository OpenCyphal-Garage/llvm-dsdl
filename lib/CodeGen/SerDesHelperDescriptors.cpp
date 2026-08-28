//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Builds shared helper descriptor bundles for serdes generation.
///
/// These routines compute optional capacity and union-tag helper requirements from section structure and lowering data.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/CodeGen/SerDesHelperDescriptors.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <utility>

#include "llvmdsdl/Frontend/AST.h"
#include "llvmdsdl/Semantics/Model.h"

namespace llvmdsdl
{

SharedSerDesHelperDescriptors buildSharedSerDesHelperDescriptors(const SemanticSection& section,
                                                                 const std::string&     capacityCheckSymbol,
                                                                 const std::string&     unionTagValidateSymbol)
{
    SharedSerDesHelperDescriptors out;

    if (!capacityCheckSymbol.empty())
    {
        CapacityCheckHelperDescriptor descriptor;
        descriptor.symbol       = capacityCheckSymbol;
        descriptor.requiredBits = std::max<std::int64_t>(0, section.serializationBufferSizeBits);
        out.capacityCheck       = std::move(descriptor);
    }

    if (section.isUnion && !unionTagValidateSymbol.empty())
    {
        UnionTagValidateHelperDescriptor descriptor;
        descriptor.symbol = unionTagValidateSymbol;
        std::set<std::int64_t> tags;
        for (const auto& field : section.fields)
        {
            if (field.isPadding)
            {
                continue;
            }
            tags.insert(static_cast<std::int64_t>(field.unionOptionIndex));
        }
        descriptor.allowedTags.assign(tags.begin(), tags.end());
        out.unionTagValidate = std::move(descriptor);
    }

    return out;
}

std::optional<ArrayLengthHelperDescriptor> buildArrayLengthHelperDescriptor(
    const SemanticFieldType&     type,
    std::optional<std::uint32_t> prefixBitsOverride,
    const std::string&           prefixSymbol,
    const std::string&           validateSymbol)
{
    const auto kind          = type.arrayKind;
    const bool variableArray = (kind == ArrayKind::VariableInclusive) || (kind == ArrayKind::VariableExclusive);
    if (!variableArray)
    {
        return std::nullopt;
    }

    ArrayLengthHelperDescriptor out;
    out.prefixSymbol   = prefixSymbol;
    out.validateSymbol = validateSymbol;
    out.prefixBits =
        prefixBitsOverride.value_or(static_cast<std::uint32_t>(std::max<std::int64_t>(0, type.arrayLengthPrefixBits)));
    out.capacity = std::max<std::int64_t>(0, type.arrayCapacity);
    return out;
}

std::optional<ArrayLengthHelperDescriptor> buildArrayLengthHelperDescriptor(
    const SemanticField&         field,
    std::optional<std::uint32_t> prefixBitsOverride,
    const std::string&           prefixSymbol,
    const std::string&           validateSymbol)
{
    if (field.isPadding)
    {
        return std::nullopt;
    }
    return buildArrayLengthHelperDescriptor(field.resolvedType, prefixBitsOverride, prefixSymbol, validateSymbol);
}

std::optional<ScalarHelperDescriptor> buildScalarHelperDescriptor(const SemanticFieldType&   type,
                                                                  const ScalarHelperSymbols& symbols)
{
    ScalarHelperDescriptor out;
    out.bitLength = static_cast<std::uint32_t>(std::max<std::int64_t>(0, type.bitLength));
    out.castMode  = type.castMode;

    switch (type.scalarCategory)
    {
    case SemanticScalarCategory::UnsignedInt:
    case SemanticScalarCategory::Byte:
    case SemanticScalarCategory::Utf8:
        out.kind        = ScalarHelperKind::Unsigned;
        out.serSymbol   = symbols.serUnsignedSymbol;
        out.deserSymbol = symbols.deserUnsignedSymbol;
        return out;
    case SemanticScalarCategory::SignedInt:
        out.kind        = ScalarHelperKind::Signed;
        out.serSymbol   = symbols.serSignedSymbol;
        out.deserSymbol = symbols.deserSignedSymbol;
        return out;
    case SemanticScalarCategory::Float:
        out.kind        = ScalarHelperKind::Float;
        out.serSymbol   = symbols.serFloatSymbol;
        out.deserSymbol = symbols.deserFloatSymbol;
        return out;
    default:
        break;
    }
    return std::nullopt;
}

std::optional<ScalarHelperDescriptor> buildScalarHelperDescriptor(const SemanticField&       field,
                                                                  const ScalarHelperSymbols& symbols)
{
    if (field.isPadding)
    {
        return std::nullopt;
    }
    return buildScalarHelperDescriptor(field.resolvedType, symbols);
}

std::optional<DelimiterValidateHelperDescriptor> buildDelimiterValidateHelperDescriptor(const SemanticFieldType& type,
                                                                                        const std::string&       symbol)
{
    if (symbol.empty())
    {
        return std::nullopt;
    }
    if (type.scalarCategory != SemanticScalarCategory::Composite || type.compositeSealed)
    {
        return std::nullopt;
    }
    DelimiterValidateHelperDescriptor out;
    out.symbol = symbol;
    return out;
}

std::optional<DelimiterValidateHelperDescriptor> buildDelimiterValidateHelperDescriptor(const SemanticField& field,
                                                                                        const std::string&   symbol)
{
    if (field.isPadding)
    {
        return std::nullopt;
    }
    return buildDelimiterValidateHelperDescriptor(field.resolvedType, symbol);
}

}  // namespace llvmdsdl
