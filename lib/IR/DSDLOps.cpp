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

#include <optional>
#include <set>
#include <string>
#include <utility>
#include <llvm/ADT/StringMap.h>
#include <llvm/ADT/StringRef.h>
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

/// @brief The section an op belongs to; empty for a message, which has only one.
llvm::StringRef sectionOf(const std::optional<llvm::StringRef> section)
{
    return section.value_or(llvm::StringRef{});
}

/// @brief Checks one section's union option indices against its plan.
///
/// The tag value that selects an option is stamped twice -- on the `dsdl.field` that names it and on
/// the `dsdl.io` step that serialises it -- because the name and the wire layout live in two
/// different halves of the schema and each half needs the pair. They are produced by the same walk
/// over one section, so a disagreement here is a lowering bug rather than malformed input, which is
/// the reason to check it: a backend that reads one half and a serialiser that reads the other would
/// otherwise disagree silently about which option a tag means.
///
/// @param[in] plan The section's plan.
/// @param[in] fields The section's fields, in declaration order.
/// @param[in] emitError Sink for the diagnostic.
/// @return Success when the section's options agree.
LogicalResult verifyUnionOptionIndices(SerializationPlanOp                                 plan,
                                       llvm::ArrayRef<FieldOp>                             fields,
                                       const llvm::function_ref<InFlightDiagnostic(Twine)> emitError)
{
    if (!plan.getIsUnion())
    {
        for (FieldOp field : fields)
        {
            if (field.getUnionOptionIndex())
            {
                return emitError("'union_option_index' on a field of a section that is not a union");
            }
        }
        return success();
    }

    std::int64_t expected = 0;
    for (FieldOp field : fields)
    {
        const auto optionIndex = field.getUnionOptionIndex();
        if (!optionIndex)
        {
            return emitError("union option '" + field.getName() + "' requires a 'union_option_index'");
        }
        if (*optionIndex != expected)
        {
            return emitError("union option '" + field.getName() + "' has index " + Twine(*optionIndex) +
                             ", expected " + Twine(expected) + " from its declaration order");
        }
        ++expected;
    }

    if (const auto optionCount = plan.getUnionOptionCount())
    {
        if (*optionCount != expected)
        {
            return emitError("'union_option_count' is " + Twine(*optionCount) + ", but the section declares " +
                             Twine(expected) + " options");
        }
    }

    // The plan's own steps carry the same index. Pairing is positional: a union has one field step
    // per option, in option order.
    std::size_t stepPosition = 0;
    for (Operation& step : plan.getBody().front())
    {
        auto io = llvm::dyn_cast<IOOp>(step);
        if (!io || (io.getKind() != "field"))
        {
            continue;
        }
        if (stepPosition >= fields.size())
        {
            return emitError("the plan has more option steps than the section declares options");
        }
        FieldOp            option   = fields[stepPosition];
        const std::int64_t declared = *option.getUnionOptionIndex();
        if (io.getUnionOptionIndex() != declared)
        {
            return emitError("option '" + option.getName() + "' is index " + Twine(declared) +
                             " in schema space and " + Twine(io.getUnionOptionIndex()) + " in its plan step");
        }
        ++stepPosition;
    }
    if (stepPosition != fields.size())
    {
        return emitError("the section declares " + Twine(fields.size()) + " options, but the plan has " +
                         Twine(stepPosition) + " option steps");
    }
    return success();
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

    // Fields and plans are siblings in the schema body, one group per section. A message has a
    // single unnamed section; a service has `request` and `response`.
    llvm::StringMap<llvm::SmallVector<FieldOp, 8>> fieldsBySection;
    for (Operation& op : (*this)->getRegion(0).front())
    {
        if (auto field = llvm::dyn_cast<FieldOp>(op); field && !field.getPadding())
        {
            fieldsBySection[sectionOf(field.getSection())].push_back(field);
        }
    }
    for (Operation& op : (*this)->getRegion(0).front())
    {
        auto plan = llvm::dyn_cast<SerializationPlanOp>(op);
        if (!plan || plan.getBody().empty())
        {
            continue;
        }
        const llvm::StringRef section = sectionOf(plan.getSection());
        if (failed(verifyUnionOptionIndices(plan,
                                            fieldsBySection.lookup(section),
                                            [&](const Twine message) { return emitOpError(message); })))
        {
            return failure();
        }
    }
    return success();
}

