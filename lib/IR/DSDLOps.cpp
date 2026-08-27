//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Implements verification and operation glue for DSDL MLIR ops.
///
/// Operation-specific semantic checks are defined here alongside generated operation class inclusions.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/IR/DSDLOps.h"

#include <algorithm>
#include <set>
#include <string>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/LogicalResult.h>
#include <cstdint>

#include "llvmdsdl/Transforms/LoweredSerDesContract.h"
#include "mlir/IR/Builders.h"           // IWYU pragma: keep
#include "mlir/IR/BuiltinAttributes.h"  // IWYU pragma: keep
#include "mlir/IR/Diagnostics.h"        // IWYU pragma: keep
#include "mlir/Support/LLVM.h"

using namespace mlir;
using namespace mlir::dsdl;

namespace
{

bool isSupportedScalarCategory(llvm::StringRef category)
{
    return category == "bool" || category == "byte" || category == "utf8" || category == "unsigned" ||
           category == "signed" || category == "float" || category == "void" || category == "composite";
}

bool isSupportedCastMode(llvm::StringRef castMode)
{
    return castMode == "saturated" || castMode == "truncated";
}

bool isVariableArrayKind(llvm::StringRef arrayKind)
{
    return arrayKind == "variable_inclusive" || arrayKind == "variable_exclusive";
}

bool isSupportedArrayKind(llvm::StringRef arrayKind)
{
    return arrayKind == "none" || arrayKind == "fixed" || isVariableArrayKind(arrayKind);
}

}  // namespace

LogicalResult SchemaOp::verify()
{
    if (!getSealed() && !getExtentBitsAttr())
    {
        return emitOpError("requires either sealed or extent");
    }
    if ((*this)->getNumRegions() == 0 || (*this)->getRegion(0).empty())
    {
        return emitOpError("must contain a non-empty body region");
    }
    return success();
}

