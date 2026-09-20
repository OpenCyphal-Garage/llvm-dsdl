//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Reads a serialisation plan's steps out of its operations.
///
//===----------------------------------------------------------------------===//
#include "llvmdsdl/Transforms/PlanSteps.h"

#include "llvmdsdl/IR/DSDLOps.h"
#include "llvmdsdl/Support/DefinitionNaming.h"
#include <llvm/ADT/StringRef.h>
#include <mlir/Support/LLVM.h>
#include <mlir/IR/BuiltinAttributes.h>
#include <mlir/IR/BuiltinOps.h>
#include <mlir/IR/Operation.h>
#include <mlir/IR/Region.h>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace llvmdsdl
{
namespace
{

/// @brief The width a step contributes, as the lowering settled it.
std::int64_t ioStepBits(mlir::dsdl::IOOp io)
{
    return nonNegative(io.getLoweredBits().value_or(io.getMaxBits()));
}

std::string valueOrEmpty(const std::optional<llvm::StringRef> value)
{
    return value ? value->str() : std::string{};
}

/// @brief The one width of the composite @p io nests, when its plan in the module has one.
///
/// A type's message plan is the one without a section. Its bounds are a whole number of bytes
/// when they agree, since a composite is padded to a boundary; anything else is left as varying.
std::optional<std::int64_t> nestedFixedBits(mlir::dsdl::IOOp io)
{
    auto module = io->getParentOfType<mlir::ModuleOp>();
    if (!module || !io.isComposite() || !io.getCompositeFullName())
    {
        return std::nullopt;
    }
    auto schema = module.lookupSymbol<mlir::dsdl::SchemaOp>(
        renderDefinitionSymbolBase(*io.getCompositeFullName(),
                                   static_cast<std::uint32_t>(io.getCompositeMajor().value_or(0)),
                                   static_cast<std::uint32_t>(io.getCompositeMinor().value_or(0))));
    if (!schema || schema.getBody().empty())
    {
        return std::nullopt;
    }
    for (mlir::dsdl::SerializationPlanOp plan : schema.getBody().front().getOps<mlir::dsdl::SerializationPlanOp>())
    {
        if (plan.getSection())
        {
            continue;
        }
        const std::int64_t bits = plan.getMaxBits();
        if ((bits == plan.getMinBits()) && (bits >= 0) && ((bits % 8) == 0))
        {
            return bits;
        }
        return std::nullopt;
    }
    return std::nullopt;
}

PlanStep stepFor(mlir::dsdl::IOOp io)
{
    PlanStep step;
    step.kind                         = io.isPadding() ? PlanStepKind::Padding : PlanStepKind::Field;
    step.bits                         = ioStepBits(io);
    step.name                         = io.getName().str();
    step.scalarCategory               = io.getScalarCategory().str();
    step.castMode                     = io.getCastMode().str();
    step.arrayKind                    = io.getArrayKind().str();
    step.bitLength                    = io.getBitLength();
    step.arrayCapacity                = io.getArrayCapacity();
    step.arrayLengthPrefixBits        = io.getArrayLengthPrefixBits();
    step.alignmentBits                = io.getAlignmentBits();
    step.unionOptionIndex             = io.getUnionOptionIndex();
    step.unionTagBits                 = io.getUnionTagBits();
    step.compositeFullName            = valueOrEmpty(io.getCompositeFullName());
    step.compositeMajor               = io.getCompositeMajor().value_or(0);
    step.compositeMinor               = io.getCompositeMinor().value_or(0);
    step.serUnsignedHelper            = valueOrEmpty(io.getLoweredSerUnsignedHelper());
    step.deserUnsignedHelper          = valueOrEmpty(io.getLoweredDeserUnsignedHelper());
    step.serSignedHelper              = valueOrEmpty(io.getLoweredSerSignedHelper());
    step.deserSignedHelper            = valueOrEmpty(io.getLoweredDeserSignedHelper());
    step.serFloatHelper               = valueOrEmpty(io.getLoweredSerFloatHelper());
    step.deserFloatHelper             = valueOrEmpty(io.getLoweredDeserFloatHelper());
    step.serArrayLengthPrefixHelper   = valueOrEmpty(io.getLoweredSerArrayLengthPrefixHelper());
    step.deserArrayLengthPrefixHelper = valueOrEmpty(io.getLoweredDeserArrayLengthPrefixHelper());
    step.arrayLengthValidateHelper    = valueOrEmpty(io.getLoweredArrayLengthValidateHelper());
    step.delimiterValidateHelper      = valueOrEmpty(io.getLoweredDelimiterValidateHelper());
    step.compositeSealed              = io.getCompositeSealed().value_or(true);
    step.compositeExtentBits          = nonNegative(io.getCompositeExtentBits().value_or(0));
    step.compositeFixedBits           = nestedFixedBits(io);
    step.heldAsView                   = io.getHeldAsView();
    return step;
}

}  // namespace

std::vector<PlanStep> collectPlanSteps(mlir::dsdl::SerializationPlanOp plan)
{
    std::vector<PlanStep> steps;
    if (plan.getBody().empty())
    {
        return steps;
    }
    for (mlir::Operation& op : plan.getBody().front())
    {
        if (auto align = mlir::dyn_cast<mlir::dsdl::AlignOp>(op))
        {
            PlanStep alignStep;
            alignStep.kind = PlanStepKind::Align;
            alignStep.bits = static_cast<std::int64_t>(align.getBits());
            steps.push_back(std::move(alignStep));
        }
        else if (auto io = mlir::dyn_cast<mlir::dsdl::IOOp>(op))
        {
            steps.push_back(stepFor(io));
        }
    }
    return steps;
}

}  // namespace llvmdsdl