LogicalResult SerializationPlanOp::verify()
{
    if (getBody().empty())
    {
        return emitOpError("must contain a non-empty body region");
    }

    const std::int64_t minBits = getMinBits();
    const std::int64_t maxBits = getMaxBits();
    if (minBits < 0 || maxBits < 0 || maxBits < minBits)
    {
        return emitOpError("invalid min_bits/max_bits plan metadata");
    }

    if (getLowered())
    {
        const auto loweredContractVersion = (*this)->getAttrOfType<IntegerAttr>("llvmdsdl.lowered_contract_version");
        if (!loweredContractVersion ||
            !llvmdsdl::isSupportedLoweredSerDesContractVersion(loweredContractVersion.getInt()))
        {
            return emitOpError("lowered plan requires supported llvmdsdl.lowered_contract_version");
        }
        const auto loweredContractProducer = (*this)->getAttrOfType<StringAttr>("llvmdsdl.lowered_contract_producer");
        if (!loweredContractProducer || loweredContractProducer.getValue() != llvmdsdl::kLoweredSerDesContractProducer)
        {
            return emitOpError("lowered plan requires llvmdsdl.lowered_contract_producer=" +
                               std::string(llvmdsdl::kLoweredSerDesContractProducer));
        }

        // All six are read before any failure check so that a plan missing several of them reports
        // every missing attribute in one pass.
        auto require = [&](const std::optional<std::int64_t> value,
                           const llvm::StringRef             name) -> FailureOr<std::int64_t> {
            if (!value)
            {
                emitOpError("missing required '" + name.str() + "' plan attribute");
                return failure();
            }
            if (*value < 0)
            {
                emitOpError("invalid '" + name.str() + "' plan metadata");
                return failure();
            }
            return *value;
        };
        const auto lMinBits      = require(getLoweredMinBits(), getLoweredMinBitsAttrName());
        const auto lMaxBits      = require(getLoweredMaxBits(), getLoweredMaxBitsAttrName());
        const auto lStepCount    = require(getLoweredStepCount(), getLoweredStepCountAttrName());
        const auto lFieldCount   = require(getLoweredFieldCount(), getLoweredFieldCountAttrName());
        const auto lPaddingCount = require(getLoweredPaddingCount(), getLoweredPaddingCountAttrName());
        const auto lAlignCount   = require(getLoweredAlignCount(), getLoweredAlignCountAttrName());
        if (failed(lMinBits) || failed(lMaxBits) || failed(lStepCount) || failed(lFieldCount) ||
            failed(lPaddingCount) || failed(lAlignCount))
        {
            return failure();
        }
        if (*lMaxBits < *lMinBits)
        {
            return emitOpError("invalid lowered_min_bits/lowered_max_bits plan metadata");
        }
        if (*lMinBits != minBits || *lMaxBits != maxBits)
        {
            return emitOpError("lowered_min_bits/lowered_max_bits must match min_bits/max_bits");
        }
    }

    return success();
}