LogicalResult SerializationPlanOp::verify()
{
    if ((*this)->getNumRegions() == 0 || (*this)->getRegion(0).empty())
    {
        return emitOpError("must contain a non-empty body region");
    }

    const auto minBitsAttr = (*this)->getAttrOfType<IntegerAttr>("min_bits");
    const auto maxBitsAttr = (*this)->getAttrOfType<IntegerAttr>("max_bits");
    if (!minBitsAttr || !maxBitsAttr)
    {
        return emitOpError("missing required min_bits/max_bits plan metadata");
    }
    if (minBitsAttr.getInt() < 0 || maxBitsAttr.getInt() < 0 || maxBitsAttr.getInt() < minBitsAttr.getInt())
    {
        return emitOpError("invalid min_bits/max_bits plan metadata");
    }

    const bool loweredPlan        = (*this)->hasAttr("lowered");
    auto       requirePlanIntAttr = [&](llvm::StringRef attrName) -> FailureOr<std::int64_t> {
        const auto attr = (*this)->getAttrOfType<IntegerAttr>(attrName);
        if (!attr)
        {
            emitOpError("missing required '" + attrName.str() + "' plan attribute");
            return failure();
        }
        return attr.getInt();
    };
    auto requireNonNegativePlanIntAttr = [&](llvm::StringRef attrName) -> FailureOr<std::int64_t> {
        const auto value = requirePlanIntAttr(attrName);
        if (failed(value))
        {
            return failure();
        }
        if (*value < 0)
        {
            emitOpError("invalid '" + attrName.str() + "' plan metadata");
            return failure();
        }
        return value;
    };

    // Assigned only on the success path under `loweredPlan`, and read again further down. Plain
    // integers rather than FailureOr: an empty optional carried out of that branch leaves the later
    // dereferences unreachable rather than ill-formed, which GCC reports as maybe-uninitialized.
    std::int64_t loweredStepCount    = 0;
    std::int64_t loweredFieldCount   = 0;
    std::int64_t loweredPaddingCount = 0;
    std::int64_t loweredAlignCount   = 0;
    if (loweredPlan)
    {
        const auto loweredContractVersion = (*this)->getAttrOfType<IntegerAttr>("llvmdsdl.lowered_contract_version");
        if (!loweredContractVersion ||
            !llvmdsdl::isSupportedLoweredSerDesContractVersion(loweredContractVersion.getInt()))
        {
            return emitOpError("lowered plan requires supported llvmdsdl.lowered_contract_version");
        }
        const auto loweredContractProducer = (*this)->getAttrOfType<StringAttr>("llvmdsdl.lowered_contract_producer");
        if (!loweredContractProducer ||
            loweredContractProducer.getValue() != llvmdsdl::kLoweredSerDesContractProducer)
        {
            return emitOpError("lowered plan requires llvmdsdl.lowered_contract_producer=" +
                               std::string(llvmdsdl::kLoweredSerDesContractProducer));
        }

        // All six are read before any failure check so that a plan missing several of them reports
        // every missing attribute in one pass.
        const auto minBits      = requireNonNegativePlanIntAttr("lowered_min_bits");
        const auto maxBits      = requireNonNegativePlanIntAttr("lowered_max_bits");
        const auto stepCount    = requireNonNegativePlanIntAttr("lowered_step_count");
        const auto fieldCount   = requireNonNegativePlanIntAttr("lowered_field_count");
        const auto paddingCount = requireNonNegativePlanIntAttr("lowered_padding_count");
        const auto alignCount   = requireNonNegativePlanIntAttr("lowered_align_count");
        if (failed(minBits) || failed(maxBits) || failed(stepCount) || failed(fieldCount) || failed(paddingCount) ||
            failed(alignCount))
        {
            return failure();
        }
        if (*maxBits < *minBits)
        {
            return emitOpError("invalid lowered_min_bits/lowered_max_bits plan metadata");
        }
        if (*minBits != minBitsAttr.getInt() || *maxBits != maxBitsAttr.getInt())
        {
            return emitOpError("lowered_min_bits/lowered_max_bits must match min_bits/max_bits");
        }
        loweredStepCount    = *stepCount;
        loweredFieldCount   = *fieldCount;
        loweredPaddingCount = *paddingCount;
        loweredAlignCount   = *alignCount;
    }

    std::set<std::int64_t> unionOptionIndexes;
    std::set<std::int64_t> seenStepIndexes;
    std::int64_t           observedStepCount    = 0;
    std::int64_t           observedFieldCount   = 0;
    std::int64_t           observedPaddingCount = 0;
    std::int64_t           observedAlignCount   = 0;
    for (Operation& op : (*this)->getRegion(0).front())
    {
        const auto opName = op.getName().getStringRef();
        if (opName == "dsdl.align")
        {
            ++observedStepCount;
            ++observedAlignCount;
            if (loweredPlan)
            {
                const auto stepIndex = op.getAttrOfType<IntegerAttr>("step_index");
                if (!stepIndex)
                {
                    return op.emitError("missing required 'step_index' attribute in lowered plan");
                }
                if (stepIndex.getInt() < 0)
                {
                    return op.emitError("invalid negative step_index in lowered plan");
                }
                if (!seenStepIndexes.insert(stepIndex.getInt()).second)
                {
                    return op.emitError("duplicate step_index in lowered plan");
                }
                const auto bits = op.getAttrOfType<IntegerAttr>("bits");
                if (!bits || bits.getInt() <= 1)
                {
                    return op.emitError("lowered plan cannot contain no-op alignment");
                }
            }
            continue;
        }
        if (opName == "dsdl.io")
        {
            ++observedStepCount;
            if ((*this)->hasAttr("is_union"))
            {
                const auto kindAttr = op.getAttrOfType<StringAttr>("kind");
                const auto kind     = kindAttr ? kindAttr.getValue() : llvm::StringRef("field");
                if (kind != "padding")
                {
                    const auto optionAttr = op.getAttrOfType<IntegerAttr>("union_option_index");
                    if (optionAttr)
                    {
                        if (optionAttr.getInt() < 0)
                        {
                            return op.emitError("invalid negative union_option_index");
                        }
                        unionOptionIndexes.insert(optionAttr.getInt());
                    }
                }
            }

            const auto kindAttr = op.getAttrOfType<StringAttr>("kind");
            const auto kind     = kindAttr ? kindAttr.getValue() : llvm::StringRef("field");
            if (kind == "padding")
            {
                ++observedPaddingCount;
            }
            else
            {
                ++observedFieldCount;
            }

            if (loweredPlan)
            {
                const auto stepIndex = op.getAttrOfType<IntegerAttr>("step_index");
                if (!stepIndex)
                {
                    return op.emitError("missing required 'step_index' attribute in lowered plan");
                }
                if (stepIndex.getInt() < 0)
                {
                    return op.emitError("invalid negative step_index in lowered plan");
                }
                if (!seenStepIndexes.insert(stepIndex.getInt()).second)
                {
                    return op.emitError("duplicate step_index in lowered plan");
                }
                const auto stepMinBits = op.getAttrOfType<IntegerAttr>("min_bits");
                const auto stepMaxBits = op.getAttrOfType<IntegerAttr>("max_bits");
                const auto loweredBits = op.getAttrOfType<IntegerAttr>("lowered_bits");
                if (!stepMinBits || !stepMaxBits || !loweredBits)
                {
                    return op.emitError(
                        "missing required min_bits/max_bits/lowered_bits step metadata in lowered plan");
                }
                if (stepMinBits.getInt() < 0 || stepMaxBits.getInt() < stepMinBits.getInt() ||
                    loweredBits.getInt() < 0 || loweredBits.getInt() != stepMaxBits.getInt())
                {
                    return op.emitError("invalid min_bits/max_bits/lowered_bits step metadata in lowered plan");
                }
            }
            continue;
        }
        return op.emitError("unsupported operation in serialization plan body");
    }

    if ((*this)->hasAttr("is_union"))
    {
        const auto unionTagBitsAttr     = (*this)->getAttrOfType<IntegerAttr>("union_tag_bits");
        const auto unionOptionCountAttr = (*this)->getAttrOfType<IntegerAttr>("union_option_count");
        if (!unionTagBitsAttr || !unionOptionCountAttr)
        {
            return emitOpError("union plan missing union_tag_bits/union_option_count metadata");
        }
        const auto unionTagBits = std::max<std::int64_t>(unionTagBitsAttr.getInt(), 0);
        if (unionTagBits <= 0 || unionTagBits > 64)
        {
            return emitOpError("union plan has invalid union_tag_bits");
        }
        if (unionOptionIndexes.empty())
        {
            return emitOpError("union plan has no selectable options");
        }
        if (unionOptionCountAttr.getInt() <= 0)
        {
            return emitOpError("union plan has invalid union_option_count");
        }
        if (loweredPlan)
        {
            if (unionOptionCountAttr.getInt() != static_cast<std::int64_t>(unionOptionIndexes.size()))
            {
                return emitOpError("lowered union_option_count does not match selectable options");
            }
        }
    }

    if (loweredPlan)
    {
        if (observedStepCount != loweredStepCount || observedFieldCount != loweredFieldCount ||
            observedPaddingCount != loweredPaddingCount || observedAlignCount != loweredAlignCount)
        {
            return emitOpError("lowered step counters do not match serialization plan body");
        }
        for (const auto stepIndex : seenStepIndexes)
        {
            if (stepIndex >= loweredStepCount)
            {
                return emitOpError("step_index out of lowered_step_count bounds");
            }
        }
    }

    return success();
}

