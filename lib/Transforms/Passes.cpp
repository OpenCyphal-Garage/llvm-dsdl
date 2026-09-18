//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Implements and registers core DSDL transformation passes.
///
/// Pass implementations annotate and lower schema operations into a form consumable by backend code generators.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/SerDes/HelperBodyPlan.h"
#include "llvmdsdl/IR/DSDLOps.h"
#include "llvmdsdl/Transforms/Passes.h"

#include <llvm/ADT/STLExtras.h>
#include <llvm/ADT/StringMap.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/ErrorHandling.h>
#include <memory>
#include <mlir/IR/Attributes.h>
#include <mlir/IR/Block.h>
#include <mlir/IR/BuiltinAttributes.h>
#include <mlir/IR/Diagnostics.h>
#include <mlir/IR/DialectRegistry.h>
#include <mlir/IR/Location.h>
#include <mlir/IR/Operation.h>
#include <mlir/IR/OperationSupport.h>
#include <mlir/IR/Region.h>
#include <mlir/IR/TypeRange.h>
#include <mlir/IR/Types.h>
#include <mlir/IR/Value.h>
#include <mlir/Support/LLVM.h>
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <set>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include <mlir/Dialect/Arith/IR/Arith.h>
#include <mlir/Dialect/Func/IR/FuncOps.h>
#include <mlir/Dialect/SCF/IR/SCF.h>
#include <mlir/IR/Builders.h>
#include <mlir/IR/BuiltinOps.h>
#include <mlir/IR/BuiltinTypes.h>
#include <mlir/Pass/Pass.h>
#include <mlir/Pass/PassManager.h>
#include <mlir/Pass/PassRegistry.h>
#include <mlir/Transforms/Passes.h>

#include "llvmdsdl/Transforms/LoweredSerDesContract.h"
#include "llvmdsdl/Support/DefinitionNaming.h"
#include "llvmdsdl/Support/ScalarStorage.h"