LogicalResult SerializationPlanOp::verifyRegions()
{
    const bool loweredPlan = getLowered();
    // Present when the plan is lowered: verify() has already required them.
    const std::int64_t loweredStepCount    = getLoweredStepCount().value_or(0);
    const std::int64_t loweredFieldCount   = getLoweredFieldCount().value_or(0);
    const std::int64_t loweredPaddingCount = getLoweredPaddingCount().value_or(0);
    const std::int64_t loweredAlignCount   = getLoweredAlignCount().value_or(0);

    std::set<std::int64_t> unionOptionIndexes;
    std::set<std::int64_t> seenStepIndexes;
    std::int64_t           observedStepCount    = 0;
    std::int64_t           observedFieldCount   = 0;
    std::int64_t           observedPaddingCount = 0;
    std::int64_t           observedAlignCount   = 0;
    auto                   recordStepIndex      = [&](Operation& op, const std::optional<std::int64_t> stepIndex) {
        if (!stepIndex)
        {
            return op.emitError("missing required 'step_index' attribute in lowered plan");
        }
        if (*stepIndex < 0)
        {
            return op.emitError("invalid negative step_index in lowered plan");
        }
        if (!seenStepIndexes.insert(*stepIndex).second)
        {
            return op.emitError("duplicate step_index in lowered plan");
        }
        return InFlightDiagnostic();
    };
    for (Operation& op : getBody().front())
    {
        if (auto align = dyn_cast<AlignOp>(op))
        {
            ++observedStepCount;
            ++observedAlignCount;
            if (loweredPlan)
            {
                if (failed(recordStepIndex(op, align.getStepIndex())))
                {
                    return failure();
                }
                if (align.getBits() <= 1)
                {
                    return op.emitError("lowered plan cannot contain no-op alignment");
                }
            }
            continue;
        }
        if (auto io = dyn_cast<IOOp>(op))
        {
            ++observedStepCount;
            if (io.isPadding())
            {
                ++observedPaddingCount;
            }
            else
            {
                ++observedFieldCount;
                if (getIsUnion())
                {
                    unionOptionIndexes.insert(io.getUnionOptionIndex());
                }
            }

            if (loweredPlan)
            {
                if (failed(recordStepIndex(op, io.getStepIndex())))
                {
                    return failure();
                }
                const auto loweredBits = io.getLoweredBits();
                if (!loweredBits)
                {
                    return op.emitError("missing required lowered_bits step metadata in lowered plan");
                }
                if (*loweredBits < 0 || *loweredBits != io.getMaxBits())
                {
                    return op.emitError("invalid min_bits/max_bits/lowered_bits step metadata in lowered plan");
                }
            }
            continue;
        }
        return op.emitError("unsupported operation in serialisation plan body");
    }

    if (getIsUnion())
    {
        const auto unionTagBits     = getUnionTagBits();
        const auto unionOptionCount = getUnionOptionCount();
        if (!unionTagBits || !unionOptionCount)
        {
            return emitOpError("union plan missing union_tag_bits/union_option_count metadata");
        }
        if (*unionTagBits <= 0 || *unionTagBits > 64)
        {
            return emitOpError("union plan has invalid union_tag_bits");
        }
        if (unionOptionIndexes.empty())
        {
            return emitOpError("union plan has no selectable options");
        }
        if (*unionOptionCount <= 0)
        {
            return emitOpError("union plan has invalid union_option_count");
        }
        if (loweredPlan && std::cmp_not_equal(*unionOptionCount, unionOptionIndexes.size()))
        {
            return emitOpError("lowered union_option_count does not match selectable options");
        }
    }

    if (loweredPlan)
    {
        if (observedStepCount != loweredStepCount || observedFieldCount != loweredFieldCount ||
            observedPaddingCount != loweredPaddingCount || observedAlignCount != loweredAlignCount)
        {
            return emitOpError("lowered step counters do not match serialisation plan body");
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
    if (const auto optionIndex = getUnionOptionIndex())
    {
        if (*optionIndex < 0)
        {
            return emitOpError("invalid 'union_option_index'");
        }
        if (getPadding())
        {
            return emitOpError("a padding field is not a union option and carries no 'union_option_index'");
        }
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
    const auto kind = getKind();
    if (kind != "field" && kind != "padding")
    {
        return emitOpError("unsupported 'kind' value");
    }
    const auto scalarCategory = getScalarCategory();
    if (!isSupportedScalarCategory(scalarCategory))
    {
        return emitOpError("unsupported 'scalar_category' value");
    }
    if (!isSupportedCastMode(getCastMode()))
    {
        return emitOpError("unsupported 'cast_mode' value");
    }
    if (!isSupportedArrayKind(getArrayKind()))
    {
        return emitOpError("unsupported 'array_kind' value");
    }
    if ((kind == "padding") != (scalarCategory == "void"))
    {
        return emitOpError("a padding step is a void category, and a void category is a padding step");
    }

    const std::int64_t minBits = getMinBits();
    const std::int64_t maxBits = getMaxBits();
    if (minBits < 0 || maxBits < 0 || maxBits < minBits)
    {
        return emitOpError("invalid min_bits/max_bits metadata");
    }

    const std::int64_t bitLength             = getBitLength();
    const std::int64_t arrayCapacity         = getArrayCapacity();
    const std::int64_t arrayLengthPrefixBits = getArrayLengthPrefixBits();
    if (bitLength < 0 || arrayCapacity < 0 || arrayLengthPrefixBits < 0)
    {
        return emitOpError("invalid bit_length/array_capacity/array_length_prefix_bits metadata");
    }

    // Defense-in-depth for the Cyphal Specification primitive bit-length ranges.
    // The frontend already rejects out-of-range widths, but enforcing it here
    // guarantees no downstream pass or hand-authored IR can smuggle a
    // non-conformant scalar (e.g. int1, uint100, float8) into codegen. Signed
    // and unsigned integers have different minimums per the spec: signed [2, 64],
    // unsigned [1, 64].
    if (scalarCategory == "signed")
    {
        if (bitLength < 2 || bitLength > 64)
        {
            return emitOpError("invalid scalar bit_length metadata; signed integer widths must be in [2, 64]");
        }
    }
    else if (scalarCategory == "unsigned")
    {
        if (bitLength < 1 || bitLength > 64)
        {
            return emitOpError("invalid scalar bit_length metadata; unsigned integer widths must be in [1, 64]");
        }
    }
    else if (scalarCategory == "float")
    {
        if (bitLength != 16 && bitLength != 32 && bitLength != 64)
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
        if (bitLength > 64)
        {
            return emitOpError("invalid scalar bit_length metadata; void widths must be in [0, 64]");
        }
    }
    if (getAlignmentBits() <= 0)
    {
        return emitOpError("invalid alignment_bits metadata");
    }
    if (getUnionOptionIndex() < 0)
    {
        return emitOpError("invalid union_option_index metadata");
    }
    if (getUnionTagBits() < 0 || getUnionTagBits() > 64)
    {
        return emitOpError("invalid union_tag_bits metadata");
    }

    if (kind == "field" && isVariableArrayKind(getArrayKind()))
    {
        if (arrayLengthPrefixBits <= 0)
        {
            return emitOpError("variable array field requires positive prefix width");
        }
        if (arrayLengthPrefixBits > 64)
        {
            return emitOpError("variable array field prefix width exceeds 64 bits");
        }
    }

    const bool namesComposite = getCompositeCTypeNameAttr() || getCompositeFullNameAttr() || getCompositeMajor() ||
                                getCompositeMinor() || getCompositeSealed() || getCompositeExtentBits();
    if (scalarCategory == "composite")
    {
        if (!getCompositeCTypeNameAttr() || !getCompositeFullNameAttr() || !getCompositeMajor() ||
            !getCompositeMinor() || !getCompositeSealed() || !getCompositeExtentBits())
        {
            return emitOpError("a composite step carries composite_c_type_name, composite_full_name, "
                               "composite_major, composite_minor, composite_sealed and composite_extent_bits");
        }
    }
    else if (namesComposite)
    {
        return emitOpError("only a composite step carries composite_* attributes");
    }

    return success();
}

#define GET_OP_CLASSES
#include "llvmdsdl/IR/DSDLOps.cpp.inc"  // IWYU pragma: keep
