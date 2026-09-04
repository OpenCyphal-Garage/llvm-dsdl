//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Converts plan bodies to EmitC, for the C backend.
///
/// `convert-dsdl-to-emitc` is the C target's answer to the operations `build-dsdl-plan-bodies`
/// produces: a member access becomes a member expression, a bit access a runtime call, and a
/// nested call the published symbol. It renders no body of its own.
///
/// The line-building concatenations here carry NOLINT for
/// performance-inefficient-string-concatenation. Each one spells out a line of generated
/// source, and an append sequence would cost the reader the line itself.
///
//===----------------------------------------------------------------------===//

#include <llvm/ADT/SmallVector.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/ErrorHandling.h>
#include <mlir/IR/BuiltinAttributes.h>
#include <mlir/IR/Diagnostics.h>
#include <mlir/IR/DialectRegistry.h>
#include <mlir/IR/Location.h>
#include <mlir/IR/Operation.h>
#include <mlir/IR/OperationSupport.h>
#include <mlir/IR/Region.h>
#include <mlir/IR/Value.h>
#include <mlir/Pass/PassRegistry.h>
#include <mlir/Transforms/DialectConversion.h>
#include <mlir/Rewrite/FrozenRewritePatternSet.h>
#include <mlir/Transforms/GreedyPatternRewriteDriver.h>
#include <mlir/Support/LLVM.h>
#include <cstdint>
#include <set>
#include <string>
#include <cstddef>
#include <optional>
#include <cstring>
#include <memory>
#include <utility>

#include <mlir/Dialect/Arith/IR/Arith.h>
#include <mlir/Dialect/SCF/IR/SCF.h>
#include "llvmdsdl/IR/DSDLDialect.h"
#include "llvmdsdl/IR/DSDLOps.h"
#include "llvmdsdl/IR/DSDLTypes.h"
#include "llvmdsdl/Transforms/LoweredSerDesContract.h"
#include "llvmdsdl/Transforms/LoweredSerDesContractValidation.h"
#include "llvmdsdl/Transforms/Passes.h"
#include "llvmdsdl/Transforms/PlanSteps.h"
#include "llvmdsdl/Support/DefinitionNaming.h"
#include <mlir/Dialect/EmitC/IR/EmitC.h>
#include <mlir/Dialect/Func/IR/FuncOps.h>
#include <mlir/IR/Builders.h>
#include <mlir/IR/BuiltinOps.h>
#include <mlir/Pass/Pass.h>

namespace llvmdsdl
{
namespace
{

constexpr llvm::StringRef kRuntimeCopyBits = "dsdl_runtime_copy_bits";

/// @brief Converts the dialect's pointer onto the C path's pointer.
///
/// The plan body is built once on `!dsdl.ptr`; this is the half of that bargain the C backend
/// pays. Object emission converts the same type to `!llvm.ptr`.
///
/// The conversion has to reach function signatures, not just operands, because `!dsdl.ptr` is
/// how a plan states what it was handed. An operand-only rewrite would leave the argument type
/// behind and no EmitC operation accepts it.
mlir::TypeConverter makeBitCopyTypeConverter()
{
    mlir::TypeConverter converter;
    converter.addConversion([](mlir::Type type) { return type; });
    converter.addConversion([](mlir::dsdl::OpaqueType named) -> mlir::Type {
        return mlir::emitc::OpaqueType::get(named.getContext(), named.getName());
    });
    converter.addConversion([&converter](mlir::dsdl::PtrType ptr) -> mlir::Type {
        return mlir::emitc::PointerType::get(converter.convertType(ptr.getPointee()));
    });
    return converter;
}

/// @brief Rewrites a bulk bit copy into `dsdl_runtime_copy_bits`.
struct BitWriteLowering final : public mlir::OpConversionPattern<mlir::dsdl::BitWriteOp>
{
    using mlir::OpConversionPattern<mlir::dsdl::BitWriteOp>::OpConversionPattern;

    mlir::LogicalResult matchAndRewrite(mlir::dsdl::BitWriteOp           op,
                                        OpAdaptor                        adaptor,
                                        mlir::ConversionPatternRewriter& rewriter) const override
    {
        rewriter.replaceOpWithNewOp<mlir::emitc::CallOpaqueOp>(op,
                                                               mlir::TypeRange{},
                                                               rewriter.getStringAttr(kRuntimeCopyBits),
                                                               mlir::ValueRange{adaptor.getDestination(),
                                                                                adaptor.getDestinationBitOffset(),
                                                                                adaptor.getWidth(),
                                                                                adaptor.getSource(),
                                                                                adaptor.getSourceBitOffset()});
        return mlir::success();
    }
};

/// @brief Rewrites a bulk bit read into `dsdl_runtime_get_bits`, which zero-extends a run
///        reaching past the buffer rather than refusing it.
struct BitReadLowering final : public mlir::OpConversionPattern<mlir::dsdl::BitReadOp>
{
    using mlir::OpConversionPattern<mlir::dsdl::BitReadOp>::OpConversionPattern;

    mlir::LogicalResult matchAndRewrite(mlir::dsdl::BitReadOp            op,
                                        OpAdaptor                        adaptor,
                                        mlir::ConversionPatternRewriter& rewriter) const override
    {
        rewriter.replaceOpWithNewOp<mlir::emitc::CallOpaqueOp>(op,
                                                               mlir::TypeRange{},
                                                               rewriter.getStringAttr("dsdl_runtime_get_bits"),
                                                               mlir::ValueRange{adaptor.getDestination(),
                                                                                adaptor.getBuffer(),
                                                                                adaptor.getBufferSizeBytes(),
                                                                                adaptor.getBitOffset(),
                                                                                adaptor.getWidth()});
        return mlir::success();
    }
};

struct IsNullLowering final : public mlir::OpConversionPattern<mlir::dsdl::IsNullOp>
{
    using mlir::OpConversionPattern<mlir::dsdl::IsNullOp>::OpConversionPattern;