namespace llvmdsdl
{

namespace
{

std::int64_t nonNegative(const std::int64_t value)
{
    return std::max<std::int64_t>(value, 0);
}

void stampLoweredContractAttributes(mlir::Operation* op, mlir::Builder& builder)
{
    op->setAttr(kLoweredSerDesContractVersionAttr, builder.getI64IntegerAttr(kLoweredSerDesContractVersion));
    op->setAttr(kLoweredSerDesContractProducerAttr, builder.getStringAttr(kLoweredSerDesContractProducer));
}

mlir::LogicalResult canonicalizePlan(mlir::dsdl::SerializationPlanOp plan, mlir::Builder& builder)
{
    auto&                         body = plan.getBody().front();
    std::vector<mlir::Operation*> eraseOps;
    std::int64_t                  stepIndex    = 0;
    std::int64_t                  alignCount   = 0;
    std::int64_t                  fieldCount   = 0;
    std::int64_t                  paddingCount = 0;
    std::set<std::int64_t>        unionOptionIndexes;

    for (mlir::Operation& op : body)
    {
        if (auto align = mlir::dyn_cast<mlir::dsdl::AlignOp>(op))
        {
            if (align.getBits() <= 1)
            {
                eraseOps.push_back(&op);
                continue;
            }
            align.setStepIndexAttr(builder.getI64IntegerAttr(stepIndex++));
            ++alignCount;
            continue;
        }

        if (auto io = mlir::dyn_cast<mlir::dsdl::IOOp>(op))
        {
            const bool         isPadding = io.isPadding();
            const std::int64_t minBits   = nonNegative(io.getMinBits());
            const std::int64_t maxBits   = std::max<std::int64_t>(nonNegative(io.getMaxBits()), minBits);

            if (isPadding && maxBits == 0)
            {
                eraseOps.push_back(&op);
                continue;
            }

            io.setMinBitsAttr(builder.getI64IntegerAttr(minBits));
            io.setMaxBitsAttr(builder.getI64IntegerAttr(maxBits));
            io.setLoweredBitsAttr(builder.getI64IntegerAttr(maxBits));
            io.setStepIndexAttr(builder.getI64IntegerAttr(stepIndex++));

            if (isPadding)
            {
                ++paddingCount;
            }
            else
            {
                unionOptionIndexes.insert(io.getUnionOptionIndex());
                ++fieldCount;
            }
            continue;
        }

        return op.emitError("unsupported operation in serialisation plan body");
    }

    for (mlir::Operation* op : eraseOps)
    {
        op->erase();
    }

    const std::int64_t minBits = nonNegative(plan.getMinBits());
    const std::int64_t maxBits = std::max<std::int64_t>(nonNegative(plan.getMaxBits()), minBits);
    plan.setMinBitsAttr(builder.getI64IntegerAttr(minBits));
    plan.setMaxBitsAttr(builder.getI64IntegerAttr(maxBits));
    plan.setLoweredMinBitsAttr(builder.getI64IntegerAttr(minBits));
    plan.setLoweredMaxBitsAttr(builder.getI64IntegerAttr(maxBits));
    plan.setLoweredStepCountAttr(builder.getI64IntegerAttr(stepIndex));
    plan.setLoweredFieldCountAttr(builder.getI64IntegerAttr(fieldCount));
    plan.setLoweredPaddingCountAttr(builder.getI64IntegerAttr(paddingCount));
    plan.setLoweredAlignCountAttr(builder.getI64IntegerAttr(alignCount));
    plan.setLowered(true);
    stampLoweredContractAttributes(plan.getOperation(), builder);

    if (plan.getIsUnion())
    {
        plan.setUnionTagBitsAttr(builder.getI64IntegerAttr(nonNegative(plan.getUnionTagBits().value_or(0))));
        plan.setUnionOptionCountAttr(builder.getI64IntegerAttr(static_cast<std::int64_t>(unionOptionIndexes.size())));
    }

    return mlir::success();
}

/// @brief Lowers one scalar helper shape into arith ops.
///
/// The shape comes from @ref llvmdsdl::helperBodyForScalar, which the language
/// emitters also call: this pass and those backends have to agree about what a
/// given field's helper does, and the way to guarantee that is for neither of them
/// to decide it.
///
/// @param[in] body The shape, as decided for this field and direction.
/// @param[in] value The helper's argument.
/// @param[in,out] builder Positioned at the helper's entry block.
/// @param[in] loc Location to attribute the emitted ops to.
/// @return The value the helper returns.
mlir::Value lowerScalarHelperBody(const llvmdsdl::HelperBody& body,
                                  mlir::Value                 value,
                                  mlir::OpBuilder&            builder,
                                  const mlir::Location        loc)
{
    const auto bits = static_cast<unsigned>(body.bits);
    switch (body.kind)
    {
    case llvmdsdl::HelperBodyKind::Identity:
        return value;

    case llvmdsdl::HelperBodyKind::Mask: {
        // helperBodyForScalar only asks for a mask below the full width; at 64 it answers
        // Identity, because the shift that would build the mask has no defined result.
        assert(bits < 64U);
        const auto mask      = static_cast<std::int64_t>((UINT64_C(1) << bits) - UINT64_C(1));
        auto       maskConst = mlir::arith::ConstantIntOp::create(builder, loc, mask, 64);
        return mlir::arith::AndIOp::create(builder, loc, value, maskConst).getResult();
    }

    case llvmdsdl::HelperBodyKind::SaturateUnsigned: {
        assert(bits < 64U);
        const auto mask      = static_cast<std::int64_t>((UINT64_C(1) << bits) - UINT64_C(1));
        auto       maskConst = mlir::arith::ConstantIntOp::create(builder, loc, mask, 64);
        auto       over = mlir::arith::CmpIOp::create(builder, loc, mlir::arith::CmpIPredicate::ugt, value, maskConst);
        return mlir::arith::SelectOp::create(builder, loc, over, maskConst, value).getResult();
    }

    case llvmdsdl::HelperBodyKind::SaturateSigned: {
        auto minConst   = mlir::arith::ConstantIntOp::create(builder, loc, body.minValue, 64);
        auto maxConst   = mlir::arith::ConstantIntOp::create(builder, loc, body.maxValue, 64);
        auto below      = mlir::arith::CmpIOp::create(builder, loc, mlir::arith::CmpIPredicate::slt, value, minConst);
        auto above      = mlir::arith::CmpIOp::create(builder, loc, mlir::arith::CmpIPredicate::sgt, value, maxConst);
        auto clampedLow = mlir::arith::SelectOp::create(builder, loc, below, minConst, value);
        return mlir::arith::SelectOp::create(builder, loc, above, maxConst, clampedLow).getResult();
    }

    case llvmdsdl::HelperBodyKind::SignExtend: {
        // Sign extension needs a sign bit to propagate and a width to propagate it into,
        // so helperBodyForScalar asks for it only strictly between those bounds.
        assert(bits > 0U && bits < 64U);
        const std::uint64_t maskU       = (UINT64_C(1) << bits) - UINT64_C(1);
        const std::uint64_t signU       = UINT64_C(1) << (bits - 1U);
        const std::uint64_t extendMaskU = ~maskU;
        auto maskConst   = mlir::arith::ConstantIntOp::create(builder, loc, static_cast<std::int64_t>(maskU), 64);
        auto signConst   = mlir::arith::ConstantIntOp::create(builder, loc, static_cast<std::int64_t>(signU), 64);
        auto extendConst = mlir::arith::ConstantIntOp::create(builder, loc, static_cast<std::int64_t>(extendMaskU), 64);
        auto zeroConst   = mlir::arith::ConstantIntOp::create(builder, loc, 0, 64);
        auto masked      = mlir::arith::AndIOp::create(builder, loc, value, maskConst).getResult();
        auto signPart    = mlir::arith::AndIOp::create(builder, loc, masked, signConst).getResult();
        auto isNegative =
            mlir::arith::CmpIOp::create(builder, loc, mlir::arith::CmpIPredicate::ne, signPart, zeroConst);
        auto negExtended = mlir::arith::OrIOp::create(builder, loc, masked, extendConst).getResult();
        return mlir::arith::SelectOp::create(builder, loc, isNegative, negExtended, masked).getResult();
    }

    case llvmdsdl::HelperBodyKind::StatusGuard:
    case llvmdsdl::HelperBodyKind::TagMembership:
        // Neither is a scalar helper: they answer a status rather than normalising a value,
        // and the C backend builds them elsewhere. Returning the argument would look like a
        // helper that does nothing rather than like the mistake it is.
        assert(false && "helper body shape is not a scalar normalisation");
        break;
    }
    return value;
}

mlir::LogicalResult createPlanCapacityCheckFunction(mlir::ModuleOp                  module,
                                                    mlir::dsdl::SerializationPlanOp plan,
                                                    mlir::OpBuilder&                builder)
{
    auto schema = plan->getParentOfType<mlir::dsdl::SchemaOp>();
    if (!schema)
    {
        return plan->emitOpError("must be nested under dsdl.schema");
    }
    const auto        schemaSym   = schema.getSymNameAttr();
    const auto        sectionAttr = plan.getSectionAttr();
    const std::string section     = sectionAttr ? sectionAttr.getValue().str() : "";
    const std::string funcName =
        "llvmdsdl_plan_capacity_check__" + schemaSym.getValue().str() + renderSectionSymbolSuffix(section);
    plan.setLoweredCapacityCheckHelperAttr(builder.getStringAttr(funcName));
    if (module.lookupSymbol<mlir::func::FuncOp>(funcName))
    {
        return mlir::success();
    }

    mlir::OpBuilder::InsertionGuard const g(builder);
    builder.setInsertionPointToEnd(&module.getBodyRegion().front());

    const mlir::Location loc    = plan->getLoc();
    auto                 i64Ty  = builder.getIntegerType(64);
    auto                 i8Ty   = builder.getIntegerType(8);
    auto                 fnType = builder.getFunctionType(mlir::TypeRange{i64Ty}, mlir::TypeRange{i8Ty});
    auto                 fn     = mlir::func::FuncOp::create(builder, loc, funcName, fnType);
    fn->setAttr("llvmdsdl.plan_capacity_check", builder.getUnitAttr());
    fn->setAttr("llvmdsdl.schema_sym", schemaSym);
    if (sectionAttr)
    {
        fn->setAttr("llvmdsdl.section", sectionAttr);
    }
    fn->setAttr("llvmdsdl.plan_origin", builder.getStringAttr(kLoweredSerDesContractProducer));

    mlir::Block* entry = fn.addEntryBlock();
    builder.setInsertionPointToStart(entry);
    mlir::Value const  capacityBits = entry->getArgument(0);
    const std::int64_t requiredBits = nonNegative(plan.getMaxBits());

    auto requiredBitsValue = mlir::arith::ConstantIntOp::create(builder, loc, requiredBits, 64).getResult();
    auto cond =
        mlir::arith::CmpIOp::create(builder, loc, mlir::arith::CmpIPredicate::ugt, requiredBitsValue, capacityBits);
    auto status = mlir::scf::IfOp::create(builder, loc, mlir::TypeRange{i8Ty}, cond, true);
    {
        mlir::OpBuilder thenBuilder = status.getThenBodyBuilder();
        auto            fail        = mlir::arith::ConstantIntOp::create(thenBuilder, loc, -3, 8).getResult();
        mlir::scf::YieldOp::create(thenBuilder, loc, fail);
    }
    {
        mlir::OpBuilder elseBuilder = status.getElseBodyBuilder();
        auto            ok          = mlir::arith::ConstantIntOp::create(elseBuilder, loc, 0, 8).getResult();
        mlir::scf::YieldOp::create(elseBuilder, loc, ok);
    }
    mlir::func::ReturnOp::create(builder, loc, status.getResults());

    return mlir::success();
}

mlir::LogicalResult createUnionTagValidationFunction(mlir::ModuleOp                  module,
                                                     mlir::dsdl::SerializationPlanOp plan,
                                                     mlir::OpBuilder&                builder)
{
    if (!plan.getIsUnion())
    {
        return mlir::success();
    }

    auto schema = plan->getParentOfType<mlir::dsdl::SchemaOp>();
    if (!schema)
    {
        return plan->emitOpError("must be nested under dsdl.schema");
    }
    const auto        schemaSym   = schema.getSymNameAttr();
    const auto        sectionAttr = plan.getSectionAttr();
    const std::string section     = sectionAttr ? sectionAttr.getValue().str() : "";
    const std::string funcName =
        "llvmdsdl_plan_validate_union_tag__" + schemaSym.getValue().str() + renderSectionSymbolSuffix(section);
    plan.setLoweredUnionTagValidateHelperAttr(builder.getStringAttr(funcName));
    if (module.lookupSymbol<mlir::func::FuncOp>(funcName))
    {
        return mlir::success();
    }

    std::set<std::int64_t> optionIndexes;
    if (!plan.getBody().empty())
    {
        for (mlir::dsdl::IOOp op : plan.getBody().front().getOps<mlir::dsdl::IOOp>())
        {
            if (op.isPadding())
            {
                continue;
            }
            optionIndexes.insert(op.getUnionOptionIndex());
        }
    }
    if (optionIndexes.empty())
    {
        return plan->emitOpError("union plan has no selectable options");
    }

    mlir::OpBuilder::InsertionGuard const g(builder);
    builder.setInsertionPointToEnd(&module.getBodyRegion().front());

    const mlir::Location loc    = plan->getLoc();
    auto                 i64Ty  = builder.getIntegerType(64);
    auto                 i8Ty   = builder.getIntegerType(8);
    auto                 fnType = builder.getFunctionType(mlir::TypeRange{i64Ty}, mlir::TypeRange{i8Ty});
    auto                 fn     = mlir::func::FuncOp::create(builder, loc, funcName, fnType);
    fn->setAttr("llvmdsdl.union_tag_validate", builder.getUnitAttr());
    fn->setAttr("llvmdsdl.schema_sym", schemaSym);
    if (sectionAttr)
    {
        fn->setAttr("llvmdsdl.section", sectionAttr);
    }
    fn->setAttr("llvmdsdl.plan_origin", builder.getStringAttr(kLoweredSerDesContractProducer));

    mlir::Block* entry = fn.addEntryBlock();
    builder.setInsertionPointToStart(entry);
    mlir::Value const tagValue = entry->getArgument(0);
    mlir::Value       anyMatch = mlir::arith::ConstantIntOp::create(builder, loc, 0, 1).getResult();
    for (const std::int64_t option : optionIndexes)
    {
        auto optConst = mlir::arith::ConstantIntOp::create(builder, loc, option, 64).getResult();
        auto match    = mlir::arith::CmpIOp::create(builder, loc, mlir::arith::CmpIPredicate::eq, tagValue, optConst);
        anyMatch      = mlir::arith::OrIOp::create(builder, loc, anyMatch, match);
    }

    auto status = mlir::scf::IfOp::create(builder, loc, mlir::TypeRange{i8Ty}, anyMatch, true);
    {
        mlir::OpBuilder thenBuilder = status.getThenBodyBuilder();
        auto            ok          = mlir::arith::ConstantIntOp::create(thenBuilder, loc, 0, 8).getResult();
        mlir::scf::YieldOp::create(thenBuilder, loc, ok);
    }
    {
        mlir::OpBuilder elseBuilder = status.getElseBodyBuilder();
        auto            fail        = mlir::arith::ConstantIntOp::create(elseBuilder, loc, -11, 8).getResult();
        mlir::scf::YieldOp::create(elseBuilder, loc, fail);
    }
    mlir::func::ReturnOp::create(builder, loc, status.getResults());

    return mlir::success();
}

mlir::LogicalResult createScalarUnsignedFieldHelpers(mlir::ModuleOp                  module,
                                                     mlir::dsdl::SerializationPlanOp plan,
                                                     mlir::OpBuilder&                builder)
{
    auto schema = plan->getParentOfType<mlir::dsdl::SchemaOp>();
    if (!schema)
    {
        return plan->emitOpError("must be nested under dsdl.schema");
    }
    const auto        schemaSym   = schema.getSymNameAttr();
    const auto        sectionAttr = plan.getSectionAttr();
    const std::string section     = sectionAttr ? sectionAttr.getValue().str() : "";

    if (plan.getBody().empty())
    {
        return mlir::success();
    }

    for (mlir::dsdl::IOOp op : plan.getBody().front().getOps<mlir::dsdl::IOOp>())
    {
        if (op.isPadding())
        {
            continue;
        }
        const auto scalar = op.getScalarCategory();
        if (scalar != "unsigned" && scalar != "byte" && scalar != "utf8")
        {
            continue;
        }
        const std::int64_t bitLength = op.getBitLength();
        if (bitLength <= 0 || bitLength > 64)
        {
            continue;
        }
        if (!op.getStepIndex())
        {
            return op.emitOpError("carries no step_index; canonicalise the plan first");
        }
        const std::int64_t stepIndex = *op.getStepIndex();
        const auto         castMode  = op.getCastMode();

        const std::string symbolStem = "llvmdsdl_plan_scalar_unsigned__" + schemaSym.getValue().str() +
                                       renderSectionSymbolSuffix(section) + "__" + std::to_string(stepIndex);
        const std::string serName    = symbolStem + "__ser";
        const std::string deserName  = symbolStem + "__deser";
        op.setLoweredSerUnsignedHelperAttr(builder.getStringAttr(serName));
        op.setLoweredDeserUnsignedHelperAttr(builder.getStringAttr(deserName));

        const auto serShape   = llvmdsdl::helperBodyForScalar(llvmdsdl::HelperScalarKind::Unsigned,
                                                              static_cast<std::uint32_t>(bitLength),
                                                              castMode == "saturated",
                                                              llvmdsdl::HelperDirection::Serialize);
        const auto deserShape = llvmdsdl::helperBodyForScalar(llvmdsdl::HelperScalarKind::Unsigned,
                                                              static_cast<std::uint32_t>(bitLength),
                                                              castMode == "saturated",
                                                              llvmdsdl::HelperDirection::Deserialize);

        if (!module.lookupSymbol<mlir::func::FuncOp>(serName))
        {
            mlir::OpBuilder::InsertionGuard const g(builder);
            builder.setInsertionPointToEnd(&module.getBodyRegion().front());
            const mlir::Location loc    = op.getLoc();
            auto                 i64Ty  = builder.getIntegerType(64);
            auto                 fnType = builder.getFunctionType(mlir::TypeRange{i64Ty}, mlir::TypeRange{i64Ty});
            auto                 fn     = mlir::func::FuncOp::create(builder, loc, serName, fnType);
            fn->setAttr("llvmdsdl.scalar_unsigned_helper", builder.getUnitAttr());
            fn->setAttr("llvmdsdl.scalar_unsigned_helper_kind", builder.getStringAttr("serialize"));
            fn->setAttr("llvmdsdl.schema_sym", schemaSym);
            if (sectionAttr)
            {
                fn->setAttr("llvmdsdl.section", sectionAttr);
            }
            auto* entry = fn.addEntryBlock();
            builder.setInsertionPointToStart(entry);
            auto       value  = entry->getArgument(0);
            const auto result = lowerScalarHelperBody(serShape, value, builder, loc);
            mlir::func::ReturnOp::create(builder, loc, result);
        }

        if (!module.lookupSymbol<mlir::func::FuncOp>(deserName))
        {
            mlir::OpBuilder::InsertionGuard const g(builder);
            builder.setInsertionPointToEnd(&module.getBodyRegion().front());
            const mlir::Location loc    = op.getLoc();
            auto                 i64Ty  = builder.getIntegerType(64);
            auto                 fnType = builder.getFunctionType(mlir::TypeRange{i64Ty}, mlir::TypeRange{i64Ty});
            auto                 fn     = mlir::func::FuncOp::create(builder, loc, deserName, fnType);
            fn->setAttr("llvmdsdl.scalar_unsigned_helper", builder.getUnitAttr());
            fn->setAttr("llvmdsdl.scalar_unsigned_helper_kind", builder.getStringAttr("deserialize"));
            fn->setAttr("llvmdsdl.schema_sym", schemaSym);
            if (sectionAttr)
            {
                fn->setAttr("llvmdsdl.section", sectionAttr);
            }
            auto* entry = fn.addEntryBlock();
            builder.setInsertionPointToStart(entry);
            auto       value  = entry->getArgument(0);
            const auto result = lowerScalarHelperBody(deserShape, value, builder, loc);
            mlir::func::ReturnOp::create(builder, loc, result);
        }
    }

    return mlir::success();
}

mlir::LogicalResult createScalarSignedFieldHelpers(mlir::ModuleOp                  module,
                                                   mlir::dsdl::SerializationPlanOp plan,
                                                   mlir::OpBuilder&                builder)
{
    auto schema = plan->getParentOfType<mlir::dsdl::SchemaOp>();
    if (!schema)
    {
        return plan->emitOpError("must be nested under dsdl.schema");
    }
    const auto        schemaSym   = schema.getSymNameAttr();
    const auto        sectionAttr = plan.getSectionAttr();
    const std::string section     = sectionAttr ? sectionAttr.getValue().str() : "";

    if (plan.getBody().empty())
    {
        return mlir::success();
    }

    for (mlir::dsdl::IOOp op : plan.getBody().front().getOps<mlir::dsdl::IOOp>())
    {
        if (op.isPadding())
        {
            continue;
        }
        const auto scalar = op.getScalarCategory();
        if (scalar != "signed")
        {
            continue;
        }
        const std::int64_t bitLength = op.getBitLength();
        if (bitLength <= 0 || bitLength > 64)
        {
            continue;
        }
        if (!op.getStepIndex())
        {
            return op.emitOpError("carries no step_index; canonicalise the plan first");
        }
        const std::int64_t stepIndex = *op.getStepIndex();
        const auto         castMode  = op.getCastMode();

        const std::string symbolStem = "llvmdsdl_plan_scalar_signed__" + schemaSym.getValue().str() +
                                       renderSectionSymbolSuffix(section) + "__" + std::to_string(stepIndex);
        const std::string serName    = symbolStem + "__ser";
        const std::string deserName  = symbolStem + "__deser";
        op.setLoweredSerSignedHelperAttr(builder.getStringAttr(serName));
        op.setLoweredDeserSignedHelperAttr(builder.getStringAttr(deserName));

        const auto serShape   = llvmdsdl::helperBodyForScalar(llvmdsdl::HelperScalarKind::Signed,
                                                              static_cast<std::uint32_t>(bitLength),
                                                              castMode == "saturated",
                                                              llvmdsdl::HelperDirection::Serialize);
        const auto deserShape = llvmdsdl::helperBodyForScalar(llvmdsdl::HelperScalarKind::Signed,
                                                              static_cast<std::uint32_t>(bitLength),
                                                              castMode == "saturated",
                                                              llvmdsdl::HelperDirection::Deserialize);

        if (!module.lookupSymbol<mlir::func::FuncOp>(serName))
        {
            mlir::OpBuilder::InsertionGuard const g(builder);
            builder.setInsertionPointToEnd(&module.getBodyRegion().front());
            const mlir::Location loc    = op.getLoc();
            auto                 i64Ty  = builder.getIntegerType(64);
            auto                 fnType = builder.getFunctionType(mlir::TypeRange{i64Ty}, mlir::TypeRange{i64Ty});
            auto                 fn     = mlir::func::FuncOp::create(builder, loc, serName, fnType);
            fn->setAttr("llvmdsdl.scalar_signed_helper", builder.getUnitAttr());
            fn->setAttr("llvmdsdl.scalar_signed_helper_kind", builder.getStringAttr("serialize"));
            fn->setAttr("llvmdsdl.schema_sym", schemaSym);
            if (sectionAttr)
            {
                fn->setAttr("llvmdsdl.section", sectionAttr);
            }
            auto* entry = fn.addEntryBlock();
            builder.setInsertionPointToStart(entry);
            auto       value  = entry->getArgument(0);
            const auto result = lowerScalarHelperBody(serShape, value, builder, loc);
            mlir::func::ReturnOp::create(builder, loc, result);
        }

        if (!module.lookupSymbol<mlir::func::FuncOp>(deserName))
        {
            mlir::OpBuilder::InsertionGuard const g(builder);
            builder.setInsertionPointToEnd(&module.getBodyRegion().front());
            const mlir::Location loc    = op.getLoc();
            auto                 i64Ty  = builder.getIntegerType(64);
            auto                 fnType = builder.getFunctionType(mlir::TypeRange{i64Ty}, mlir::TypeRange{i64Ty});
            auto                 fn     = mlir::func::FuncOp::create(builder, loc, deserName, fnType);
            fn->setAttr("llvmdsdl.scalar_signed_helper", builder.getUnitAttr());
            fn->setAttr("llvmdsdl.scalar_signed_helper_kind", builder.getStringAttr("deserialize"));
            fn->setAttr("llvmdsdl.schema_sym", schemaSym);
            if (sectionAttr)
            {
                fn->setAttr("llvmdsdl.section", sectionAttr);
            }
            auto* entry = fn.addEntryBlock();
            builder.setInsertionPointToStart(entry);
            auto       value  = entry->getArgument(0);
            const auto result = lowerScalarHelperBody(deserShape, value, builder, loc);
            mlir::func::ReturnOp::create(builder, loc, result);
        }
    }

    return mlir::success();
}

mlir::LogicalResult createScalarFloatFieldHelpers(mlir::ModuleOp                  module,
                                                  mlir::dsdl::SerializationPlanOp plan,
                                                  mlir::OpBuilder&                builder)
{
    auto schema = plan->getParentOfType<mlir::dsdl::SchemaOp>();
    if (!schema)
    {
        return plan->emitOpError("must be nested under dsdl.schema");
    }
    const auto        schemaSym   = schema.getSymNameAttr();
    const auto        sectionAttr = plan.getSectionAttr();
    const std::string section     = sectionAttr ? sectionAttr.getValue().str() : "";

    if (plan.getBody().empty())
    {
        return mlir::success();
    }

    for (mlir::dsdl::IOOp op : plan.getBody().front().getOps<mlir::dsdl::IOOp>())
    {
        if (op.isPadding())
        {
            continue;
        }
        const auto scalar = op.getScalarCategory();
        if (scalar != "float")
        {
            continue;
        }
        const std::int64_t bitLength = op.getBitLength();
        if (bitLength != 16 && bitLength != 32 && bitLength != 64)
        {
            continue;
        }
        if (!op.getStepIndex())
        {
            return op.emitOpError("carries no step_index; canonicalise the plan first");
        }
        const std::int64_t stepIndex  = *op.getStepIndex();
        const std::string  symbolStem = "llvmdsdl_plan_scalar_float__" + schemaSym.getValue().str() +
                                        renderSectionSymbolSuffix(section) + "__" + std::to_string(stepIndex);
        const std::string  serName    = symbolStem + "__ser";
        const std::string  deserName  = symbolStem + "__deser";
        op.setLoweredSerFloatHelperAttr(builder.getStringAttr(serName));
        op.setLoweredDeserFloatHelperAttr(builder.getStringAttr(deserName));

        // Width-match the helper to the field's native storage type: 16/32-bit
        // fields are held as `float` (f32), 64-bit as `double` (f64). Typing the
        // helper as f64 for every width forced callers to promote a float32
        // value to double and narrow it back, which canonicalises a signalling
        // NaN's mantissa payload and diverges bit-for-bit from the reference
        // compiler. The helper is an identity pass-through, so matching the width
        // preserves the exact bits.
        // NOLINTNEXTLINE(cppcoreguidelines-slicing) -- mlir::Type is a value handle, so nothing is sliced.
        auto floatTy = (bitLength == 64) ? mlir::Type(builder.getF64Type()) : mlir::Type(builder.getF32Type());

        if (!module.lookupSymbol<mlir::func::FuncOp>(serName))
        {
            mlir::OpBuilder::InsertionGuard const g(builder);
            builder.setInsertionPointToEnd(&module.getBodyRegion().front());
            const mlir::Location loc    = op.getLoc();
            auto                 fnType = builder.getFunctionType(mlir::TypeRange{floatTy}, mlir::TypeRange{floatTy});
            auto                 fn     = mlir::func::FuncOp::create(builder, loc, serName, fnType);
            fn->setAttr("llvmdsdl.scalar_float_helper", builder.getUnitAttr());
            fn->setAttr("llvmdsdl.scalar_float_helper_kind", builder.getStringAttr("serialize"));
            fn->setAttr("llvmdsdl.schema_sym", schemaSym);
            if (sectionAttr)
            {
                fn->setAttr("llvmdsdl.section", sectionAttr);
            }
            auto* entry = fn.addEntryBlock();
            builder.setInsertionPointToStart(entry);
            auto value = entry->getArgument(0);
            mlir::func::ReturnOp::create(builder, loc, value);
        }

        if (!module.lookupSymbol<mlir::func::FuncOp>(deserName))
        {
            mlir::OpBuilder::InsertionGuard const g(builder);
            builder.setInsertionPointToEnd(&module.getBodyRegion().front());
            const mlir::Location loc    = op.getLoc();
            auto                 fnType = builder.getFunctionType(mlir::TypeRange{floatTy}, mlir::TypeRange{floatTy});
            auto                 fn     = mlir::func::FuncOp::create(builder, loc, deserName, fnType);
            fn->setAttr("llvmdsdl.scalar_float_helper", builder.getUnitAttr());
            fn->setAttr("llvmdsdl.scalar_float_helper_kind", builder.getStringAttr("deserialize"));
            fn->setAttr("llvmdsdl.schema_sym", schemaSym);
            if (sectionAttr)
            {
                fn->setAttr("llvmdsdl.section", sectionAttr);
            }
            auto* entry = fn.addEntryBlock();
            builder.setInsertionPointToStart(entry);
            auto value = entry->getArgument(0);
            mlir::func::ReturnOp::create(builder, loc, value);
        }
    }

    return mlir::success();
}

mlir::LogicalResult createArrayLengthValidationHelpers(mlir::ModuleOp                  module,
                                                       mlir::dsdl::SerializationPlanOp plan,
                                                       mlir::OpBuilder&                builder)
{
    auto schema = plan->getParentOfType<mlir::dsdl::SchemaOp>();
    if (!schema)
    {
        return plan->emitOpError("must be nested under dsdl.schema");
    }
    const auto        schemaSym   = schema.getSymNameAttr();
    const auto        sectionAttr = plan.getSectionAttr();
    const std::string section     = sectionAttr ? sectionAttr.getValue().str() : "";

    if (plan.getBody().empty())
    {
        return mlir::success();
    }

    for (mlir::dsdl::IOOp op : plan.getBody().front().getOps<mlir::dsdl::IOOp>())
    {
        if (op.isPadding())
        {
            continue;
        }
        const bool variableArray = op.isVariableArray();
        if (!variableArray)
        {
            continue;
        }
        const std::int64_t capacity = op.getArrayCapacity();
        if (!op.getStepIndex())
        {
            return op.emitOpError("carries no step_index; canonicalise the plan first");
        }
        const std::int64_t stepIndex  = *op.getStepIndex();
        const std::string  symbolName = "llvmdsdl_plan_validate_array_length__" + schemaSym.getValue().str() +
                                        renderSectionSymbolSuffix(section) + "__" + std::to_string(stepIndex);
        op.setLoweredArrayLengthValidateHelperAttr(builder.getStringAttr(symbolName));

        if (module.lookupSymbol<mlir::func::FuncOp>(symbolName))
        {
            continue;
        }

        mlir::OpBuilder::InsertionGuard const g(builder);
        builder.setInsertionPointToEnd(&module.getBodyRegion().front());

        const mlir::Location loc    = op.getLoc();
        auto                 i64Ty  = builder.getIntegerType(64);
        auto                 i8Ty   = builder.getIntegerType(8);
        auto                 fnType = builder.getFunctionType(mlir::TypeRange{i64Ty}, mlir::TypeRange{i8Ty});
        auto                 fn     = mlir::func::FuncOp::create(builder, loc, symbolName, fnType);
        fn->setAttr("llvmdsdl.array_length_validate", builder.getUnitAttr());
        fn->setAttr("llvmdsdl.schema_sym", schemaSym);
        if (sectionAttr)
        {
            fn->setAttr("llvmdsdl.section", sectionAttr);
        }

        auto* entry = fn.addEntryBlock();
        builder.setInsertionPointToStart(entry);
        auto length     = entry->getArgument(0);
        auto zeroConst  = mlir::arith::ConstantIntOp::create(builder, loc, 0, 64);
        auto capConst   = mlir::arith::ConstantIntOp::create(builder, loc, capacity, 64);
        auto isNegative = mlir::arith::CmpIOp::create(builder, loc, mlir::arith::CmpIPredicate::slt, length, zeroConst);
        auto tooLarge   = mlir::arith::CmpIOp::create(builder, loc, mlir::arith::CmpIPredicate::sgt, length, capConst);
        // A length the target's index type cannot hold is rejected on that target.
        auto held       = mlir::dsdl::IndexHoldsOp::create(builder, loc, builder.getI1Type(), length);
        auto falseConst = mlir::arith::ConstantIntOp::create(builder, loc, 0, 1);
        auto unheld     = mlir::arith::CmpIOp::create(builder, loc, mlir::arith::CmpIPredicate::eq, held, falseConst);
        auto outOfRange = mlir::arith::OrIOp::create(builder, loc, isNegative, tooLarge);
        auto invalid    = mlir::arith::OrIOp::create(builder, loc, outOfRange, unheld);
        auto status     = mlir::scf::IfOp::create(builder, loc, mlir::TypeRange{i8Ty}, invalid, true);
        {
            mlir::OpBuilder thenBuilder = status.getThenBodyBuilder();
            auto            fail        = mlir::arith::ConstantIntOp::create(thenBuilder, loc, -10, 8).getResult();
            mlir::scf::YieldOp::create(thenBuilder, loc, fail);
        }
        {
            mlir::OpBuilder elseBuilder = status.getElseBodyBuilder();
            auto            ok          = mlir::arith::ConstantIntOp::create(elseBuilder, loc, 0, 8).getResult();
            mlir::scf::YieldOp::create(elseBuilder, loc, ok);
        }
        mlir::func::ReturnOp::create(builder, loc, status.getResults());
    }

    return mlir::success();
}

mlir::LogicalResult createArrayLengthPrefixHelpers(mlir::ModuleOp                  module,
                                                   mlir::dsdl::SerializationPlanOp plan,
                                                   mlir::OpBuilder&                builder)
{
    auto schema = plan->getParentOfType<mlir::dsdl::SchemaOp>();
    if (!schema)
    {
        return plan->emitOpError("must be nested under dsdl.schema");
    }
    const auto        schemaSym   = schema.getSymNameAttr();
    const auto        sectionAttr = plan.getSectionAttr();
    const std::string section     = sectionAttr ? sectionAttr.getValue().str() : "";

    if (plan.getBody().empty())
    {
        return mlir::success();
    }

    for (mlir::dsdl::IOOp op : plan.getBody().front().getOps<mlir::dsdl::IOOp>())
    {
        if (op.isPadding())
        {
            continue;
        }
        const bool variableArray = op.isVariableArray();
        if (!variableArray)
        {
            continue;
        }
        const std::int64_t prefixBits = op.getArrayLengthPrefixBits();
        if (prefixBits <= 0 || prefixBits > 64)
        {
            return op.emitOpError("invalid array-length prefix width");
        }
        if (!op.getStepIndex())
        {
            return op.emitOpError("carries no step_index; canonicalise the plan first");
        }
        const std::int64_t stepIndex  = *op.getStepIndex();
        const std::string  symbolStem = "llvmdsdl_plan_array_length_prefix__" + schemaSym.getValue().str() +
                                        renderSectionSymbolSuffix(section) + "__" + std::to_string(stepIndex);
        const std::string  serName    = symbolStem + "__ser";
        const std::string  deserName  = symbolStem + "__deser";
        op.setLoweredSerArrayLengthPrefixHelperAttr(builder.getStringAttr(serName));
        op.setLoweredDeserArrayLengthPrefixHelperAttr(builder.getStringAttr(deserName));

        const bool          fullWidth = (prefixBits == 64);
        const std::uint64_t mask =
            fullWidth ? UINT64_MAX : ((UINT64_C(1) << static_cast<unsigned>(prefixBits)) - UINT64_C(1));
        const auto maskSigned = fullWidth ? INT64_C(-1) : static_cast<std::int64_t>(mask);

        if (!module.lookupSymbol<mlir::func::FuncOp>(serName))
        {
            mlir::OpBuilder::InsertionGuard const g(builder);
            builder.setInsertionPointToEnd(&module.getBodyRegion().front());
            const mlir::Location loc    = op.getLoc();
            auto                 i64Ty  = builder.getIntegerType(64);
            auto                 fnType = builder.getFunctionType(mlir::TypeRange{i64Ty}, mlir::TypeRange{i64Ty});
            auto                 fn     = mlir::func::FuncOp::create(builder, loc, serName, fnType);
            fn->setAttr("llvmdsdl.array_length_prefix_helper", builder.getUnitAttr());
            fn->setAttr("llvmdsdl.array_length_prefix_helper_kind", builder.getStringAttr("serialize"));
            fn->setAttr("llvmdsdl.schema_sym", schemaSym);
            if (sectionAttr)
            {
                fn->setAttr("llvmdsdl.section", sectionAttr);
            }
            auto* entry = fn.addEntryBlock();
            builder.setInsertionPointToStart(entry);
            auto value = entry->getArgument(0);
            if (fullWidth)
            {
                mlir::func::ReturnOp::create(builder, loc, value);
            }
            else
            {
                auto maskConst = mlir::arith::ConstantIntOp::create(builder, loc, maskSigned, 64);
                auto result    = mlir::arith::AndIOp::create(builder, loc, value, maskConst).getResult();
                mlir::func::ReturnOp::create(builder, loc, result);
            }
        }

        if (!module.lookupSymbol<mlir::func::FuncOp>(deserName))
        {
            mlir::OpBuilder::InsertionGuard const g(builder);
            builder.setInsertionPointToEnd(&module.getBodyRegion().front());
            const mlir::Location loc    = op.getLoc();
            auto                 i64Ty  = builder.getIntegerType(64);
            auto                 fnType = builder.getFunctionType(mlir::TypeRange{i64Ty}, mlir::TypeRange{i64Ty});
            auto                 fn     = mlir::func::FuncOp::create(builder, loc, deserName, fnType);
            fn->setAttr("llvmdsdl.array_length_prefix_helper", builder.getUnitAttr());
            fn->setAttr("llvmdsdl.array_length_prefix_helper_kind", builder.getStringAttr("deserialize"));
            fn->setAttr("llvmdsdl.schema_sym", schemaSym);
            if (sectionAttr)
            {
                fn->setAttr("llvmdsdl.section", sectionAttr);
            }
            auto* entry = fn.addEntryBlock();
            builder.setInsertionPointToStart(entry);
            auto value = entry->getArgument(0);
            if (fullWidth)
            {
                mlir::func::ReturnOp::create(builder, loc, value);
            }
            else
            {
                auto maskConst = mlir::arith::ConstantIntOp::create(builder, loc, maskSigned, 64);
                auto result    = mlir::arith::AndIOp::create(builder, loc, value, maskConst).getResult();
                mlir::func::ReturnOp::create(builder, loc, result);
            }
        }
    }

    return mlir::success();
}

mlir::LogicalResult createUnionTagIoHelpers(mlir::ModuleOp                  module,
                                            mlir::dsdl::SerializationPlanOp plan,
                                            mlir::OpBuilder&                builder)
{
    if (!plan.getIsUnion())
    {
        return mlir::success();
    }

    auto schema = plan->getParentOfType<mlir::dsdl::SchemaOp>();
    if (!schema)
    {
        return plan->emitOpError("must be nested under dsdl.schema");
    }
    const auto         schemaSym   = schema.getSymNameAttr();
    const auto         sectionAttr = plan.getSectionAttr();
    const std::string  section     = sectionAttr ? sectionAttr.getValue().str() : "";
    const std::int64_t tagBits     = plan.getUnionTagBits().value_or(0);
    if (tagBits <= 0 || tagBits > 64)
    {
        return plan->emitOpError("invalid union tag width");
    }

    const std::string symbolStem =
        "llvmdsdl_plan_union_tag__" + schemaSym.getValue().str() + renderSectionSymbolSuffix(section);
    const std::string serName   = symbolStem + "__ser";
    const std::string deserName = symbolStem + "__deser";
    plan.setLoweredSerUnionTagHelperAttr(builder.getStringAttr(serName));
    plan.setLoweredDeserUnionTagHelperAttr(builder.getStringAttr(deserName));

    const bool          fullWidth = (tagBits == 64);
    const std::uint64_t mask = fullWidth ? UINT64_MAX : ((UINT64_C(1) << static_cast<unsigned>(tagBits)) - UINT64_C(1));
    const auto          maskSigned = fullWidth ? INT64_C(-1) : static_cast<std::int64_t>(mask);

    if (!module.lookupSymbol<mlir::func::FuncOp>(serName))
    {
        mlir::OpBuilder::InsertionGuard const g(builder);
        builder.setInsertionPointToEnd(&module.getBodyRegion().front());
        const mlir::Location loc    = plan->getLoc();
        auto                 i64Ty  = builder.getIntegerType(64);
        auto                 fnType = builder.getFunctionType(mlir::TypeRange{i64Ty}, mlir::TypeRange{i64Ty});
        auto                 fn     = mlir::func::FuncOp::create(builder, loc, serName, fnType);
        fn->setAttr("llvmdsdl.union_tag_helper", builder.getUnitAttr());
        fn->setAttr("llvmdsdl.union_tag_helper_kind", builder.getStringAttr("serialize"));
        fn->setAttr("llvmdsdl.schema_sym", schemaSym);
        if (sectionAttr)
        {
            fn->setAttr("llvmdsdl.section", sectionAttr);
        }
        auto* entry = fn.addEntryBlock();
        builder.setInsertionPointToStart(entry);
        auto value = entry->getArgument(0);
        if (fullWidth)
        {
            mlir::func::ReturnOp::create(builder, loc, value);
        }
        else
        {
            auto maskConst = mlir::arith::ConstantIntOp::create(builder, loc, maskSigned, 64);
            auto result    = mlir::arith::AndIOp::create(builder, loc, value, maskConst).getResult();
            mlir::func::ReturnOp::create(builder, loc, result);
        }
    }

    if (!module.lookupSymbol<mlir::func::FuncOp>(deserName))
    {
        mlir::OpBuilder::InsertionGuard const g(builder);
        builder.setInsertionPointToEnd(&module.getBodyRegion().front());
        const mlir::Location loc    = plan->getLoc();
        auto                 i64Ty  = builder.getIntegerType(64);
        auto                 fnType = builder.getFunctionType(mlir::TypeRange{i64Ty}, mlir::TypeRange{i64Ty});
        auto                 fn     = mlir::func::FuncOp::create(builder, loc, deserName, fnType);
        fn->setAttr("llvmdsdl.union_tag_helper", builder.getUnitAttr());
        fn->setAttr("llvmdsdl.union_tag_helper_kind", builder.getStringAttr("deserialize"));
        fn->setAttr("llvmdsdl.schema_sym", schemaSym);
        if (sectionAttr)
        {
            fn->setAttr("llvmdsdl.section", sectionAttr);
        }
        auto* entry = fn.addEntryBlock();
        builder.setInsertionPointToStart(entry);
        auto value = entry->getArgument(0);
        if (fullWidth)
        {
            mlir::func::ReturnOp::create(builder, loc, value);
        }
        else
        {
            auto maskConst = mlir::arith::ConstantIntOp::create(builder, loc, maskSigned, 64);
            auto result    = mlir::arith::AndIOp::create(builder, loc, value, maskConst).getResult();
            mlir::func::ReturnOp::create(builder, loc, result);
        }
    }

    return mlir::success();
}

mlir::LogicalResult createDelimiterHeaderValidationHelpers(mlir::ModuleOp                  module,
                                                           mlir::dsdl::SerializationPlanOp plan,
                                                           mlir::OpBuilder&                builder)
{
    auto schema = plan->getParentOfType<mlir::dsdl::SchemaOp>();
    if (!schema)
    {
        return plan->emitOpError("must be nested under dsdl.schema");
    }
    const auto        schemaSym   = schema.getSymNameAttr();
    const auto        sectionAttr = plan.getSectionAttr();
    const std::string section     = sectionAttr ? sectionAttr.getValue().str() : "";

    if (plan.getBody().empty())
    {
        return mlir::success();
    }

    for (mlir::dsdl::IOOp op : plan.getBody().front().getOps<mlir::dsdl::IOOp>())
    {
        if (op.isPadding())
        {
            continue;
        }
        const auto scalar = op.getScalarCategory();
        if (scalar != "composite")
        {
            continue;
        }
        const bool compositeSealed = op.getCompositeSealed().value_or(true);
        if (compositeSealed)
        {
            continue;
        }
        if (!op.getStepIndex())
        {
            return op.emitOpError("carries no step_index; canonicalise the plan first");
        }
        const std::int64_t stepIndex  = *op.getStepIndex();
        const std::string  symbolName = "llvmdsdl_plan_validate_delimiter_header__" + schemaSym.getValue().str() +
                                        renderSectionSymbolSuffix(section) + "__" + std::to_string(stepIndex);
        op.setLoweredDelimiterValidateHelperAttr(builder.getStringAttr(symbolName));

        if (module.lookupSymbol<mlir::func::FuncOp>(symbolName))
        {
            continue;
        }

        mlir::OpBuilder::InsertionGuard const g(builder);
        builder.setInsertionPointToEnd(&module.getBodyRegion().front());

        const mlir::Location loc    = op.getLoc();
        auto                 i64Ty  = builder.getIntegerType(64);
        auto                 i8Ty   = builder.getIntegerType(8);
        auto                 fnType = builder.getFunctionType(mlir::TypeRange{i64Ty, i64Ty}, mlir::TypeRange{i8Ty});
        auto                 fn     = mlir::func::FuncOp::create(builder, loc, symbolName, fnType);
        fn->setAttr("llvmdsdl.delimiter_header_validate", builder.getUnitAttr());
        fn->setAttr("llvmdsdl.schema_sym", schemaSym);
        if (sectionAttr)
        {
            fn->setAttr("llvmdsdl.section", sectionAttr);
        }

        auto* entry = fn.addEntryBlock();
        builder.setInsertionPointToStart(entry);
        auto headerBytes    = entry->getArgument(0);
        auto remainingBytes = entry->getArgument(1);
        auto zeroConst      = mlir::arith::ConstantIntOp::create(builder, loc, 0, 64);
        auto isNegative =
            mlir::arith::CmpIOp::create(builder, loc, mlir::arith::CmpIPredicate::slt, headerBytes, zeroConst);
        auto tooLarge =
            mlir::arith::CmpIOp::create(builder, loc, mlir::arith::CmpIPredicate::ugt, headerBytes, remainingBytes);
        auto invalid = mlir::arith::OrIOp::create(builder, loc, isNegative, tooLarge);
        auto status  = mlir::scf::IfOp::create(builder, loc, mlir::TypeRange{i8Ty}, invalid, true);
        {
            mlir::OpBuilder thenBuilder = status.getThenBodyBuilder();
            auto            fail        = mlir::arith::ConstantIntOp::create(thenBuilder, loc, -12, 8).getResult();
            mlir::scf::YieldOp::create(thenBuilder, loc, fail);
        }
        {
            mlir::OpBuilder elseBuilder = status.getElseBodyBuilder();
            auto            ok          = mlir::arith::ConstantIntOp::create(elseBuilder, loc, 0, 8).getResult();
            mlir::scf::YieldOp::create(elseBuilder, loc, ok);
        }
        mlir::func::ReturnOp::create(builder, loc, status.getResults());
    }

    return mlir::success();
}

mlir::LogicalResult runLowerDSDLSerializationLowering(mlir::ModuleOp module)
{
    mlir::OpBuilder                              builder(module.getContext());
    std::vector<mlir::dsdl::SerializationPlanOp> plans;

    for (mlir::dsdl::SchemaOp op : module.getBodyRegion().front().getOps<mlir::dsdl::SchemaOp>())
    {
        if (op.getBody().empty())
        {
            continue;
        }
        for (const mlir::dsdl::SerializationPlanOp child :
             op.getBody().front().getOps<mlir::dsdl::SerializationPlanOp>())
        {
            if (mlir::failed(canonicalizePlan(child, builder)))
            {
                return mlir::failure();
            }
            plans.push_back(child);
        }
    }

    // Stamped whether or not any plan was found. The attribute records that this pass ran at this
    // contract version, not that the module contains plans, so a module with no definitions is a
    // well-formed lowered module rather than an unstamped one a consumer must reject.
    stampLoweredContractAttributes(module, builder);

    for (const mlir::dsdl::SerializationPlanOp plan : plans)
    {
        if (mlir::failed(createPlanCapacityCheckFunction(module, plan, builder)))
        {
            return mlir::failure();
        }
        if (mlir::failed(createUnionTagValidationFunction(module, plan, builder)))
        {
            return mlir::failure();
        }
        if (mlir::failed(createScalarUnsignedFieldHelpers(module, plan, builder)))
        {
            return mlir::failure();
        }
        if (mlir::failed(createScalarSignedFieldHelpers(module, plan, builder)))
        {
            return mlir::failure();
        }
        if (mlir::failed(createScalarFloatFieldHelpers(module, plan, builder)))
        {
            return mlir::failure();
        }
        if (mlir::failed(createUnionTagIoHelpers(module, plan, builder)))
        {
            return mlir::failure();
        }
        if (mlir::failed(createArrayLengthValidationHelpers(module, plan, builder)))
        {
            return mlir::failure();
        }
        if (mlir::failed(createArrayLengthPrefixHelpers(module, plan, builder)))
        {
            return mlir::failure();
        }
        if (mlir::failed(createDelimiterHeaderValidationHelpers(module, plan, builder)))
        {
            return mlir::failure();
        }
    }

    return mlir::success();
}

struct LowerDSDLSerializationPass
    : public mlir::PassWrapper<LowerDSDLSerializationPass, mlir::OperationPass<mlir::ModuleOp>>
{
    llvm::StringRef getArgument() const final
    {
        return "lower-dsdl-serialization";
    }
    llvm::StringRef getDescription() const final
    {
        return "Lower DSDL serialisation-plan ops into canonical control-flow form";
    }
    void getDependentDialects(mlir::DialectRegistry& registry) const override
    {
        registry.insert<mlir::arith::ArithDialect, mlir::func::FuncDialect, mlir::scf::SCFDialect>();
    }

    // NOLINTNEXTLINE(misc-override-with-different-visibility) -- MLIR declares passes this way.
    void runOnOperation() override
    {
        if (mlir::failed(runLowerDSDLSerializationLowering(getOperation())))
        {
            signalPassFailure();
        }
    }
};

struct LowerDSDLExecPass : public mlir::PassWrapper<LowerDSDLExecPass, mlir::OperationPass<mlir::ModuleOp>>
{
    llvm::StringRef getArgument() const final
    {
        return "lower-dsdl-exec";
    }
    llvm::StringRef getDescription() const final
    {
        return "Lower DSDL serialisation-plan ops into canonical executable-contract control-flow form";
    }
    void getDependentDialects(mlir::DialectRegistry& registry) const override
    {
        registry.insert<mlir::arith::ArithDialect, mlir::func::FuncDialect, mlir::scf::SCFDialect>();
    }

