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

#include "llvmdsdl/Transforms/Passes.h"

#include <llvm/ADT/StringRef.h>
#include <llvm/ADT/Twine.h>
#include <llvm/ADT/ilist_iterator.h>
#include <llvm/Support/Casting.h>
#include <llvm/Support/ErrorHandling.h>
#include <llvm/Support/LogicalResult.h>
#include <llvm/Support/TypeName.h>
#include <mlir/IR/Attributes.h>
#include <mlir/IR/Block.h>
#include <mlir/IR/BuiltinAttributes.h>
#include <mlir/IR/BuiltinTypeInterfaces.h>
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
#include <cstdint>
#include <limits>
#include <set>
#include <string>
#include <vector>
#include <functional>
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

namespace llvmdsdl
{

void registerDSDLConvertPasses();

namespace
{

std::int64_t nonNegative(const std::int64_t value)
{
    return std::max<std::int64_t>(value, 0);
}

std::int64_t intAttrOrDefault(mlir::Operation* op, llvm::StringRef name, const std::int64_t fallback)
{
    if (const auto attr = op->getAttrOfType<mlir::IntegerAttr>(name))
    {
        return attr.getInt();
    }
    return fallback;
}

void setI64Attr(mlir::Operation* op, llvm::StringRef name, const std::int64_t value, mlir::Builder& builder)
{
    op->setAttr(name, builder.getI64IntegerAttr(value));
}

void setI32Attr(mlir::Operation* op, llvm::StringRef name, const std::int64_t value, mlir::Builder& builder)
{
    op->setAttr(name, builder.getI32IntegerAttr(static_cast<std::int32_t>(value)));
}

void stampLoweredContractAttributes(mlir::Operation* op, mlir::Builder& builder)
{
    op->setAttr(kLoweredSerDesContractVersionAttr, builder.getI64IntegerAttr(kLoweredSerDesContractVersion));
    op->setAttr(kLoweredSerDesContractProducerAttr, builder.getStringAttr(kLoweredSerDesContractProducer));
}

std::string sectionSuffix(llvm::StringRef sectionName)
{
    if (sectionName == "request")
    {
        return "__request";
    }
    if (sectionName == "response")
    {
        return "__response";
    }
    return "";
}

mlir::LogicalResult canonicalizePlan(mlir::Operation* plan, mlir::Builder& builder)
{
    auto&                         body = plan->getRegion(0).front();
    std::vector<mlir::Operation*> eraseOps;
    std::int64_t                  stepIndex    = 0;
    std::int64_t                  alignCount   = 0;
    std::int64_t                  fieldCount   = 0;
    std::int64_t                  paddingCount = 0;
    std::set<std::int64_t>        unionOptionIndexes;

    for (mlir::Operation& op : body)
    {
        const auto opName = op.getName().getStringRef();
        if (opName == "dsdl.align")
        {
            const std::int64_t bits = nonNegative(intAttrOrDefault(&op, "bits", 0));
            if (bits <= 1)
            {
                eraseOps.push_back(&op);
                continue;
            }
            setI32Attr(&op, "bits", bits, builder);
            setI64Attr(&op, "step_index", stepIndex++, builder);
            ++alignCount;
            continue;
        }

        if (opName == "dsdl.io")
        {
            const auto kindAttr  = op.getAttrOfType<mlir::StringAttr>("kind");
            const auto kind      = kindAttr ? kindAttr.getValue() : llvm::StringRef("field");
            const bool isPadding = kind == "padding";

            const std::int64_t minBits = nonNegative(intAttrOrDefault(&op, "min_bits", 0));
            const std::int64_t maxBits =
                std::max<std::int64_t>(nonNegative(intAttrOrDefault(&op, "max_bits", minBits)), minBits);

            const std::int64_t bitLength     = nonNegative(intAttrOrDefault(&op, "bit_length", /*fallback=*/0));
            const std::int64_t arrayCapacity = nonNegative(intAttrOrDefault(&op, "array_capacity", /*fallback=*/0));
            const std::int64_t arrayPrefixBits =
                nonNegative(intAttrOrDefault(&op, "array_length_prefix_bits", /*fallback=*/0));
            const std::int64_t alignmentBits =
                std::max<std::int64_t>(nonNegative(intAttrOrDefault(&op, "alignment_bits", /*fallback=*/1)), 1);
            const std::int64_t unionOptionIndex =
                nonNegative(intAttrOrDefault(&op, "union_option_index", /*fallback=*/0));
            const std::int64_t unionTagBits = nonNegative(intAttrOrDefault(&op, "union_tag_bits", /*fallback=*/0));

            if (isPadding && maxBits == 0)
            {
                eraseOps.push_back(&op);
                continue;
            }

            setI64Attr(&op, "min_bits", minBits, builder);
            setI64Attr(&op, "max_bits", maxBits, builder);
            setI64Attr(&op, "lowered_bits", maxBits, builder);
            setI64Attr(&op, "step_index", stepIndex++, builder);

            setI64Attr(&op, "bit_length", bitLength, builder);
            setI64Attr(&op, "array_capacity", arrayCapacity, builder);
            setI64Attr(&op, "array_length_prefix_bits", arrayPrefixBits, builder);
            setI64Attr(&op, "alignment_bits", alignmentBits, builder);
            setI64Attr(&op, "union_option_index", unionOptionIndex, builder);
            setI64Attr(&op, "union_tag_bits", unionTagBits, builder);

            if (!isPadding)
            {
                unionOptionIndexes.insert(unionOptionIndex);
            }

            if (isPadding)
            {
                ++paddingCount;
            }
            else
            {
                ++fieldCount;
            }
            continue;
        }

        return op.emitError("unsupported operation in serialization plan body");
    }

    for (mlir::Operation* op : eraseOps)
    {
        op->erase();
    }

    std::int64_t minBits = nonNegative(intAttrOrDefault(plan, "min_bits", 0));
    std::int64_t maxBits = nonNegative(intAttrOrDefault(plan, "max_bits", minBits));
    if (maxBits < minBits)
    {
        maxBits = minBits;
    }
    setI64Attr(plan, "min_bits", minBits, builder);
    setI64Attr(plan, "max_bits", maxBits, builder);
    setI64Attr(plan, kLoweredMinBitsAttr, minBits, builder);
    setI64Attr(plan, kLoweredMaxBitsAttr, maxBits, builder);
    setI64Attr(plan, kLoweredStepCountAttr, stepIndex, builder);
    setI64Attr(plan, kLoweredFieldCountAttr, fieldCount, builder);
    setI64Attr(plan, kLoweredPaddingCountAttr, paddingCount, builder);
    setI64Attr(plan, kLoweredAlignCountAttr, alignCount, builder);
    plan->setAttr(kLoweredPlanMarkerAttr, builder.getUnitAttr());
    stampLoweredContractAttributes(plan, builder);

    if (plan->hasAttr("is_union"))
    {
        const std::int64_t unionTagBits     = nonNegative(intAttrOrDefault(plan, "union_tag_bits", 0));
        const std::int64_t unionOptionCount = static_cast<std::int64_t>(unionOptionIndexes.size());
        setI64Attr(plan, "union_tag_bits", unionTagBits, builder);
        setI64Attr(plan, "union_option_count", unionOptionCount, builder);
    }

    return mlir::success();
}

mlir::LogicalResult createPlanCapacityCheckFunction(mlir::ModuleOp   module,
                                                    mlir::Operation* plan,
                                                    mlir::OpBuilder& builder)
{
    auto* schema = plan->getParentOp();
    if (!schema || schema->getName().getStringRef() != "dsdl.schema")
    {
        return plan->emitOpError("must be nested under dsdl.schema");
    }
    const auto schemaSym = schema->getAttrOfType<mlir::StringAttr>("sym_name");
    if (!schemaSym)
    {
        return schema->emitOpError("missing required sym_name attribute");
    }
    const auto        sectionAttr = plan->getAttrOfType<mlir::StringAttr>("section");
    const std::string section     = sectionAttr ? sectionAttr.getValue().str() : "";
    const std::string funcName =
        "__llvmdsdl_plan_capacity_check__" + schemaSym.getValue().str() + sectionSuffix(section);
    plan->setAttr(kLoweredCapacityCheckHelperAttr, builder.getStringAttr(funcName));
    if (module.lookupSymbol<mlir::func::FuncOp>(funcName))
    {
        return mlir::success();
    }

    mlir::OpBuilder::InsertionGuard g(builder);
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
    mlir::Value  capacityBits = entry->getArgument(0);
    std::int64_t requiredBits = 0;
    if (const auto maxBits = plan->getAttrOfType<mlir::IntegerAttr>("max_bits"))
    {
        requiredBits = nonNegative(maxBits.getInt());
    }
    else if (const auto loweredMaxBits = plan->getAttrOfType<mlir::IntegerAttr>("lowered_max_bits"))
    {
        requiredBits = nonNegative(loweredMaxBits.getInt());
    }
    else
    {
        return plan->emitOpError("missing required max_bits metadata");
    }

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

mlir::LogicalResult createUnionTagValidationFunction(mlir::ModuleOp   module,
                                                     mlir::Operation* plan,
                                                     mlir::OpBuilder& builder)
{
    if (!plan->hasAttr("is_union"))
    {
        return mlir::success();
    }

    auto* schema = plan->getParentOp();
    if (!schema || schema->getName().getStringRef() != "dsdl.schema")
    {
        return plan->emitOpError("must be nested under dsdl.schema");
    }
    const auto schemaSym = schema->getAttrOfType<mlir::StringAttr>("sym_name");
    if (!schemaSym)
    {
        return schema->emitOpError("missing required sym_name attribute");
    }
    const auto        sectionAttr = plan->getAttrOfType<mlir::StringAttr>("section");
    const std::string section     = sectionAttr ? sectionAttr.getValue().str() : "";
    const std::string funcName =
        "__llvmdsdl_plan_validate_union_tag__" + schemaSym.getValue().str() + sectionSuffix(section);
    plan->setAttr(kLoweredUnionTagValidateHelperAttr, builder.getStringAttr(funcName));
    if (module.lookupSymbol<mlir::func::FuncOp>(funcName))
    {
        return mlir::success();
    }

    std::set<std::int64_t> optionIndexes;
    if (plan->getNumRegions() > 0 && !plan->getRegion(0).empty())
    {
        for (mlir::Operation& op : plan->getRegion(0).front())
        {
            if (op.getName().getStringRef() != "dsdl.io")
            {
                continue;
            }
            const auto kindAttr = op.getAttrOfType<mlir::StringAttr>("kind");
            const auto kind     = kindAttr ? kindAttr.getValue() : llvm::StringRef("field");
            if (kind == "padding")
            {
                continue;
            }
            optionIndexes.insert(nonNegative(intAttrOrDefault(&op, "union_option_index", /*fallback=*/0)));
        }
    }
    if (optionIndexes.empty())
    {
        return plan->emitOpError("union plan has no selectable options");
    }

    mlir::OpBuilder::InsertionGuard g(builder);
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
    mlir::Value tagValue = entry->getArgument(0);
    mlir::Value anyMatch = mlir::arith::ConstantIntOp::create(builder, loc, 0, 1).getResult();
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

mlir::LogicalResult createScalarUnsignedFieldHelpers(mlir::ModuleOp   module,
                                                     mlir::Operation* plan,
                                                     mlir::OpBuilder& builder)
{
    auto* schema = plan->getParentOp();
    if (!schema || schema->getName().getStringRef() != "dsdl.schema")
    {
        return plan->emitOpError("must be nested under dsdl.schema");
    }
    const auto schemaSym = schema->getAttrOfType<mlir::StringAttr>("sym_name");
    if (!schemaSym)
    {
        return schema->emitOpError("missing required sym_name attribute");
    }
    const auto        sectionAttr = plan->getAttrOfType<mlir::StringAttr>("section");
    const std::string section     = sectionAttr ? sectionAttr.getValue().str() : "";

    if (plan->getNumRegions() == 0 || plan->getRegion(0).empty())
    {
        return mlir::success();
    }

    for (mlir::Operation& op : plan->getRegion(0).front())
    {
        if (op.getName().getStringRef() != "dsdl.io")
        {
            continue;
        }
        const auto kindAttr = op.getAttrOfType<mlir::StringAttr>("kind");
        const auto kind     = kindAttr ? kindAttr.getValue() : llvm::StringRef("field");
        if (kind != "field")
        {
            continue;
        }
        const auto scalarAttr = op.getAttrOfType<mlir::StringAttr>("scalar_category");
        const auto scalar     = scalarAttr ? scalarAttr.getValue() : llvm::StringRef("unsigned");
        if (scalar != "unsigned" && scalar != "byte" && scalar != "utf8")
        {
            continue;
        }
        const std::int64_t bitLength = nonNegative(intAttrOrDefault(&op, "bit_length", /*fallback=*/0));
        if (bitLength <= 0 || bitLength > 64)
        {
            continue;
        }
        const std::int64_t stepIndex    = nonNegative(intAttrOrDefault(&op, "step_index", /*fallback=*/0));
        const auto         castModeAttr = op.getAttrOfType<mlir::StringAttr>("cast_mode");
        const auto         castMode     = castModeAttr ? castModeAttr.getValue() : llvm::StringRef("truncated");

        const std::string symbolStem = "__llvmdsdl_plan_scalar_unsigned__" + schemaSym.getValue().str() +
                                       sectionSuffix(section) + "__" + std::to_string(stepIndex);
        const std::string serName   = symbolStem + "__ser";
        const std::string deserName = symbolStem + "__deser";
        op.setAttr("lowered_ser_unsigned_helper", builder.getStringAttr(serName));
        op.setAttr("lowered_deser_unsigned_helper", builder.getStringAttr(deserName));

        const bool          fullWidth = (bitLength == 64);
        const std::uint64_t mask =
            fullWidth ? UINT64_MAX : ((UINT64_C(1) << static_cast<unsigned>(bitLength)) - UINT64_C(1));
        const auto maskSigned = fullWidth ? INT64_C(-1) : static_cast<std::int64_t>(mask);

        if (!module.lookupSymbol<mlir::func::FuncOp>(serName))
        {
            mlir::OpBuilder::InsertionGuard g(builder);
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
            auto        value  = entry->getArgument(0);
            mlir::Value result = value;
            if (fullWidth)
            {
                result = value;
            }
            else if (castMode == "saturated")
            {
                auto maskConst = mlir::arith::ConstantIntOp::create(builder, loc, maskSigned, 64);
                auto over = mlir::arith::CmpIOp::create(builder, loc, mlir::arith::CmpIPredicate::ugt, value, maskConst);
                result    = mlir::arith::SelectOp::create(builder, loc, over, maskConst, value).getResult();
            }
            else
            {
                auto maskConst = mlir::arith::ConstantIntOp::create(builder, loc, maskSigned, 64);
                result         = mlir::arith::AndIOp::create(builder, loc, value, maskConst).getResult();
            }
            mlir::func::ReturnOp::create(builder, loc, result);
        }

        if (!module.lookupSymbol<mlir::func::FuncOp>(deserName))
        {
            mlir::OpBuilder::InsertionGuard g(builder);
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
            auto value = entry->getArgument(0);
            if (fullWidth)
            {
                mlir::func::ReturnOp::create(builder, loc, value);
            }
            else
            {
                auto maskConst = mlir::arith::ConstantIntOp::create(builder, loc, maskSigned, 64);
                auto masked    = mlir::arith::AndIOp::create(builder, loc, value, maskConst).getResult();
                mlir::func::ReturnOp::create(builder, loc, masked);
            }
        }
    }

    return mlir::success();
}

mlir::LogicalResult createScalarSignedFieldHelpers(mlir::ModuleOp   module,
                                                   mlir::Operation* plan,
                                                   mlir::OpBuilder& builder)
{
    auto* schema = plan->getParentOp();
    if (!schema || schema->getName().getStringRef() != "dsdl.schema")
    {
        return plan->emitOpError("must be nested under dsdl.schema");
    }
    const auto schemaSym = schema->getAttrOfType<mlir::StringAttr>("sym_name");
    if (!schemaSym)
    {
        return schema->emitOpError("missing required sym_name attribute");
    }
    const auto        sectionAttr = plan->getAttrOfType<mlir::StringAttr>("section");
    const std::string section     = sectionAttr ? sectionAttr.getValue().str() : "";

    if (plan->getNumRegions() == 0 || plan->getRegion(0).empty())
    {
        return mlir::success();
    }

    for (mlir::Operation& op : plan->getRegion(0).front())
    {
        if (op.getName().getStringRef() != "dsdl.io")
        {
            continue;
        }
        const auto kindAttr = op.getAttrOfType<mlir::StringAttr>("kind");
        const auto kind     = kindAttr ? kindAttr.getValue() : llvm::StringRef("field");
        if (kind != "field")
        {
            continue;
        }
        const auto scalarAttr = op.getAttrOfType<mlir::StringAttr>("scalar_category");
        const auto scalar     = scalarAttr ? scalarAttr.getValue() : llvm::StringRef("signed");
        if (scalar != "signed")
        {
            continue;
        }
        const std::int64_t bitLength = nonNegative(intAttrOrDefault(&op, "bit_length", /*fallback=*/0));
        if (bitLength <= 0 || bitLength > 64)
        {
            continue;
        }
        const std::int64_t stepIndex    = nonNegative(intAttrOrDefault(&op, "step_index", /*fallback=*/0));
        const auto         castModeAttr = op.getAttrOfType<mlir::StringAttr>("cast_mode");
        const auto         castMode     = castModeAttr ? castModeAttr.getValue() : llvm::StringRef("truncated");

        const std::string symbolStem = "__llvmdsdl_plan_scalar_signed__" + schemaSym.getValue().str() +
                                       sectionSuffix(section) + "__" + std::to_string(stepIndex);
        const std::string serName   = symbolStem + "__ser";
        const std::string deserName = symbolStem + "__deser";
        op.setAttr("lowered_ser_signed_helper", builder.getStringAttr(serName));
        op.setAttr("lowered_deser_signed_helper", builder.getStringAttr(deserName));

        std::int64_t minValue = std::numeric_limits<std::int64_t>::min();
        std::int64_t maxValue = std::numeric_limits<std::int64_t>::max();
        if (bitLength < 64)
        {
            maxValue = (INT64_C(1) << static_cast<unsigned>(bitLength - 1)) - INT64_C(1);
            minValue = -(INT64_C(1) << static_cast<unsigned>(bitLength - 1));
        }

        if (!module.lookupSymbol<mlir::func::FuncOp>(serName))
        {
            mlir::OpBuilder::InsertionGuard g(builder);
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
            auto        value  = entry->getArgument(0);
            mlir::Value result = value;
            if (castMode == "saturated" && bitLength < 64)
            {
                auto minConst = mlir::arith::ConstantIntOp::create(builder, loc, minValue, 64);
                auto maxConst = mlir::arith::ConstantIntOp::create(builder, loc, maxValue, 64);
                auto below = mlir::arith::CmpIOp::create(builder, loc, mlir::arith::CmpIPredicate::slt, value, minConst);
                auto above = mlir::arith::CmpIOp::create(builder, loc, mlir::arith::CmpIPredicate::sgt, value, maxConst);
                auto clampedLow = mlir::arith::SelectOp::create(builder, loc, below, minConst, value);
                result          = mlir::arith::SelectOp::create(builder, loc, above, maxConst, clampedLow).getResult();
            }
            mlir::func::ReturnOp::create(builder, loc, result);
        }

        if (!module.lookupSymbol<mlir::func::FuncOp>(deserName))
        {
            mlir::OpBuilder::InsertionGuard g(builder);
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
            auto value = entry->getArgument(0);

            if (bitLength >= 64)
            {
                mlir::func::ReturnOp::create(builder, loc, value);
            }
            else
            {
                const std::uint64_t maskU       = (UINT64_C(1) << static_cast<unsigned>(bitLength)) - UINT64_C(1);
                const std::uint64_t signU       = UINT64_C(1) << static_cast<unsigned>(bitLength - 1);
                const std::uint64_t extendMaskU = ~maskU;
                auto maskConst = mlir::arith::ConstantIntOp::create(builder, loc, static_cast<std::int64_t>(maskU), 64);
                auto signConst = mlir::arith::ConstantIntOp::create(builder, loc, static_cast<std::int64_t>(signU), 64);
                auto extendConst =
                    mlir::arith::ConstantIntOp::create(builder, loc, static_cast<std::int64_t>(extendMaskU), 64);
                auto zeroConst = mlir::arith::ConstantIntOp::create(builder, loc, 0, 64);
                auto masked    = mlir::arith::AndIOp::create(builder, loc, value, maskConst).getResult();
                auto signPart  = mlir::arith::AndIOp::create(builder, loc, masked, signConst).getResult();
                auto isNegative =
                    mlir::arith::CmpIOp::create(builder, loc, mlir::arith::CmpIPredicate::ne, signPart, zeroConst);
                auto negExtended = mlir::arith::OrIOp::create(builder, loc, masked, extendConst).getResult();
                auto result = mlir::arith::SelectOp::create(builder, loc, isNegative, negExtended, masked).getResult();
                mlir::func::ReturnOp::create(builder, loc, result);
            }
        }
    }

    return mlir::success();
}

mlir::LogicalResult createScalarFloatFieldHelpers(mlir::ModuleOp   module,
                                                  mlir::Operation* plan,
                                                  mlir::OpBuilder& builder)
{
    auto* schema = plan->getParentOp();
    if (!schema || schema->getName().getStringRef() != "dsdl.schema")
    {
        return plan->emitOpError("must be nested under dsdl.schema");
    }
    const auto schemaSym = schema->getAttrOfType<mlir::StringAttr>("sym_name");
    if (!schemaSym)
    {
        return schema->emitOpError("missing required sym_name attribute");
    }
    const auto        sectionAttr = plan->getAttrOfType<mlir::StringAttr>("section");
    const std::string section     = sectionAttr ? sectionAttr.getValue().str() : "";

    if (plan->getNumRegions() == 0 || plan->getRegion(0).empty())
    {
        return mlir::success();
    }

    for (mlir::Operation& op : plan->getRegion(0).front())
    {
        if (op.getName().getStringRef() != "dsdl.io")
        {
            continue;
        }
        const auto kindAttr = op.getAttrOfType<mlir::StringAttr>("kind");
        const auto kind     = kindAttr ? kindAttr.getValue() : llvm::StringRef("field");
        if (kind != "field")
        {
            continue;
        }
        const auto scalarAttr = op.getAttrOfType<mlir::StringAttr>("scalar_category");
        const auto scalar     = scalarAttr ? scalarAttr.getValue() : llvm::StringRef("float");
        if (scalar != "float")
        {
            continue;
        }
        const std::int64_t bitLength = nonNegative(intAttrOrDefault(&op, "bit_length", /*fallback=*/0));
        if (bitLength != 16 && bitLength != 32 && bitLength != 64)
        {
            continue;
        }
        const std::int64_t stepIndex  = nonNegative(intAttrOrDefault(&op, "step_index", /*fallback=*/0));
        const std::string  symbolStem = "__llvmdsdl_plan_scalar_float__" + schemaSym.getValue().str() +
                                       sectionSuffix(section) + "__" + std::to_string(stepIndex);
        const std::string serName   = symbolStem + "__ser";
        const std::string deserName = symbolStem + "__deser";
        op.setAttr("lowered_ser_float_helper", builder.getStringAttr(serName));
        op.setAttr("lowered_deser_float_helper", builder.getStringAttr(deserName));

        // Width-match the helper to the field's native storage type: 16/32-bit
        // fields are held as `float` (f32), 64-bit as `double` (f64). Typing the
        // helper as f64 for every width forced callers to promote a float32
        // value to double and narrow it back, which canonicalizes a signaling
        // NaN's mantissa payload and diverges bit-for-bit from the reference
        // compiler. The helper is an identity pass-through, so matching the width
        // preserves the exact bits.
        auto floatTy = (bitLength == 64) ? mlir::Type(builder.getF64Type()) : mlir::Type(builder.getF32Type());

        if (!module.lookupSymbol<mlir::func::FuncOp>(serName))
        {
            mlir::OpBuilder::InsertionGuard g(builder);
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
            mlir::OpBuilder::InsertionGuard g(builder);
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

mlir::LogicalResult createArrayLengthValidationHelpers(mlir::ModuleOp   module,
                                                       mlir::Operation* plan,
                                                       mlir::OpBuilder& builder)
{
    auto* schema = plan->getParentOp();
    if (!schema || schema->getName().getStringRef() != "dsdl.schema")
    {
        return plan->emitOpError("must be nested under dsdl.schema");
    }
    const auto schemaSym = schema->getAttrOfType<mlir::StringAttr>("sym_name");
    if (!schemaSym)
    {
        return schema->emitOpError("missing required sym_name attribute");
    }
    const auto        sectionAttr = plan->getAttrOfType<mlir::StringAttr>("section");
    const std::string section     = sectionAttr ? sectionAttr.getValue().str() : "";

    if (plan->getNumRegions() == 0 || plan->getRegion(0).empty())
    {
        return mlir::success();
    }

    for (mlir::Operation& op : plan->getRegion(0).front())
    {
        if (op.getName().getStringRef() != "dsdl.io")
        {
            continue;
        }
        const auto kindAttr = op.getAttrOfType<mlir::StringAttr>("kind");
        const auto kind     = kindAttr ? kindAttr.getValue() : llvm::StringRef("field");
        if (kind != "field")
        {
            continue;
        }
        const auto arrayKindAttr = op.getAttrOfType<mlir::StringAttr>("array_kind");
        const auto arrayKind     = arrayKindAttr ? arrayKindAttr.getValue() : llvm::StringRef("none");
        const bool variableArray = (arrayKind == "variable_inclusive" || arrayKind == "variable_exclusive");
        if (!variableArray)
        {
            continue;
        }
        const std::int64_t capacity   = nonNegative(intAttrOrDefault(&op, "array_capacity", /*fallback=*/0));
        const std::int64_t stepIndex  = nonNegative(intAttrOrDefault(&op, "step_index", /*fallback=*/0));
        const std::string  symbolName = "__llvmdsdl_plan_validate_array_length__" + schemaSym.getValue().str() +
                                       sectionSuffix(section) + "__" + std::to_string(stepIndex);
        op.setAttr("lowered_array_length_validate_helper", builder.getStringAttr(symbolName));

        if (module.lookupSymbol<mlir::func::FuncOp>(symbolName))
        {
            continue;
        }

        mlir::OpBuilder::InsertionGuard g(builder);
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
        auto invalid    = mlir::arith::OrIOp::create(builder, loc, isNegative, tooLarge);
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

mlir::LogicalResult createArrayLengthPrefixHelpers(mlir::ModuleOp   module,
                                                   mlir::Operation* plan,
                                                   mlir::OpBuilder& builder)
{
    auto* schema = plan->getParentOp();
    if (!schema || schema->getName().getStringRef() != "dsdl.schema")
    {
        return plan->emitOpError("must be nested under dsdl.schema");
    }
    const auto schemaSym = schema->getAttrOfType<mlir::StringAttr>("sym_name");
    if (!schemaSym)
    {
        return schema->emitOpError("missing required sym_name attribute");
    }
    const auto        sectionAttr = plan->getAttrOfType<mlir::StringAttr>("section");
    const std::string section     = sectionAttr ? sectionAttr.getValue().str() : "";

    if (plan->getNumRegions() == 0 || plan->getRegion(0).empty())
    {
        return mlir::success();
    }

    for (mlir::Operation& op : plan->getRegion(0).front())
    {
        if (op.getName().getStringRef() != "dsdl.io")
        {
            continue;
        }
        const auto kindAttr = op.getAttrOfType<mlir::StringAttr>("kind");
        const auto kind     = kindAttr ? kindAttr.getValue() : llvm::StringRef("field");
        if (kind != "field")
        {
            continue;
        }
        const auto arrayKindAttr = op.getAttrOfType<mlir::StringAttr>("array_kind");
        const auto arrayKind     = arrayKindAttr ? arrayKindAttr.getValue() : llvm::StringRef("none");
        const bool variableArray = (arrayKind == "variable_inclusive" || arrayKind == "variable_exclusive");
        if (!variableArray)
        {
            continue;
        }
        const std::int64_t prefixBits = nonNegative(intAttrOrDefault(&op, "array_length_prefix_bits", /*fallback=*/0));
        if (prefixBits <= 0 || prefixBits > 64)
        {
            return op.emitOpError("invalid array-length prefix width");
        }
        const std::int64_t stepIndex  = nonNegative(intAttrOrDefault(&op, "step_index", /*fallback=*/0));
        const std::string  symbolStem = "__llvmdsdl_plan_array_length_prefix__" + schemaSym.getValue().str() +
                                       sectionSuffix(section) + "__" + std::to_string(stepIndex);
        const std::string serName   = symbolStem + "__ser";
        const std::string deserName = symbolStem + "__deser";
        op.setAttr("lowered_ser_array_length_prefix_helper", builder.getStringAttr(serName));
        op.setAttr("lowered_deser_array_length_prefix_helper", builder.getStringAttr(deserName));

        const bool          fullWidth = (prefixBits == 64);
        const std::uint64_t mask =
            fullWidth ? UINT64_MAX : ((UINT64_C(1) << static_cast<unsigned>(prefixBits)) - UINT64_C(1));
        const auto maskSigned = fullWidth ? INT64_C(-1) : static_cast<std::int64_t>(mask);

        if (!module.lookupSymbol<mlir::func::FuncOp>(serName))
        {
            mlir::OpBuilder::InsertionGuard g(builder);
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
            mlir::OpBuilder::InsertionGuard g(builder);
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

mlir::LogicalResult createUnionTagIoHelpers(mlir::ModuleOp module, mlir::Operation* plan, mlir::OpBuilder& builder)
{
    if (!plan->hasAttr("is_union"))
    {
        return mlir::success();
    }

    auto* schema = plan->getParentOp();
    if (!schema || schema->getName().getStringRef() != "dsdl.schema")
    {
        return plan->emitOpError("must be nested under dsdl.schema");
    }
    const auto schemaSym = schema->getAttrOfType<mlir::StringAttr>("sym_name");
    if (!schemaSym)
    {
        return schema->emitOpError("missing required sym_name attribute");
    }
    const auto         sectionAttr = plan->getAttrOfType<mlir::StringAttr>("section");
    const std::string  section     = sectionAttr ? sectionAttr.getValue().str() : "";
    const std::int64_t tagBits     = nonNegative(intAttrOrDefault(plan, "union_tag_bits", /*fallback=*/0));
    if (tagBits <= 0 || tagBits > 64)
    {
        return plan->emitOpError("invalid union tag width");
    }

    const std::string symbolStem = "__llvmdsdl_plan_union_tag__" + schemaSym.getValue().str() + sectionSuffix(section);
    const std::string serName    = symbolStem + "__ser";
    const std::string deserName  = symbolStem + "__deser";
    plan->setAttr(kLoweredSerUnionTagHelperAttr, builder.getStringAttr(serName));
    plan->setAttr(kLoweredDeserUnionTagHelperAttr, builder.getStringAttr(deserName));

    const bool          fullWidth = (tagBits == 64);
    const std::uint64_t mask = fullWidth ? UINT64_MAX : ((UINT64_C(1) << static_cast<unsigned>(tagBits)) - UINT64_C(1));
    const auto          maskSigned = fullWidth ? INT64_C(-1) : static_cast<std::int64_t>(mask);

    if (!module.lookupSymbol<mlir::func::FuncOp>(serName))
    {
        mlir::OpBuilder::InsertionGuard g(builder);
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
        mlir::OpBuilder::InsertionGuard g(builder);
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

mlir::LogicalResult createDelimiterHeaderValidationHelpers(mlir::ModuleOp   module,
                                                           mlir::Operation* plan,
                                                           mlir::OpBuilder& builder)
{
    auto* schema = plan->getParentOp();
    if (!schema || schema->getName().getStringRef() != "dsdl.schema")
    {
        return plan->emitOpError("must be nested under dsdl.schema");
    }
    const auto schemaSym = schema->getAttrOfType<mlir::StringAttr>("sym_name");
    if (!schemaSym)
    {
        return schema->emitOpError("missing required sym_name attribute");
    }
    const auto        sectionAttr = plan->getAttrOfType<mlir::StringAttr>("section");
    const std::string section     = sectionAttr ? sectionAttr.getValue().str() : "";

    if (plan->getNumRegions() == 0 || plan->getRegion(0).empty())
    {
        return mlir::success();
    }

    for (mlir::Operation& op : plan->getRegion(0).front())
    {
        if (op.getName().getStringRef() != "dsdl.io")
        {
            continue;
        }
        const auto kindAttr = op.getAttrOfType<mlir::StringAttr>("kind");
        const auto kind     = kindAttr ? kindAttr.getValue() : llvm::StringRef("field");
        if (kind != "field")
        {
            continue;
        }
        const auto scalarAttr = op.getAttrOfType<mlir::StringAttr>("scalar_category");
        const auto scalar     = scalarAttr ? scalarAttr.getValue() : llvm::StringRef("unsigned");
        if (scalar != "composite")
        {
            continue;
        }
        const auto sealedAttr      = op.getAttrOfType<mlir::BoolAttr>("composite_sealed");
        const bool compositeSealed = sealedAttr ? sealedAttr.getValue() : true;
        if (compositeSealed)
        {
            continue;
        }
        const std::int64_t stepIndex  = nonNegative(intAttrOrDefault(&op, "step_index", /*fallback=*/0));
        const std::string  symbolName = "__llvmdsdl_plan_validate_delimiter_header__" + schemaSym.getValue().str() +
                                       sectionSuffix(section) + "__" + std::to_string(stepIndex);
        op.setAttr("lowered_delimiter_validate_helper", builder.getStringAttr(symbolName));

        if (module.lookupSymbol<mlir::func::FuncOp>(symbolName))
        {
            continue;
        }

        mlir::OpBuilder::InsertionGuard g(builder);
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
    mlir::OpBuilder               builder(module.getContext());
    std::vector<mlir::Operation*> plans;

    for (mlir::Operation& op : module.getBodyRegion().front())
    {
        if (op.getName().getStringRef() != "dsdl.schema")
        {
            continue;
        }
        for (mlir::Operation& child : op.getRegion(0).front())
        {
            if (child.getName().getStringRef() != "dsdl.serialization_plan")
            {
                continue;
            }
            if (mlir::failed(canonicalizePlan(&child, builder)))
            {
                return mlir::failure();
            }
            plans.push_back(&child);
        }
    }

    // Stamped whether or not any plan was found. The attribute records that this pass ran at this
    // contract version, not that the module contains plans, so a module with no definitions is a
    // well-formed lowered module rather than an unstamped one a consumer must reject.
    stampLoweredContractAttributes(module, builder);

    for (mlir::Operation* plan : plans)
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
        return "Lower DSDL serialization-plan ops into canonical control-flow form";
    }
    void getDependentDialects(mlir::DialectRegistry& registry) const override
    {
        registry.insert<mlir::arith::ArithDialect, mlir::func::FuncDialect, mlir::scf::SCFDialect>();
    }

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
        return "Lower DSDL serialization-plan ops into canonical executable-contract control-flow form";
    }
    void getDependentDialects(mlir::DialectRegistry& registry) const override
    {
        registry.insert<mlir::arith::ArithDialect, mlir::func::FuncDialect, mlir::scf::SCFDialect>();
    }

    void runOnOperation() override
    {
        if (mlir::failed(runLowerDSDLSerializationLowering(getOperation())))
        {
            signalPassFailure();
        }
    }
};

struct AnnotateDSDLAliasabilityPass
    : public mlir::PassWrapper<AnnotateDSDLAliasabilityPass, mlir::OperationPass<mlir::ModuleOp>>
{
    llvm::StringRef getArgument() const final
    {
        return "dsdl-annotate-aliasability";
    }
    llvm::StringRef getDescription() const final
    {
        // Conservative annotator: stamps aliasability metadata only. It does not
        // prove anything about emitted-code overhead and does not switch the
        // serializer onto a zero-copy path.
        return "Annotate serialization plans with conservative zero-overhead aliasability facts";
    }

    void runOnOperation() override
    {
        auto            module = getOperation();
        mlir::OpBuilder builder(module.getContext());

        for (mlir::Operation& op : module.getBodyRegion().front())
        {
            if (op.getName().getStringRef() != "dsdl.schema" || op.getNumRegions() == 0 || op.getRegion(0).empty())
            {
                continue;
            }
            for (mlir::Operation& child : op.getRegion(0).front())
            {
                if (child.getName().getStringRef() != "dsdl.serialization_plan")
                {
                    continue;
                }

                const auto fixedSize = child.hasAttr("fixed_size");
                const auto sealed    = child.hasAttr("sealed");

                std::string reason;
                bool        hasPayloadFields = false;
                std::int64_t offsetBits = 0;

                if (child.getNumRegions() > 0 && !child.getRegion(0).empty())
                {
                    for (mlir::Operation& step : child.getRegion(0).front())
                    {
                        if (step.getName().getStringRef() == "dsdl.align")
                        {
                            const auto alignBits = nonNegative(intAttrOrDefault(&step, "bits", 1));
                            if (alignBits > 1)
                            {
                                const auto rem = offsetBits % alignBits;
                                if (rem != 0)
                                {
                                    offsetBits += (alignBits - rem);
                                }
                            }
                            continue;
                        }
                        if (step.getName().getStringRef() != "dsdl.io")
                        {
                            continue;
                        }
                        const auto kindAttr = step.getAttrOfType<mlir::StringAttr>("kind");
                        const auto kind     = kindAttr ? kindAttr.getValue() : llvm::StringRef("field");
                        const auto bitLength = nonNegative(intAttrOrDefault(&step, "bit_length", 0));
                        if (kind == "padding")
                        {
                            offsetBits += bitLength;
                            continue;
                        }

                        hasPayloadFields = true;
                        if ((offsetBits % 8) != 0)
                        {
                            reason = "unaligned-field";
                            break;
                        }
                        if (bitLength <= 0)
                        {
                            reason = "invalid-bit-length";
                            break;
                        }
                        if ((bitLength % 8) != 0)
                        {
                            reason = "sub-byte-field";
                            break;
                        }
                        const auto arrayKindAttr = step.getAttrOfType<mlir::StringAttr>("array_kind");
                        const auto arrayKind     = arrayKindAttr ? arrayKindAttr.getValue() : llvm::StringRef("none");
                        if (arrayKind == "variable_inclusive" || arrayKind == "variable_exclusive")
                        {
                            reason = "variable-array";
                            break;
                        }
                        const auto scalarCategoryAttr = step.getAttrOfType<mlir::StringAttr>("scalar_category");
                        const auto scalarCategory =
                            scalarCategoryAttr ? scalarCategoryAttr.getValue() : llvm::StringRef("void");
                        if (scalarCategory == "composite")
                        {
                            reason = "composite-field";
                            break;
                        }
                        if (scalarCategory == "float" && bitLength != 16 && bitLength != 32 && bitLength != 64)
                        {
                            reason = "unsupported-float-width";
                            break;
                        }
                        offsetBits += bitLength;
                    }
                }

                if (reason.empty() && !fixedSize)
                {
                    reason = "not-fixed-size";
                }
                if (reason.empty() && !sealed)
                {
                    reason = "not-sealed";
                }
                if (reason.empty() && child.hasAttr("is_union"))
                {
                    reason = "union-type";
                }
                if (reason.empty() && !hasPayloadFields)
                {
                    reason = "empty-layout";
                }

                const bool eligible = reason.empty();
                if (eligible)
                {
                    child.setAttr("zoh_alias_eligible", builder.getUnitAttr());
                    child.removeAttr("zoh_alias_reason");
                }
                else
                {
                    child.removeAttr("zoh_alias_eligible");
                    child.setAttr("zoh_alias_reason", builder.getStringAttr(reason));
                }
            }
        }
    }
};

// Validation-only pass: it checks the target-endianness attribute and stamps a
// legalized marker. It performs no byte reordering. The DSDL wire format is always
// little-endian, so per-target endianness handling lives in the emitted code (the
// `LLVMDSDL_TARGET_ENDIANNESS_BIG` conditional gates only the zero-copy view helpers).
struct DSDLEndianLegalizePass
    : public mlir::PassWrapper<DSDLEndianLegalizePass, mlir::OperationPass<mlir::ModuleOp>>
{
    llvm::StringRef getArgument() const final
    {
        return "dsdl-legalize-endianness";
    }
    llvm::StringRef getDescription() const final
    {
        return "Validate and stamp DSDL target endianness metadata (no byte reordering)";
    }

    void runOnOperation() override
    {
        auto            module = getOperation();
        mlir::OpBuilder builder(module.getContext());
        const auto      targetEndianness = module->getAttrOfType<mlir::StringAttr>("llvmdsdl.target_endianness");
        if (!targetEndianness)
        {
            module.emitError("missing required module attribute 'llvmdsdl.target_endianness'");
            signalPassFailure();
            return;
        }
        const auto endianValue = targetEndianness.getValue();
        if (endianValue != "little" && endianValue != "big")
        {
            module.emitError("unsupported module target endianness (expected 'little' or 'big')");
            signalPassFailure();
            return;
        }
        module->setAttr("llvmdsdl.target_endianness_legalized", builder.getUnitAttr());
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

std::unique_ptr<mlir::Pass> createDSDLAnnotateAliasabilityPass()
{
    return std::make_unique<AnnotateDSDLAliasabilityPass>();
}

std::unique_ptr<mlir::Pass> createDSDLEndianLegalizePass()
{
    return std::make_unique<DSDLEndianLegalizePass>();
}

void addOptimizeLoweredSerDesPipeline(mlir::OpPassManager& pm)
{
    auto& funcPM = pm.nest<mlir::func::FuncOp>();
    funcPM.addPass(mlir::createCanonicalizerPass());
    funcPM.addPass(mlir::createCSEPass());
}

void registerDSDLPasses()
{
    static bool once = false;
    if (once)
    {
        return;
    }
    once = true;
    static mlir::PassRegistration<LowerDSDLSerializationPass> reg;
    static mlir::PassRegistration<LowerDSDLExecPass>          regExec;
    static mlir::PassRegistration<AnnotateDSDLAliasabilityPass> regAlias;
    static mlir::PassRegistration<DSDLEndianLegalizePass>     regEndian;
    static mlir::PassPipelineRegistration<>
        optimizeLoweredSerDesPipeline("optimize-dsdl-lowered-serdes",
                                      "Apply semantics-preserving canonicalization and CSE to lowered DSDL SerDes IR",
                                      [](mlir::OpPassManager& pm) { addOptimizeLoweredSerDesPipeline(pm); });
    registerDSDLConvertPasses();
}

}  // namespace llvmdsdl