    mlir::LogicalResult matchAndRewrite(mlir::dsdl::IsNullOp             op,
                                        OpAdaptor                        adaptor,
                                        mlir::ConversionPatternRewriter& rewriter) const override
    {
        const mlir::Type pointerType = adaptor.getPointer().getType();
        auto null = mlir::emitc::ConstantOp::create(rewriter,
                                                    op.getLoc(),
                                                    pointerType,
                                                    mlir::emitc::OpaqueAttr::get(rewriter.getContext(), "NULL"));
        rewriter.replaceOpWithNewOp<mlir::emitc::CmpOp>(op,
                                                        rewriter.getI1Type(),
                                                        mlir::emitc::CmpPredicate::eq,
                                                        adaptor.getPointer(),
                                                        null);
        return mlir::success();
    }
};

/// @brief Addresses `pointer[0]`, which is how EmitC spells a dereference that can be assigned.
///
/// `emitc.apply "*"` yields an rvalue and cannot be written through, so both directions go via
/// a zero subscript.
mlir::Value scalarSlot(mlir::ConversionPatternRewriter& rewriter, mlir::Location loc, mlir::Value pointer)
{
    auto indexType = mlir::emitc::OpaqueType::get(rewriter.getContext(), "size_t");
    auto zero      = mlir::emitc::ConstantOp::create(rewriter,
                                                     loc,
                                                     indexType,
                                                     mlir::emitc::OpaqueAttr::get(rewriter.getContext(), "0"));
    return mlir::emitc::SubscriptOp::create(rewriter,
                                            loc,
                                            mlir::cast<mlir::TypedValue<mlir::emitc::PointerType>>(pointer),
                                            zero);
}

struct BufferOrEmptyLowering final : public mlir::OpConversionPattern<mlir::dsdl::BufferOrEmptyOp>
{
    using mlir::OpConversionPattern<mlir::dsdl::BufferOrEmptyOp>::OpConversionPattern;

    mlir::LogicalResult matchAndRewrite(mlir::dsdl::BufferOrEmptyOp      op,
                                        OpAdaptor                        adaptor,
                                        mlir::ConversionPatternRewriter& rewriter) const override
    {
        const mlir::Location loc         = op.getLoc();
        const mlir::Type     pointerType = adaptor.getBuffer().getType();
        auto null   = mlir::emitc::ConstantOp::create(rewriter,
                                                      loc,
                                                      pointerType,
                                                      mlir::emitc::OpaqueAttr::get(rewriter.getContext(), "NULL"));
        auto isNull = mlir::emitc::CmpOp::create(rewriter,
                                                 loc,
                                                 rewriter.getI1Type(),
                                                 mlir::emitc::CmpPredicate::eq,
                                                 adaptor.getBuffer(),
                                                 null);
        // A string literal is the shortest expression of "somewhere readable holding no
        // bytes" that C guarantees; the pointer is never dereferenced past its terminator
        // because the capacity travelling with it is zero.
        auto empty = mlir::emitc::ConstantOp::create(rewriter,
                                                     loc,
                                                     pointerType,
                                                     mlir::emitc::OpaqueAttr::get(rewriter.getContext(),
                                                                                  "(const uint8_t*)\"\""));
        rewriter.replaceOpWithNewOp<mlir::emitc::ConditionalOp>(op, pointerType, isNull, empty, adaptor.getBuffer());
        return mlir::success();
    }
};

struct LoadScalarLowering final : public mlir::OpConversionPattern<mlir::dsdl::LoadScalarOp>
{
    using mlir::OpConversionPattern<mlir::dsdl::LoadScalarOp>::OpConversionPattern;

    mlir::LogicalResult matchAndRewrite(mlir::dsdl::LoadScalarOp         op,
                                        OpAdaptor                        adaptor,
                                        mlir::ConversionPatternRewriter& rewriter) const override
    {
        const mlir::Location loc    = op.getLoc();
        const mlir::Value    slot   = scalarSlot(rewriter, loc, adaptor.getPointer());
        const mlir::Type     stored = mlir::cast<mlir::emitc::LValueType>(slot.getType()).getValueType();
        mlir::Value          loaded = mlir::emitc::LoadOp::create(rewriter, loc, stored, slot);
        if (stored != op.getValue().getType())
        {
            loaded = mlir::emitc::CastOp::create(rewriter, loc, op.getValue().getType(), loaded);
        }
        rewriter.replaceOp(op, loaded);
        return mlir::success();
    }
};

struct StoreScalarLowering final : public mlir::OpConversionPattern<mlir::dsdl::StoreScalarOp>
{
    using mlir::OpConversionPattern<mlir::dsdl::StoreScalarOp>::OpConversionPattern;