    // NOLINTNEXTLINE(misc-override-with-different-visibility) -- MLIR declares passes this way.
    void runOnOperation() override
    {
        if (mlir::failed(runLowerDSDLSerializationLowering(getOperation())))
        {
            signalPassFailure();
        }
    }
};

/// @brief Replaces a host-image section's field-wise body with one move.
///
/// The body it rewrites has a shape every plan body shares: the buffer is taken once, the fields
/// are worked through, and the consumed count is stored. Only the middle is replaced, so this does
/// not care whether the field work was scalars, a loop over a fixed array, or a call into a nested
/// type -- all three are the same bytes once the verdict holds.
///
/// It declines anything it does not recognise, and a declined body is the one every backend
/// already translates, so declining is free.
struct FoldDSDLHostImageBodiesPass
    : public mlir::PassWrapper<FoldDSDLHostImageBodiesPass, mlir::OperationPass<mlir::ModuleOp>>
{
    llvm::StringRef getArgument() const final
    {
        return "dsdl-fold-host-image-bodies";
    }
    llvm::StringRef getDescription() const final
    {
        return "Fold a host-image section's serdes bodies into a single move";
    }

    // NOLINTNEXTLINE(misc-override-with-different-visibility) -- MLIR declares passes this way.
    void runOnOperation() override
    {
        auto module = getOperation();

        // The verdict lives on the plan; the bodies are functions beside it. They are paired by the
        // schema symbol the lowering stamps on both.
        llvm::StringMap<std::int64_t> foldableBytes;
        for (mlir::dsdl::SchemaOp schema : module.getBodyRegion().front().getOps<mlir::dsdl::SchemaOp>())
        {
            if (schema.getBody().empty())
            {
                continue;
            }
            for (mlir::dsdl::SerializationPlanOp plan :
                 schema.getBody().front().getOps<mlir::dsdl::SerializationPlanOp>())
            {
                if (!plan.getHostImage())
                {
                    continue;
                }
                const std::int64_t bits = plan.getMaxBits();
                if ((bits <= 0) || ((bits % 8) != 0))
                {
                    continue;
                }
                foldableBytes[sectionBodyKey(schema.getSymName(), plan.getSection())] = bits / 8;
            }
        }
        if (foldableBytes.empty())
        {
            return;
        }

        for (const mlir::func::FuncOp body : module.getOps<mlir::func::FuncOp>())
        {
            const auto kind = body->getAttrOfType<mlir::StringAttr>("llvmdsdl.plan_body");
            const auto sym  = body->getAttrOfType<mlir::StringAttr>("llvmdsdl.schema_sym");
            if (!kind || !sym)
            {
                continue;
            }
            const auto section = body->getAttrOfType<mlir::StringAttr>("llvmdsdl.section");
            const auto found   = foldableBytes.find(
                sectionBodyKey(sym.getValue(),
                               section ? std::optional<llvm::StringRef>(section.getValue()) : std::nullopt));
            if (found == foldableBytes.end())
            {
                continue;
            }
            if (kind.getValue() == "deserialize")
            {
                (void) foldDeserialize(body, found->second);
            }
            else if (kind.getValue() == "serialize")
            {
                (void) foldSerialize(body, found->second);
            }
        }
    }

private:
    static std::string sectionBodyKey(const llvm::StringRef schemaSym, const std::optional<llvm::StringRef> section)
    {
        return schemaSym.str() + "/" + section.value_or(llvm::StringRef{}).str();
    }

    /// @brief The region between taking the buffer and storing the consumed count.
    ///
    /// Returns the ops to replace, or nothing when the body does not have this shape or when one of
    /// those ops is used by something outside the run -- which would make removing them a change in
    /// meaning rather than in speed.
    static std::optional<std::vector<mlir::Operation*>> fieldWorkAfter(mlir::Operation* anchorOp,
                                                                       mlir::Operation* stopBefore,
                                                                       mlir::Block&     block)
    {
        // The consumed count is computed between the field work and the store that keeps it, and it
        // reads the available size rather than any field, so it survives the fold. Everything it is
        // built from is excluded from the run before anything is removed.
        llvm::SmallPtrSet<mlir::Operation*, 16> keep;
        std::vector<mlir::Value> pending(stopBefore->getOperands().begin(), stopBefore->getOperands().end());
        while (!pending.empty())
        {
            const mlir::Value value = pending.back();
            pending.pop_back();
            mlir::Operation* const definer = value.getDefiningOp();
            if ((definer == nullptr) || (definer->getBlock() != &block) || !keep.insert(definer).second)
            {
                continue;
            }
            pending.insert(pending.end(), definer->getOperands().begin(), definer->getOperands().end());
        }

        std::vector<mlir::Operation*>           run;
        llvm::SmallPtrSet<mlir::Operation*, 32> inRun;
        bool                                    collecting = false;
        for (mlir::Operation& op : block)
        {
            if (&op == anchorOp)
            {
                collecting = true;
                continue;
            }
            if (&op == stopBefore)
            {
                break;
            }
            if (collecting && !keep.contains(&op))
            {
                run.push_back(&op);
                inRun.insert(&op);
            }
        }
        if (!collecting || run.empty())
        {
            return std::nullopt;
        }
        for (mlir::Operation* op : run)
        {
            for (const mlir::Value result : op->getResults())
            {
                for (const mlir::Operation* const user : result.getUsers())
                {
                    if (!inRun.contains(user))
                    {
                        return std::nullopt;
                    }
                }
            }
        }
        return run;
    }

    static void eraseRun(std::vector<mlir::Operation*>& run)
    {
        // Backwards: a later op may use an earlier one's result, and erasing a value still in use
        // is not allowed.
        for (mlir::Operation* const op : llvm::reverse(run))
        {
            op->erase();
        }
    }

    static mlir::LogicalResult foldDeserialize(mlir::func::FuncOp body, const std::int64_t bytes)
    {
        // The accepted path is the `else` region of the rejection test, and the buffer is taken
        // there once.
        mlir::dsdl::BufferOrEmptyOp buffer;
        body.walk([&](mlir::dsdl::BufferOrEmptyOp op) { buffer = op; });
        if (!buffer)
        {
            return mlir::failure();
        }
        mlir::dsdl::StoreScalarOp consumed;
        for (mlir::Operation& op : *buffer->getBlock())
        {
            if (auto store = mlir::dyn_cast<mlir::dsdl::StoreScalarOp>(op))
            {
                consumed = store;
            }
        }
        if (!consumed)
        {
            return mlir::failure();
        }
        auto run = fieldWorkAfter(buffer, consumed.getOperation(), *buffer->getBlock());
        if (!run)
        {
            return mlir::failure();
        }

        mlir::OpBuilder builder(consumed);
        mlir::Value     size;
        // The available byte count is what the field reads were given; it is loaded before the
        // buffer is taken, and the consumed arithmetic still uses it.
        for (mlir::Operation& op : *buffer->getBlock())
        {
            if (auto load = mlir::dyn_cast<mlir::dsdl::LoadScalarOp>(op))
            {
                size = load.getResult();
                break;
            }
        }
        if (!size)
        {
            return mlir::failure();
        }
        mlir::dsdl::ImageReadOp::create(builder,
                                        body.getLoc(),
                                        body.getArgument(0),
                                        buffer.getResult(),
                                        size,
                                        builder.getI64IntegerAttr(bytes));
        eraseRun(*run);
        return mlir::success();
    }

    /// @brief The write side is not folded yet.
    ///
    /// Reading is a straight run between taking the buffer and storing the consumed count, so the
    /// fold replaces a span. Writing is a chain of `scf.if` steps threading an error code, and
    /// collapsing it means substituting the code the chain ends on -- the capacity check's, since a
    /// field write cannot fail once the buffer is known to hold the payload. Getting that
    /// substitution right is the remaining work; until it is, declining leaves the body every
    /// backend already translates.
    static mlir::LogicalResult foldSerialize(mlir::func::FuncOp /*body*/, std::int64_t /*bytes*/)
    {
        return mlir::failure();
    }
};

struct VerifyDSDLAliasLayoutPass
    : public mlir::PassWrapper<VerifyDSDLAliasLayoutPass, mlir::OperationPass<mlir::ModuleOp>>
{
    llvm::StringRef getArgument() const final
    {
        return "dsdl-verify-alias-layout";
    }
    llvm::StringRef getDescription() const final
    {
        return "Check each plan's layout verdicts against the steps it carries";
    }

    // NOLINTNEXTLINE(misc-override-with-different-visibility) -- MLIR declares passes this way.
    void runOnOperation() override
    {
        auto module = getOperation();
        // A nested composite's own extent is what the holder's alignment turns on, and the module
        // carries every type the holder names, so the plans are indexed before the walk.
        llvm::StringMap<mlir::dsdl::SerializationPlanOp> messagePlans;
        for (mlir::dsdl::SchemaOp op : module.getBodyRegion().front().getOps<mlir::dsdl::SchemaOp>())
        {
            if (op.getBody().empty())
            {
                continue;
            }
            for (mlir::dsdl::SerializationPlanOp child : op.getBody().front().getOps<mlir::dsdl::SerializationPlanOp>())
            {
                if (!child.getSection())
                {
                    messagePlans[compositeKey(op.getFullName(), op.getMajor(), op.getMinor())] = child;
                }
            }
        }

        for (mlir::dsdl::SchemaOp op : module.getBodyRegion().front().getOps<mlir::dsdl::SchemaOp>())
        {
            if (op.getBody().empty())
            {
                continue;
            }
            for (const mlir::dsdl::SerializationPlanOp child :
                 op.getBody().front().getOps<mlir::dsdl::SerializationPlanOp>())
            {
                if (mlir::failed(verifyPlan(child, messagePlans)))
                {
                    signalPassFailure();
                    return;
                }
            }
        }
    }

    static std::string compositeKey(const llvm::StringRef fullName, const std::int64_t major, const std::int64_t minor)
    {
        return fullName.str() + "." + std::to_string(major) + "." + std::to_string(minor);
    }

private:
    /// @brief Re-derives from the steps what the analysis recorded, and reports a disagreement.
    ///
    /// The steps say less than the schema did -- a composite's own verdict is not among them -- so
    /// this checks what they can decide: a plan whose steps are not a flat byte run cannot be
    /// `wire_flat`, and `host_image` never holds where `wire_flat` does not.
    static mlir::LogicalResult verifyPlan(mlir::dsdl::SerializationPlanOp                         plan,
                                          const llvm::StringMap<mlir::dsdl::SerializationPlanOp>& messagePlans)
    {
        const bool wireFlat  = plan.getWireFlat();
        const bool hostImage = plan.getHostImage();

        if (wireFlat && plan.getWireFlatReason())
        {
            return plan.emitError("wire_flat holds and carries a reason");
        }
        if (hostImage && plan.getHostImageReason())
        {
            return plan.emitError("host_image holds and carries a reason");
        }
        // Lowering states exactly one of the pair, so neither means the plan states no verdict --
        // hand-written IR, which `dsdl-opt` takes. There is then nothing to disagree with.
        if (!wireFlat && !plan.getWireFlatReason())
        {
            return mlir::success();
        }
        if (hostImage && !wireFlat)
        {
            return plan.emitError("host_image holds where wire_flat does not");
        }
        if (!wireFlat)
        {
            return mlir::success();
        }

        if (!plan.getSealed())
        {
            return plan.emitError("wire_flat holds for a delimited plan");
        }
        if (!plan.getFixedSize())
        {
            return plan.emitError("wire_flat holds for a plan whose length varies");
        }
        if (plan.getIsUnion())
        {
            return plan.emitError("wire_flat holds for a union plan");
        }
        if (plan.getBody().empty())
        {
            return plan.emitError("wire_flat holds for a plan with no steps");
        }

        std::int64_t offsetBits = 0;
        bool         hasPayload = false;
        for (mlir::Operation& stepOp : plan.getBody().front())
        {
            auto step = mlir::dyn_cast<mlir::dsdl::IOOp>(stepOp);
            if (!step)
            {
                continue;
            }
            // A composite step carries its width here; a scalar carries it in `bit_length`.
            const std::int64_t bits = step.isComposite() ? step.getMinBits() : step.getBitLength();
            if (step.isPadding())
            {
                offsetBits += bits;
                continue;
            }
            hasPayload = true;
            if ((offsetBits % 8) != 0)
            {
                return step.emitError("wire_flat holds but this field does not begin on a byte boundary");
            }
            if (step.isVariableArray())
            {
                return step.emitError("wire_flat holds but this field is a variable-length array");
            }
            // The wire carries a fixed array as a contiguous run, so the run is what has to land on
            // a byte boundary, not each element: `bool[8]` is one byte and `bool[4]` is not.
            const std::int64_t count = step.isArray() ? std::max<std::int64_t>(step.getArrayCapacity(), 0) : 1;
            const std::int64_t total = bits * count;
            if (bits <= 0 || (total % 8) != 0)
            {
                return step.emitError("wire_flat holds but this field is not a whole number of bytes");
            }
            offsetBits += total;
        }
        if (!hasPayload)
        {
            return plan.emitError("wire_flat holds for a plan with no payload field");
        }
        if (!hostImage)
        {
            return mlir::success();
        }
        return verifyHostImage(plan, messagePlans).first;
    }

    /// @brief Re-derives a plan's host extent, reporting a step the `host_image` claim contradicts.
    ///
    /// The structure holds no member for padding and holds each scalar in a whole-byte storage
    /// width, so a step that is padding, or whose width the host would widen, contradicts the claim.
    /// The offsets are then walked under natural alignment, which is what decided the verdict.
    /// @return Success or the error, paired with the plan's size and alignment in bytes.
    static std::pair<mlir::LogicalResult, std::pair<std::int64_t, std::int64_t>> verifyHostImage(
        mlir::dsdl::SerializationPlanOp                         plan,
        const llvm::StringMap<mlir::dsdl::SerializationPlanOp>& messagePlans,
        const unsigned                                          depth = 0)
    {
        const std::pair<std::int64_t, std::int64_t> unknown{0, 0};
        // A cycle is not expressible in DSDL, so this bounds a malformed module rather than a schema.
        if (depth > 64U)
        {
            return {plan.emitError("host_image verification recursed too deeply"), unknown};
        }

        std::int64_t offsetBytes = 0;
        std::int64_t alignBytes  = 1;
        for (mlir::Operation& stepOp : plan.getBody().front())
        {
            auto step = mlir::dyn_cast<mlir::dsdl::IOOp>(stepOp);
            if (!step)
            {
                continue;
            }
            if (step.isPadding())
            {
                return {step.emitError("host_image holds but this step is wire padding the structure does not hold"),
                        unknown};
            }

            std::int64_t elementSize  = 0;
            std::int64_t elementAlign = 1;
            if (step.isComposite())
            {
                const auto nested = messagePlans.find(compositeKey(step.getCompositeFullName().value_or(""),
                                                                   step.getCompositeMajor().value_or(0),
                                                                   step.getCompositeMinor().value_or(0)));
                // A type this module does not carry cannot be re-derived here; the analysis decided
                // it with the whole model in hand, and the reality lane measures the result.
                if (nested == messagePlans.end())
                {
                    return {mlir::success(), unknown};
                }
                const auto resolved = verifyHostImage(nested->second, messagePlans, depth + 1);
                if (mlir::failed(resolved.first))
                {
                    return {step.emitError("host_image holds but a nested type is not a byte image"), unknown};
                }
                if (resolved.second.second == 0)
                {
                    return {mlir::success(), unknown};
                }
                elementSize  = resolved.second.first;
                elementAlign = resolved.second.second;
            }
            else
            {
                const auto bits = static_cast<std::uint32_t>(step.getBitLength());
                const auto storage =
                    (step.getScalarCategory() == "float") ? floatStorageBits(bits) : scalarStorageBits(bits);
                if (storage != bits)
                {
                    return {step.emitError("host_image holds but the host stores this field wider than the wire "
                                           "carries it"),
                            unknown};
                }
                elementSize  = bits / 8;
                elementAlign = elementSize;
            }

            if ((elementAlign == 0) || ((offsetBytes % elementAlign) != 0))
            {
                return {step.emitError("host_image holds but the structure would pad before this field"), unknown};
            }
            const std::int64_t count = step.isArray() ? std::max<std::int64_t>(step.getArrayCapacity(), 0) : 1;
            offsetBytes += elementSize * count;
            alignBytes = std::max(alignBytes, elementAlign);
        }
        if ((offsetBytes % alignBytes) != 0)
        {
            return {plan.emitError("host_image holds but the structure would pad after its last field"), unknown};
        }
        return {mlir::success(), {offsetBytes, alignBytes}};
    }
};

}  // namespace

std::unique_ptr<mlir::Pass> createLowerDSDLSerializationPass()
{
    return std::make_unique<LowerDSDLSerializationPass>();
}

std::unique_ptr<mlir::Pass> createLowerDSDLExecPass()
{
    return std::make_unique<LowerDSDLExecPass>();
}

std::unique_ptr<mlir::Pass> createDSDLVerifyAliasLayoutPass()
{
    return std::make_unique<VerifyDSDLAliasLayoutPass>();
}

std::unique_ptr<mlir::Pass> createFoldDSDLHostImageBodiesPass()
{
    return std::make_unique<FoldDSDLHostImageBodiesPass>();
}

void addOptimizeLoweredSerDesPipeline(mlir::OpPassManager& pm)
{
    auto& funcPM = pm.nest<mlir::func::FuncOp>();
    funcPM.addPass(mlir::createCanonicalizerPass());
    funcPM.addPass(mlir::createCSEPass());
}

void addLowerDSDLBodiesPipeline(mlir::OpPassManager& pm,
                                const bool           optimizeLoweredSerDes,
                                const bool           targetObjectsAreByteImages)
{
    pm.addPass(createLowerDSDLExecPass());
    pm.addPass(createDSDLVerifyAliasLayoutPass());
    pm.addPass(createBuildDSDLPlanBodiesPass());
    // Its own stage, under the target's capability. Folding inside the optimise stage would make
    // the fast path turn on a flag about simplification, which is a different question.
    if (targetObjectsAreByteImages)
    {
        pm.addPass(createFoldDSDLHostImageBodiesPass());
    }
    // After the bodies: what is simplified here is what every backend translates.
    if (optimizeLoweredSerDes)
    {
        addOptimizeLoweredSerDesPipeline(pm);
    }
}

void registerDSDLPasses()
{
    static bool once = false;
    if (once)
    {
        return;
    }
    once = true;
    static mlir::PassRegistration<LowerDSDLSerializationPass> const  reg;
    static mlir::PassRegistration<LowerDSDLExecPass> const           regExec;
    static mlir::PassRegistration<VerifyDSDLAliasLayoutPass> const   regAlias;
    static mlir::PassRegistration<FoldDSDLHostImageBodiesPass> const regFold;
    static mlir::PassPipelineRegistration<> const
        optimizeLoweredSerDesPipeline("optimize-dsdl-lowered-serdes",
                                      "Apply semantics-preserving canonicalisation and CSE to lowered DSDL SerDes IR",
                                      [](mlir::OpPassManager& pm) { addOptimizeLoweredSerDesPipeline(pm); });
    static mlir::PassPipelineRegistration<> const
        lowerBodiesPipeline("lower-dsdl-bodies",
                            "Lower serialisation plans to serialise and deserialise functions of dialect operations",
                            [](mlir::OpPassManager& pm) { addLowerDSDLBodiesPipeline(pm, false); });
    registerBuildDSDLPlanBodiesPass();
    registerDSDLConvertPasses();
    registerEmitDSDLRuntimePass();
    registerDSDLToLLVMPasses();
}

}  // namespace llvmdsdl