LogicalResult AlignOp::verify()
{
    if (getBits() <= 0)
    {
        return emitOpError("requires positive 'bits' value");
    }
    return success();
}

LogicalResult FieldOp::verify()
{
    // Padding fields (e.g. void8) are anonymous by construction; only named fields
    // must carry a name.
    if (!getPadding() && getName().empty())
    {
        return emitOpError("requires a non-empty 'name' for non-padding fields");
    }
    if (getTypeName().empty())
    {
        return emitOpError("requires a non-empty 'type_name'");
    }
    return success();
}

LogicalResult ConstantOp::verify()
{
    if (getName().empty())
    {
        return emitOpError("requires a non-empty 'name'");
    }
    if (getTypeName().empty())
    {
        return emitOpError("requires a non-empty 'type_name'");
    }
    if (getValueText().empty())
    {
        return emitOpError("requires a non-empty 'value_text'");
    }
    return success();
}

LogicalResult IOOp::verify()
{
    const auto kindAttr = (*this)->getAttrOfType<StringAttr>("kind");
    if (!kindAttr)
    {
        return emitOpError("missing required 'kind' attribute");
    }
    const auto kind = kindAttr.getValue();
    if (kind != "field" && kind != "padding")
    {
        return emitOpError("unsupported 'kind' value");
    }
    const auto nameAttr = (*this)->getAttrOfType<StringAttr>("name");
    if (!nameAttr)
    {
        return emitOpError("missing required 'name' attribute");
    }
    const auto typeNameAttr = (*this)->getAttrOfType<StringAttr>("type_name");
    if (!typeNameAttr)
    {
        return emitOpError("missing required 'type_name' attribute");
    }

    const auto scalarCategoryAttr = (*this)->getAttrOfType<StringAttr>("scalar_category");
    if (!scalarCategoryAttr)
    {
        return emitOpError("missing required 'scalar_category' attribute");
    }
    if (!isSupportedScalarCategory(scalarCategoryAttr.getValue()))
    {
        return emitOpError("unsupported 'scalar_category' value");
    }

    const auto castModeAttr = (*this)->getAttrOfType<StringAttr>("cast_mode");
    if (!castModeAttr)
    {
        return emitOpError("missing required 'cast_mode' attribute");
    }
    if (!isSupportedCastMode(castModeAttr.getValue()))
    {
        return emitOpError("unsupported 'cast_mode' value");
    }

    const auto arrayKindAttr = (*this)->getAttrOfType<StringAttr>("array_kind");
    if (!arrayKindAttr)
    {
        return emitOpError("missing required 'array_kind' attribute");
    }
    if (!isSupportedArrayKind(arrayKindAttr.getValue()))
    {
        return emitOpError("unsupported 'array_kind' value");
    }

    auto requireIntAttr = [&](llvm::StringRef attrName) -> FailureOr<std::int64_t> {
        const auto attr = (*this)->getAttrOfType<IntegerAttr>(attrName);
        if (!attr)
        {
            emitOpError("missing required '" + attrName.str() + "' attribute");
            return failure();
        }
        return attr.getInt();
    };

    const auto minBits = requireIntAttr("min_bits");
    if (failed(minBits))
    {
        return failure();
    }
    const auto maxBits = requireIntAttr("max_bits");
    if (failed(maxBits))
    {
        return failure();
    }
    if (*minBits < 0 || *maxBits < 0 || *maxBits < *minBits)
    {
        return emitOpError("invalid min_bits/max_bits metadata");
    }

    const auto bitLength = requireIntAttr("bit_length");
    if (failed(bitLength))
    {
        return failure();
    }
    const auto arrayCapacity = requireIntAttr("array_capacity");
    if (failed(arrayCapacity))
    {
        return failure();
    }
    const auto arrayLengthPrefixBits = requireIntAttr("array_length_prefix_bits");
    if (failed(arrayLengthPrefixBits))
    {
        return failure();
    }
    const auto alignmentBits = requireIntAttr("alignment_bits");
    if (failed(alignmentBits))
    {
        return failure();
    }
    const auto unionOptionIndex = requireIntAttr("union_option_index");
    if (failed(unionOptionIndex))
    {
        return failure();
    }
    const auto unionTagBits = requireIntAttr("union_tag_bits");
    if (failed(unionTagBits))
    {
        return failure();
    }
    if (*bitLength < 0 || *arrayCapacity < 0 || *arrayLengthPrefixBits < 0)
    {
        return emitOpError("invalid bit_length/array_capacity/array_length_prefix_bits metadata");
    }

    // Defense-in-depth for the Cyphal Specification primitive bit-length ranges.
    // The frontend already rejects out-of-range widths, but enforcing it here
    // guarantees no downstream pass or hand-authored IR can smuggle a
    // non-conformant scalar (e.g. int1, uint100, float8) into codegen. Signed
    // and unsigned integers have different minimums per the spec: signed [2, 64],
    // unsigned [1, 64].
    const auto scalarCategory = scalarCategoryAttr.getValue();
    if (scalarCategory == "signed")
    {
        if (*bitLength < 2 || *bitLength > 64)
        {
            return emitOpError("invalid scalar bit_length metadata; signed integer widths must be in [2, 64]");
        }
    }
    else if (scalarCategory == "unsigned")
    {
        if (*bitLength < 1 || *bitLength > 64)
        {
            return emitOpError("invalid scalar bit_length metadata; unsigned integer widths must be in [1, 64]");
        }
    }
    else if (scalarCategory == "float")
    {
        if (*bitLength != 16 && *bitLength != 32 && *bitLength != 64)
        {
            return emitOpError("invalid scalar bit_length metadata; floating-point width must be 16, 32, or 64");
        }
    }
    else if (scalarCategory == "void")
    {
        // Void categories model alignment/spacer padding. The frontend enforces
        // the spec range [1, 64] for user-written `voidN` fields; the lowering
        // pipeline may additionally synthesize a degenerate 0-bit padding that is
        // dropped downstream, so 0 is admitted here.
        if (*bitLength < 0 || *bitLength > 64)
        {
            return emitOpError("invalid scalar bit_length metadata; void widths must be in [0, 64]");
        }
    }
    if (*alignmentBits <= 0)
    {
        return emitOpError("invalid alignment_bits metadata");
    }
    if (*unionOptionIndex < 0)
    {
        return emitOpError("invalid union_option_index metadata");
    }
    if (*unionTagBits < 0 || *unionTagBits > 64)
    {
        return emitOpError("invalid union_tag_bits metadata");
    }

    if (kind == "field" && isVariableArrayKind(arrayKindAttr.getValue()))
    {
        if (*arrayLengthPrefixBits <= 0)
        {
            return emitOpError("variable array field requires positive prefix width");
        }
        if (*arrayLengthPrefixBits > 64)
        {
            return emitOpError("variable array field prefix width exceeds 64 bits");
        }
    }

    return success();
}

#define GET_OP_CLASSES
#include "llvmdsdl/IR/DSDLOps.cpp.inc"  // IWYU pragma: keep