    mlir::LogicalResult matchAndRewrite(mlir::dsdl::StoreScalarOp        op,
                                        OpAdaptor                        adaptor,
                                        mlir::ConversionPatternRewriter& rewriter) const override
    {
        const mlir::Location loc    = op.getLoc();
        const mlir::Value    slot   = scalarSlot(rewriter, loc, adaptor.getPointer());
        const mlir::Type     stored = mlir::cast<mlir::emitc::LValueType>(slot.getType()).getValueType();
        mlir::Value          value  = adaptor.getValue();
        if (stored != value.getType())
        {
            value = mlir::emitc::CastOp::create(rewriter, loc, stored, value);
        }
        rewriter.replaceOpWithNewOp<mlir::emitc::AssignOp>(op, slot, value);
        return mlir::success();
    }
};

/// @brief Names the runtime primitive a scalar access lowers to.
///
/// The runtime spells one primitive per value shape rather than one generic call, so the
/// selection is by value type, width and signedness together. Widths that are not a standard
/// integer size go through the `xx` primitives, which take the width as an argument.
std::string runtimePrimitiveName(const bool write, mlir::Type valueType, const std::int64_t width, const bool isSigned)
{
    if (mlir::isa<mlir::FloatType>(valueType))
    {
        // Selected by the field's width, not the value's. A float16 field travels as a C
        // `float` and is written by set_f16; choosing on the carrier would write 32 bits into
        // a 16-bit slot and report the buffer too small.
        return std::string(write ? "dsdl_runtime_set_f" : "dsdl_runtime_get_f") + std::to_string(width);
    }
    if (width == 1 && !isSigned)
    {
        return write ? "dsdl_runtime_set_bit" : "dsdl_runtime_get_bit";
    }
    if (write)
    {
        return isSigned ? "dsdl_runtime_set_ixx" : "dsdl_runtime_set_uxx";
    }
    // Reads answer in a concrete width, so the primitive is chosen by the smallest standard
    // integer that holds the field rather than by the field width itself.
    const unsigned holder = holderWidthFor(width);
    return std::string(isSigned ? "dsdl_runtime_get_i" : "dsdl_runtime_get_u") + std::to_string(holder);
}

/// @brief Materialises an lvalue holding @p pointer.
///
/// `emitc.member_of_ptr` takes an lvalue of pointer type, and a function parameter is an
/// rvalue, so a member access needs the pointer parked in a variable first.
mlir::Value pointerSlot(mlir::ConversionPatternRewriter& rewriter, mlir::Location loc, mlir::Value pointer)
{
    auto slotType = mlir::emitc::LValueType::get(pointer.getType());
    auto slot     = mlir::emitc::VariableOp::create(rewriter,
                                                    loc,
                                                    slotType,
                                                    mlir::emitc::OpaqueAttr::get(rewriter.getContext(), ""));
    mlir::emitc::AssignOp::create(rewriter, loc, slot, pointer);
    return slot;
}

struct WriteBitsLowering final : public mlir::OpConversionPattern<mlir::dsdl::WriteBitsOp>
{
    using mlir::OpConversionPattern<mlir::dsdl::WriteBitsOp>::OpConversionPattern;

    mlir::LogicalResult matchAndRewrite(mlir::dsdl::WriteBitsOp          op,
                                        OpAdaptor                        adaptor,
                                        mlir::ConversionPatternRewriter& rewriter) const override
    {
        const mlir::Location loc      = op.getLoc();
        const auto           width    = static_cast<std::int64_t>(op.getWidth());
        const bool           isSigned = op.getIsSigned();
        const std::string    callee   = runtimePrimitiveName(true, op.getValue().getType(), width, isSigned);

        mlir::SmallVector<mlir::Value, 5> args{adaptor.getBuffer(),
                                               adaptor.getBufferSizeBytes(),
                                               adaptor.getBitOffset(),
                                               adaptor.getValue()};
        // Only the width-carrying integer primitives take a length; the bit and float ones
        // encode it in the name they were selected by.
        if (callee == "dsdl_runtime_set_uxx" || callee == "dsdl_runtime_set_ixx")
        {
            args.push_back(mlir::emitc::ConstantOp::create(rewriter,
                                                           loc,
                                                           rewriter.getIntegerType(8),
                                                           rewriter.getI8IntegerAttr(static_cast<std::int8_t>(width))));
        }
        rewriter.replaceOpWithNewOp<mlir::emitc::CallOpaqueOp>(op,
                                                               mlir::TypeRange{rewriter.getIntegerType(8)},
                                                               rewriter.getStringAttr(callee),
                                                               args);
        return mlir::success();
    }
};

struct ReadBitsLowering final : public mlir::OpConversionPattern<mlir::dsdl::ReadBitsOp>
{
    using mlir::OpConversionPattern<mlir::dsdl::ReadBitsOp>::OpConversionPattern;

    mlir::LogicalResult matchAndRewrite(mlir::dsdl::ReadBitsOp           op,
                                        OpAdaptor                        adaptor,
                                        mlir::ConversionPatternRewriter& rewriter) const override
    {
        const mlir::Location loc    = op.getLoc();
        const auto           width  = static_cast<std::int64_t>(op.getWidth());
        const std::string    callee = runtimePrimitiveName(false, op.getValue().getType(), width, op.getIsSigned());

        mlir::SmallVector<mlir::Value, 4> args{adaptor.getBuffer(),
                                               adaptor.getBufferSizeBytes(),
                                               adaptor.getBitOffset()};
        if (callee.contains("_get_u") || callee.contains("_get_i"))
        {
            args.push_back(mlir::emitc::ConstantOp::create(rewriter,
                                                           loc,
                                                           rewriter.getIntegerType(8),
                                                           rewriter.getI8IntegerAttr(static_cast<std::int8_t>(width))));
        }
        rewriter.replaceOpWithNewOp<mlir::emitc::CallOpaqueOp>(op,
                                                               mlir::TypeRange{op.getValue().getType()},
                                                               rewriter.getStringAttr(callee),
                                                               args);
        return mlir::success();
    }
};

/// @brief Walks a member path, answering the lvalue the last name designates.
///
/// The first hop leaves a pointer and uses `member_of_ptr`; the rest are within a value and
/// use `member`. Every hop but the last lands on a struct whose type this pass does not know,
/// so the intermediate lvalues are opaque -- C resolves them, and nothing here has to.
mlir::Value walkMemberPath(mlir::ConversionPatternRewriter& rewriter,
                           mlir::Location                   loc,
                           mlir::Value                      object,
                           mlir::ArrayAttr                  path,
                           mlir::Type                       leafType)
{
    mlir::Value cursor = pointerSlot(rewriter, loc, object);
    for (std::size_t hop = 0; hop < path.size(); ++hop)
    {
        const bool       last    = (hop + 1 == path.size());
        const mlir::Type hopType = last ? leafType : mlir::emitc::OpaqueType::get(rewriter.getContext(), "struct");
        auto             member  = mlir::cast<mlir::StringAttr>(path[hop]);
        if (hop == 0)
        {
            cursor = mlir::emitc::MemberOfPtrOp::create(rewriter,
                                                        loc,
                                                        mlir::emitc::LValueType::get(hopType),
                                                        member,
                                                        cursor);
        }
        else
        {
            cursor =
                mlir::emitc::MemberOp::create(rewriter, loc, mlir::emitc::LValueType::get(hopType), member, cursor);
        }
    }
    return cursor;
}

struct LoadMemberLowering final : public mlir::OpConversionPattern<mlir::dsdl::LoadMemberOp>
{
    using mlir::OpConversionPattern<mlir::dsdl::LoadMemberOp>::OpConversionPattern;

