//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

#include <iostream>
#include <string>
#include <optional>
#include <utility>

#include "llvmdsdl/CodeGen/TypeStorage.h"
#include "llvmdsdl/Frontend/AST.h"
#include "llvmdsdl/Semantics/Model.h"

#include "UnitTests.h"

bool runTypeStorageTests()
{
    if (llvmdsdl::scalarStorageBits(1) != 8 || llvmdsdl::scalarStorageBits(8) != 8 ||
        llvmdsdl::scalarStorageBits(9) != 16 || llvmdsdl::scalarStorageBits(16) != 16 ||
        llvmdsdl::scalarStorageBits(17) != 32 || llvmdsdl::scalarStorageBits(33) != 64)
    {
        std::cerr << "scalar storage bit-width resolution mismatch\n";
        return false;
    }
    if (std::string(llvmdsdl::scalarWidthSuffix(1)) != "8" || std::string(llvmdsdl::scalarWidthSuffix(8)) != "8" ||
        std::string(llvmdsdl::scalarWidthSuffix(9)) != "16" || std::string(llvmdsdl::scalarWidthSuffix(16)) != "16" ||
        std::string(llvmdsdl::scalarWidthSuffix(17)) != "32" || std::string(llvmdsdl::scalarWidthSuffix(64)) != "64")
    {
        std::cerr << "scalar width suffix resolution mismatch\n";
        return false;
    }

    if (llvmdsdl::isVariableArray(llvmdsdl::ArrayKind::None) || llvmdsdl::isVariableArray(llvmdsdl::ArrayKind::Fixed) ||
        !llvmdsdl::isVariableArray(llvmdsdl::ArrayKind::VariableInclusive) ||
        !llvmdsdl::isVariableArray(llvmdsdl::ArrayKind::VariableExclusive))
    {
        std::cerr << "array variability classification mismatch\n";
        return false;
    }

    {
        llvmdsdl::SemanticFieldType arrayType;
        arrayType.scalarCategory        = llvmdsdl::SemanticScalarCategory::SignedInt;
        arrayType.bitLength             = 7;
        arrayType.arrayKind             = llvmdsdl::ArrayKind::VariableInclusive;
        arrayType.arrayCapacity         = 12;
        arrayType.arrayLengthPrefixBits = 8;
        arrayType.castMode              = llvmdsdl::CastMode::Truncated;

        const auto elementType = llvmdsdl::arrayElementType(arrayType);
        if (elementType.arrayKind != llvmdsdl::ArrayKind::None || elementType.arrayCapacity != 0 ||
            elementType.arrayLengthPrefixBits != 0 || elementType.scalarCategory != arrayType.scalarCategory ||
            elementType.bitLength != arrayType.bitLength || elementType.castMode != arrayType.castMode)
        {
            std::cerr << "array element type normalization mismatch\n";
            return false;
        }
    }

    {
        const auto max7 = llvmdsdl::resolveUnsignedSaturationMax(7);
        if (!max7 || *max7 != 127U)
        {
            std::cerr << "unsigned saturation max(7) mismatch\n";
            return false;
        }
        const auto max0 = llvmdsdl::resolveUnsignedSaturationMax(0);
        if (!max0 || *max0 != 0U)
        {
            std::cerr << "unsigned saturation max(0) mismatch\n";
            return false;
        }
        if (llvmdsdl::resolveUnsignedSaturationMax(64).has_value())
        {
            std::cerr << "unsigned saturation max(64) should be unbounded\n";
            return false;
        }
    }

    {
        const auto range8 = llvmdsdl::resolveSignedSaturationRange(8);
        if (!range8 || range8->first != -128 || range8->second != 127)
        {
            std::cerr << "signed saturation range(8) mismatch\n";
            return false;
        }
        const auto range1 = llvmdsdl::resolveSignedSaturationRange(1);
        if (!range1 || range1->first != -1 || range1->second != 0)
        {
            std::cerr << "signed saturation range(1) mismatch\n";
            return false;
        }
        if (llvmdsdl::resolveSignedSaturationRange(0).has_value() ||
            llvmdsdl::resolveSignedSaturationRange(64).has_value())
        {
            std::cerr << "signed saturation range should be unbounded/invalid at 0 and 64\n";
            return false;
        }
    }

    return true;
}