    mlir::LogicalResult matchAndRewrite(mlir::dsdl::LoadMemberOp         op,
                                        OpAdaptor                        adaptor,
                                        mlir::ConversionPatternRewriter& rewriter) const override
    {
        const mlir::Value slot =
            walkMemberPath(rewriter, op.getLoc(), adaptor.getObject(), op.getPath(), op.getValue().getType());
        rewriter.replaceOpWithNewOp<mlir::emitc::LoadOp>(op, op.getValue().getType(), slot);
        return mlir::success();
    }
};

struct StoreMemberLowering final : public mlir::OpConversionPattern<mlir::dsdl::StoreMemberOp>
{
    using mlir::OpConversionPattern<mlir::dsdl::StoreMemberOp>::OpConversionPattern;

    mlir::LogicalResult matchAndRewrite(mlir::dsdl::StoreMemberOp        op,
                                        OpAdaptor                        adaptor,
                                        mlir::ConversionPatternRewriter& rewriter) const override
    {
        const mlir::Value slot =
            walkMemberPath(rewriter, op.getLoc(), adaptor.getObject(), op.getPath(), adaptor.getValue().getType());
        rewriter.replaceOpWithNewOp<mlir::emitc::AssignOp>(op, slot, adaptor.getValue());
        return mlir::success();
    }
};

/// @brief Addresses one element of the array storage a path reaches.
mlir::Value elementSlot(mlir::ConversionPatternRewriter& rewriter,
                        mlir::Location                   loc,
                        mlir::Value                      object,
                        mlir::ArrayAttr                  path,
                        mlir::Value                      index,
                        llvm::StringRef                  elementTypeName)
{
    // The member is reached at whatever qualification it is declared with -- a serializer
    // holds the object by pointer-to-const -- and then the pointer is taken unqualified. The
    // element read out of it is assigned to a variable, and EmitC declares its variables
    // before assigning them, which a const-qualified declaration does not survive.
    auto*             ctx         = rewriter.getContext();
    const std::string bare        = elementTypeName.starts_with("const ")
                                        ? elementTypeName.drop_front(std::strlen("const ")).str()
                                        : elementTypeName.str();
    auto              declared    = mlir::emitc::PointerType::get(mlir::emitc::OpaqueType::get(ctx, elementTypeName));
    auto              unqualified = mlir::emitc::PointerType::get(mlir::emitc::OpaqueType::get(ctx, bare));

    auto        storageSlot = walkMemberPath(rewriter, loc, object, path, declared);
    mlir::Value storage     = mlir::emitc::LoadOp::create(rewriter, loc, declared, storageSlot);
    if (declared != unqualified)
    {
        storage = mlir::emitc::CastOp::create(rewriter, loc, unqualified, storage);
    }
    return mlir::emitc::SubscriptOp::create(rewriter,
                                            loc,
                                            mlir::cast<mlir::TypedValue<mlir::emitc::PointerType>>(storage),
                                            index);
}

struct LoadElementLowering final : public mlir::OpConversionPattern<mlir::dsdl::LoadElementOp>
{
    using mlir::OpConversionPattern<mlir::dsdl::LoadElementOp>::OpConversionPattern;

    mlir::LogicalResult matchAndRewrite(mlir::dsdl::LoadElementOp        op,
                                        OpAdaptor                        adaptor,
                                        mlir::ConversionPatternRewriter& rewriter) const override
    {
        const mlir::Location loc = op.getLoc();
        const mlir::Value    slot =
            elementSlot(rewriter, loc, adaptor.getObject(), op.getPath(), adaptor.getIndex(), op.getElementType());
        const mlir::Type stored = mlir::cast<mlir::emitc::LValueType>(slot.getType()).getValueType();
        mlir::Value      loaded = mlir::emitc::LoadOp::create(rewriter, loc, stored, slot);
        if (stored != op.getValue().getType())
        {
            loaded = mlir::emitc::CastOp::create(rewriter, loc, op.getValue().getType(), loaded);
        }
        rewriter.replaceOp(op, loaded);
        return mlir::success();
    }
};

struct StoreElementLowering final : public mlir::OpConversionPattern<mlir::dsdl::StoreElementOp>
{
    using mlir::OpConversionPattern<mlir::dsdl::StoreElementOp>::OpConversionPattern;

    mlir::LogicalResult matchAndRewrite(mlir::dsdl::StoreElementOp       op,
                                        OpAdaptor                        adaptor,
                                        mlir::ConversionPatternRewriter& rewriter) const override
    {
        const mlir::Location loc = op.getLoc();
        const mlir::Value    slot =
            elementSlot(rewriter, loc, adaptor.getObject(), op.getPath(), adaptor.getIndex(), op.getElementType());
        const mlir::Type stored = mlir::cast<mlir::emitc::LValueType>(slot.getType()).getValueType();
        mlir::Value      value  = adaptor.getValue();
        if (stored != value.getType())
        {
            value = mlir::emitc::CastOp::create(rewriter, loc, stored, value);
        }
        rewriter.replaceOpWithNewOp<mlir::emitc::AssignOp>(op, slot, value);
        return mlir::success();
    }
};

/// @brief Takes the address of an lvalue, which is how a plan hands storage to a callee.
mlir::Value addressOf(mlir::ConversionPatternRewriter& rewriter,
                      mlir::Location                   loc,
                      mlir::Value                      lvalue,
                      mlir::Type                       pointerType)
{
    return mlir::emitc::ApplyOp::create(rewriter, loc, pointerType, "&", lvalue);
}

struct MemberAddrLowering final : public mlir::OpConversionPattern<mlir::dsdl::MemberAddrOp>
{
    using mlir::OpConversionPattern<mlir::dsdl::MemberAddrOp>::OpConversionPattern;

    mlir::LogicalResult matchAndRewrite(mlir::dsdl::MemberAddrOp         op,
                                        OpAdaptor                        adaptor,
                                        mlir::ConversionPatternRewriter& rewriter) const override
    {
        auto pointerType =
            mlir::cast<mlir::emitc::PointerType>(getTypeConverter()->convertType(op.getAddress().getType()));
        const mlir::Value slot =
            walkMemberPath(rewriter, op.getLoc(), adaptor.getObject(), op.getPath(), pointerType.getPointee());
        rewriter.replaceOp(op, addressOf(rewriter, op.getLoc(), slot, pointerType));
        return mlir::success();
    }
};

struct ElementAddrLowering final : public mlir::OpConversionPattern<mlir::dsdl::ElementAddrOp>
{
    using mlir::OpConversionPattern<mlir::dsdl::ElementAddrOp>::OpConversionPattern;

    mlir::LogicalResult matchAndRewrite(mlir::dsdl::ElementAddrOp        op,
                                        OpAdaptor                        adaptor,
                                        mlir::ConversionPatternRewriter& rewriter) const override
    {
        const mlir::Location loc = op.getLoc();
        auto                 pointerType =
            mlir::cast<mlir::emitc::PointerType>(getTypeConverter()->convertType(op.getAddress().getType()));
        const mlir::Value slot =
            elementSlot(rewriter, loc, adaptor.getObject(), op.getPath(), adaptor.getIndex(), op.getElementType());
        rewriter.replaceOp(op, addressOf(rewriter, loc, slot, pointerType));
        return mlir::success();
    }
};

struct BufferAtLowering final : public mlir::OpConversionPattern<mlir::dsdl::BufferAtOp>
{
    using mlir::OpConversionPattern<mlir::dsdl::BufferAtOp>::OpConversionPattern;

    mlir::LogicalResult matchAndRewrite(mlir::dsdl::BufferAtOp           op,
                                        OpAdaptor                        adaptor,
                                        mlir::ConversionPatternRewriter& rewriter) const override
    {
        const mlir::Location loc = op.getLoc();
        auto                 pointerType =
            mlir::cast<mlir::emitc::PointerType>(getTypeConverter()->convertType(op.getAddress().getType()));
        auto element = mlir::emitc::SubscriptOp::create(rewriter,
                                                        loc,
                                                        mlir::cast<mlir::TypedValue<mlir::emitc::PointerType>>(
                                                            adaptor.getBuffer()),
                                                        adaptor.getByteOffset());
        rewriter.replaceOp(op, addressOf(rewriter, loc, element, pointerType));
        return mlir::success();
    }
};

struct LocalLowering final : public mlir::OpConversionPattern<mlir::dsdl::LocalOp>
{
    using mlir::OpConversionPattern<mlir::dsdl::LocalOp>::OpConversionPattern;

    mlir::LogicalResult matchAndRewrite(mlir::dsdl::LocalOp              op,
                                        OpAdaptor                        adaptor,
                                        mlir::ConversionPatternRewriter& rewriter) const override
    {
        const mlir::Location loc = op.getLoc();
        auto                 pointerType =
            mlir::cast<mlir::emitc::PointerType>(getTypeConverter()->convertType(op.getAddress().getType()));
        const mlir::Type stored = pointerType.getPointee();

        auto        slot = mlir::emitc::VariableOp::create(rewriter,
                                                           loc,
                                                           mlir::emitc::LValueType::get(stored),
                                                           mlir::emitc::OpaqueAttr::get(rewriter.getContext(), ""));
        mlir::Value init = adaptor.getInit();
        if (init.getType() != stored)
        {
            init = mlir::emitc::CastOp::create(rewriter, loc, stored, init);
        }
        mlir::emitc::AssignOp::create(rewriter, loc, slot, init);
        rewriter.replaceOp(op, addressOf(rewriter, loc, slot, pointerType));
        return mlir::success();
    }
};

struct CallSerdesLowering final : public mlir::OpConversionPattern<mlir::dsdl::CallSerdesOp>
{
    using mlir::OpConversionPattern<mlir::dsdl::CallSerdesOp>::OpConversionPattern;

    mlir::LogicalResult matchAndRewrite(mlir::dsdl::CallSerdesOp         op,
                                        OpAdaptor                        adaptor,
                                        mlir::ConversionPatternRewriter& rewriter) const override
    {
        rewriter.replaceOpWithNewOp<mlir::emitc::CallOpaqueOp>(op,
                                                               mlir::TypeRange{rewriter.getIntegerType(8)},
                                                               op.getCalleeAttr(),
                                                               mlir::ValueRange{adaptor.getObject(),
                                                                                adaptor.getBuffer(),
                                                                                adaptor.getSize()});
        return mlir::success();
    }
};

struct ConvertDSDLToEmitCPass : public mlir::PassWrapper<ConvertDSDLToEmitCPass, mlir::OperationPass<mlir::ModuleOp>>
{
    llvm::StringRef getArgument() const final
    {
        return "convert-dsdl-to-emitc";
    }
    llvm::StringRef getDescription() const final
    {
        return "Lower DSDL dialect schema ops into Func/Arith ops for EmitC lowering";
    }
    void getDependentDialects(mlir::DialectRegistry& registry) const override
    {
        registry.insert<mlir::emitc::EmitCDialect>();
    }

    /// @brief Converts plan operations into their C spellings.
    ///
    /// Runs before the schema walk, for a module that already carries a plan body, and again
    /// after it, for the bodies the walk builds. The second run is a no-op when the first
    /// already emptied the module of them.
    mlir::LogicalResult lowerPlanOperations(mlir::ModuleOp module)
    {
        mlir::TypeConverter     converter = makeBitCopyTypeConverter();
        mlir::RewritePatternSet patterns(&getContext());
        patterns.add<BitWriteLowering,
                     BitReadLowering,
                     WriteBitsLowering,
                     ReadBitsLowering,
                     LoadMemberLowering,
                     StoreMemberLowering,
                     LoadElementLowering,
                     StoreElementLowering,
                     MemberAddrLowering,
                     BufferAtLowering,
                     ElementAddrLowering,
                     LocalLowering,
                     CallSerdesLowering,
                     IsNullLowering,
                     BufferOrEmptyLowering,
                     LoadScalarLowering,
                     StoreScalarLowering>(converter, &getContext());
        mlir::populateFunctionOpInterfaceTypeConversionPattern<mlir::func::FuncOp>(patterns, converter);

        mlir::ConversionTarget target(getContext());
        target.addLegalDialect<mlir::emitc::EmitCDialect,
                               mlir::arith::ArithDialect,
                               mlir::func::FuncDialect,
                               mlir::scf::SCFDialect>();
        target.addLegalDialect<mlir::dsdl::DSDLDialect>();
        target.addIllegalOp<mlir::dsdl::BitWriteOp,
                            mlir::dsdl::BitReadOp,
                            mlir::dsdl::WriteBitsOp,
                            mlir::dsdl::ReadBitsOp,
                            mlir::dsdl::LoadMemberOp,
                            mlir::dsdl::StoreMemberOp,
                            mlir::dsdl::LoadElementOp,
                            mlir::dsdl::StoreElementOp,
                            mlir::dsdl::MemberAddrOp,
                            mlir::dsdl::BufferAtOp,
                            mlir::dsdl::ElementAddrOp,
                            mlir::dsdl::LocalOp,
                            mlir::dsdl::CallSerdesOp,
                            mlir::dsdl::IsNullOp,
                            mlir::dsdl::BufferOrEmptyOp,
                            mlir::dsdl::LoadScalarOp,
                            mlir::dsdl::StoreScalarOp>();
        target.addDynamicallyLegalOp<mlir::func::FuncOp>(
            [&converter](mlir::func::FuncOp fn) { return converter.isSignatureLegal(fn.getFunctionType()); });

        if (mlir::failed(mlir::applyPartialConversion(module, target, std::move(patterns))))
        {
            module.emitError("failed to lower dsdl plan operations to emitc");
            signalPassFailure();
            return mlir::failure();
        }
        return mlir::success();
    }

    // NOLINTNEXTLINE(misc-override-with-different-visibility) -- MLIR declares passes this way.
    void runOnOperation() override
    {
        auto module = getOperation();
        // Whether the generated headers can be included. A body names the struct its header
        // declares either way; only the includes depend on this.
        const bool headersAvailable = module->hasAttr("llvmdsdl.headers_available");
        auto&      body             = module.getBodyRegion().front();

        if (mlir::failed(lowerPlanOperations(module)))
        {
            return;
        }

        const auto schemaOps = llvm::to_vector(body.getOps<mlir::dsdl::SchemaOp>());
        if (schemaOps.empty())
        {
            return;
        }
        if (const auto envelopeViolation = findLoweredContractEnvelopeViolation(module.getOperation()))
        {
            switch (envelopeViolation->kind)
            {
            case LoweredContractEnvelopeViolationKind::MissingVersion:
                module.emitError("lowered SerDes contract missing module attribute '" +
                                 std::string(kLoweredSerDesContractVersionAttr) +
                                 "'; run lower-dsdl-exec before "
                                 "convert-dsdl-to-emitc");
                break;
            case LoweredContractEnvelopeViolationKind::UnsupportedMajorVersion:
                module.emitError(
                    "unsupported lowered SerDes contract major version: " +
                    loweredSerDesUnsupportedMajorVersionDiagnosticDetail(envelopeViolation->encodedVersion) +
                    "; run matching lower-dsdl-exec before convert-dsdl-to-emitc");
                break;
            case LoweredContractEnvelopeViolationKind::ProducerMismatch:
                module.emitError("lowered SerDes contract producer mismatch: expected '" +
                                 std::string(kLoweredSerDesContractProducer) +
                                 "'; run lower-dsdl-exec before "
                                 "convert-dsdl-to-emitc");
                break;
            }
            signalPassFailure();
            return;
        }

        std::set<std::string> forwardDeclaredTypes;
        std::set<std::string> capacityCheckSymbols;
        std::set<std::string> unionTagValidateSymbols;
        std::set<std::string> unionTagIoHelperSymbols;
        std::set<std::string> scalarUnsignedHelperSymbols;
        std::set<std::string> scalarSignedHelperSymbols;
        std::set<std::string> scalarFloatHelperSymbols;
        std::set<std::string> arrayLengthPrefixHelperSymbols;
        std::set<std::string> arrayLengthValidateSymbols;
        std::set<std::string> delimiterValidateSymbols;
        std::set<std::string> includedHeaders;

        for (mlir::dsdl::SchemaOp schema : schemaOps)
        {
            const std::string headerPath = schema.getHeaderPath().value_or(llvm::StringRef{}).str();
            if (schema.getBody().empty())
            {
                continue;
            }

            for (mlir::dsdl::SerializationPlanOp child :
                 schema.getBody().front().getOps<mlir::dsdl::SerializationPlanOp>())
            {
                if (const auto envelopeViolation = findLoweredContractEnvelopeViolation(child.getOperation()))
                {
                    switch (envelopeViolation->kind)
                    {
                    case LoweredContractEnvelopeViolationKind::MissingVersion:
                        child.emitOpError("missing lowered contract version; run "
                                          "lower-dsdl-exec before convert-dsdl-to-emitc");
                        break;
                    case LoweredContractEnvelopeViolationKind::UnsupportedMajorVersion:
                        child.emitOpError(
                            "unsupported lowered contract major version: " +
                            loweredSerDesUnsupportedMajorVersionDiagnosticDetail(envelopeViolation->encodedVersion) +
                            "; run matching lower-dsdl-exec before convert-dsdl-to-emitc");
                        break;
                    case LoweredContractEnvelopeViolationKind::ProducerMismatch:
                        child.emitOpError("missing lowered contract producer marker; run "
                                          "lower-dsdl-exec before convert-dsdl-to-emitc");
                        break;
                    }
                    signalPassFailure();
                    return;
                }
                if (const auto violation = findLoweredPlanContractViolation(module, child.getOperation()))
                {
                    violation->operation->emitOpError(violation->message);
                    signalPassFailure();
                    return;
                }
                const std::string section = child.getSection().value_or(llvm::StringRef{}).str();
                const std::string fnStem  = schema.getSymName().str() + renderSectionSymbolSuffix(section);
                const std::string capacityCheckSymbol =
                    child.getLoweredCapacityCheckHelper().value_or(llvm::StringRef{}).str();
                const std::string cTypeName = child.getCTypeName().str();
                if (!cTypeName.empty())
                {
                    forwardDeclaredTypes.insert(cTypeName);
                }
                const auto steps = collectPlanSteps(child);
                for (const auto& step : steps)
                {
                    // C member names are the C backend's to decide, not lowering's: it stamps
                    // `c_name` onto its own clone of the schema so that the struct declaration and
                    // the references below cannot disagree. Reaching here without one means the
                    // schema came from somewhere that did not, and every member reference this plan
                    // emits would name nothing.
                    if ((step.kind == PlanStepKind::Field) && step.cName.empty())
                    {
                        child.emitOpError("field step '" + step.name + "' has no 'c_name' attribute");
                        signalPassFailure();
                        return;
                    }
                    if (!step.serUnsignedHelper.empty())
                    {
                        if (!module.lookupSymbol<mlir::func::FuncOp>(step.serUnsignedHelper))
                        {
                            child.emitOpError("missing lowered scalar helper symbol: " + step.serUnsignedHelper);
                            signalPassFailure();
                            return;
                        }
                        scalarUnsignedHelperSymbols.insert(step.serUnsignedHelper);
                    }
                    if (!step.deserUnsignedHelper.empty())
                    {
                        if (!module.lookupSymbol<mlir::func::FuncOp>(step.deserUnsignedHelper))
                        {
                            child.emitOpError("missing lowered scalar helper symbol: " + step.deserUnsignedHelper);
                            signalPassFailure();
                            return;
                        }
                        scalarUnsignedHelperSymbols.insert(step.deserUnsignedHelper);
                    }
                    if (!step.serSignedHelper.empty())
                    {
                        if (!module.lookupSymbol<mlir::func::FuncOp>(step.serSignedHelper))
                        {
                            child.emitOpError("missing lowered scalar helper symbol: " + step.serSignedHelper);
                            signalPassFailure();
                            return;
                        }
                        scalarSignedHelperSymbols.insert(step.serSignedHelper);
                    }
                    if (!step.deserSignedHelper.empty())
                    {
                        if (!module.lookupSymbol<mlir::func::FuncOp>(step.deserSignedHelper))
                        {
                            child.emitOpError("missing lowered scalar helper symbol: " + step.deserSignedHelper);
                            signalPassFailure();
                            return;
                        }
                        scalarSignedHelperSymbols.insert(step.deserSignedHelper);
                    }
                    if (!step.serFloatHelper.empty())
                    {
                        if (!module.lookupSymbol<mlir::func::FuncOp>(step.serFloatHelper))
                        {
                            child.emitOpError("missing lowered scalar helper symbol: " + step.serFloatHelper);
                            signalPassFailure();
                            return;
                        }
                        scalarFloatHelperSymbols.insert(step.serFloatHelper);
                    }
                    if (!step.deserFloatHelper.empty())
                    {
                        if (!module.lookupSymbol<mlir::func::FuncOp>(step.deserFloatHelper))
                        {
                            child.emitOpError("missing lowered scalar helper symbol: " + step.deserFloatHelper);
                            signalPassFailure();
                            return;
                        }
                        scalarFloatHelperSymbols.insert(step.deserFloatHelper);
                    }
                    if (!step.serArrayLengthPrefixHelper.empty())
                    {
                        if (!module.lookupSymbol<mlir::func::FuncOp>(step.serArrayLengthPrefixHelper))
                        {
                            child.emitOpError("missing lowered array-length-prefix helper symbol: " +
                                              step.serArrayLengthPrefixHelper);
                            signalPassFailure();
                            return;
                        }
                        arrayLengthPrefixHelperSymbols.insert(step.serArrayLengthPrefixHelper);
                    }
                    if (!step.deserArrayLengthPrefixHelper.empty())
                    {
                        if (!module.lookupSymbol<mlir::func::FuncOp>(step.deserArrayLengthPrefixHelper))
                        {
                            child.emitOpError("missing lowered array-length-prefix helper symbol: " +
                                              step.deserArrayLengthPrefixHelper);
                            signalPassFailure();
                            return;
                        }
                        arrayLengthPrefixHelperSymbols.insert(step.deserArrayLengthPrefixHelper);
                    }
                    if (!step.arrayLengthValidateHelper.empty())
                    {
                        if (!module.lookupSymbol<mlir::func::FuncOp>(step.arrayLengthValidateHelper))
                        {
                            child.emitOpError("missing lowered array-length helper symbol: " +
                                              step.arrayLengthValidateHelper);
                            signalPassFailure();
                            return;
                        }
                        arrayLengthValidateSymbols.insert(step.arrayLengthValidateHelper);
                    }
                    if (!step.delimiterValidateHelper.empty())
                    {
                        if (!module.lookupSymbol<mlir::func::FuncOp>(step.delimiterValidateHelper))
                        {
                            child.emitOpError("missing lowered delimiter helper symbol: " +
                                              step.delimiterValidateHelper);
                            signalPassFailure();
                            return;
                        }
                        delimiterValidateSymbols.insert(step.delimiterValidateHelper);
                    }
                }
                if (child.getIsUnion())
                {
                    const auto validate = child.getLoweredUnionTagValidateHelperAttr();
                    const auto serTag   = child.getLoweredSerUnionTagHelperAttr();
                    const auto deserTag = child.getLoweredDeserUnionTagHelperAttr();
                    if (validate)
                    {
                        unionTagValidateSymbols.insert(validate.getValue().str());
                    }
                    if (serTag)
                    {
                        unionTagIoHelperSymbols.insert(serTag.getValue().str());
                    }
                    if (deserTag)
                    {
                        unionTagIoHelperSymbols.insert(deserTag.getValue().str());
                    }
                }
                capacityCheckSymbols.insert(capacityCheckSymbol);
                if (!headerPath.empty())
                {
                    includedHeaders.insert(headerPath);
                }

                // The bodies are operations by the time this pass runs, or they are absent.
                for (const char* direction : {"serialize", "deserialize"})
                {
                    const std::string body = fnStem + "__" + direction + "_ir_";
                    if (!module.lookupSymbol<mlir::func::FuncOp>(body))
                    {
                        child.emitOpError(std::string("no ") + direction +
                                          " body was built for this plan; run build-dsdl-plan-bodies before "
                                          "convert-dsdl-to-emitc");
                        signalPassFailure();
                        return;
                    }
                }
            }
        }

        mlir::OpBuilder builder(module.getContext());
        builder.setInsertionPointToStart(&body);
        const mlir::Location loc = builder.getUnknownLoc();
        mlir::emitc::VerbatimOp::create(builder, loc, "#include <stddef.h>");
        mlir::emitc::VerbatimOp::create(builder, loc, "#include <stdint.h>");
        mlir::emitc::VerbatimOp::create(builder, loc, "#include \"dsdl_runtime.h\"");
        if (headersAvailable)
        {
            for (const auto& headerPath : includedHeaders)
            {
                mlir::emitc::VerbatimOp::create(builder, loc, "#include \"" + headerPath + "\"");
            }
        }
        mlir::emitc::VerbatimOp::create(builder, loc, "/* Generated from DSDL IR by convert-dsdl-to-emitc. */");
        for (const auto& typeName : forwardDeclaredTypes)
        {
            // NOLINTNEXTLINE(performance-inefficient-string-concatenation)
            mlir::emitc::VerbatimOp::create(builder, loc, "typedef struct " + typeName + " " + typeName + ";");
        }
        for (const auto& symbol : capacityCheckSymbols)
        {
            mlir::emitc::VerbatimOp::create(builder, loc, "int8_t " + symbol + "(int64_t);");
        }
        for (const auto& symbol : unionTagValidateSymbols)
        {
            mlir::emitc::VerbatimOp::create(builder, loc, "int8_t " + symbol + "(int64_t);");
        }
        for (const auto& symbol : unionTagIoHelperSymbols)
        {
            mlir::emitc::VerbatimOp::create(builder, loc, "int64_t " + symbol + "(int64_t);");
        }
        for (const auto& symbol : scalarUnsignedHelperSymbols)
        {
            mlir::emitc::VerbatimOp::create(builder, loc, "int64_t " + symbol + "(int64_t);");
        }
        for (const auto& symbol : scalarSignedHelperSymbols)
        {
            mlir::emitc::VerbatimOp::create(builder, loc, "int64_t " + symbol + "(int64_t);");
        }
        for (const auto& symbol : scalarFloatHelperSymbols)
        {
            // The scalar float helper is width-matched (float for 16/32-bit
            // fields, double for 64-bit); mirror its actual signature in the
            // forward declaration so the prototype matches the lowered definition.
            std::string ctype = "double";
            if (auto fn = module.lookupSymbol<mlir::func::FuncOp>(symbol))
            {
                if (fn.getFunctionType().getNumInputs() == 1 && fn.getFunctionType().getInput(0).isF32())
                {
                    ctype = "float";
                }
            }
            // NOLINTNEXTLINE(performance-inefficient-string-concatenation)
            mlir::emitc::VerbatimOp::create(builder, loc, ctype + " " + symbol + "(" + ctype + ");");
        }
        for (const auto& symbol : arrayLengthPrefixHelperSymbols)
        {
            mlir::emitc::VerbatimOp::create(builder, loc, "int64_t " + symbol + "(int64_t);");
        }
        for (const auto& symbol : arrayLengthValidateSymbols)
        {
            mlir::emitc::VerbatimOp::create(builder, loc, "int8_t " + symbol + "(int64_t);");
        }
        for (const auto& symbol : delimiterValidateSymbols)
        {
            mlir::emitc::VerbatimOp::create(builder, loc, "int8_t " + symbol + "(int64_t, int64_t);");
        }

        for (const mlir::dsdl::SchemaOp schema : schemaOps)
        {
            schema->erase();
        }

        // Before the conversion, not after. A loop carries an induction variable its caller
        // does not read, and here that is a dead scf result the canonicaliser drops; once it
        // is an emitc.variable it has memory effects and stays, reaching the consumer as a
        // set-but-unused declaration and their -Werror build.
        //
        // Applied as patterns rather than through a pass manager, because this is already
        // inside a pass.
        {
            mlir::RewritePatternSet cleanup(&getContext());
            for (const mlir::RegisteredOperationName name : getContext().getRegisteredOperations())
            {
                name.getCanonicalizationPatterns(cleanup, &getContext());
            }
            if (mlir::failed(mlir::applyPatternsGreedily(module, std::move(cleanup))))
            {
                module.emitError("failed to canonicalize built plan bodies");
                signalPassFailure();
                return;
            }
        }

        if (mlir::failed(lowerPlanOperations(module)))
        {
            return;
        }

        // Again after the conversion: it introduces values of its own, and a declaration the
        // emitted C never reads is a diagnostic in the consumer's build.
        {
            mlir::RewritePatternSet cleanup(&getContext());
            for (const mlir::RegisteredOperationName name : getContext().getRegisteredOperations())
            {
                name.getCanonicalizationPatterns(cleanup, &getContext());
            }
            if (mlir::failed(mlir::applyPatternsGreedily(module, std::move(cleanup))))
            {
                module.emitError("failed to canonicalize converted plan bodies");
                signalPassFailure();
                return;
            }
        }
    }
};

}  // namespace

std::unique_ptr<mlir::Pass> createConvertDSDLToEmitCPass()
{
    return std::make_unique<ConvertDSDLToEmitCPass>();
}

void registerDSDLConvertPasses()
{
    static bool once = false;
    if (once)
    {
        return;
    }
    once = true;
    static mlir::PassRegistration<ConvertDSDLToEmitCPass> const reg;
}

}  // namespace llvmdsdl
