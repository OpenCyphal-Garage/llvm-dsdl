//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Lowers DSDL plan operations into the LLVM dialect, for emission as objects.
///
/// The counterpart of convert-dsdl-to-emitc over the same bodies. Where that one maps the
/// dialect onto C's spellings, this one maps it onto addresses and calls, which is what the
/// two targets genuinely disagree about: a plan says "the member at index 2", and C answers
/// with a name while LLVM answers with an offset.
///
//===----------------------------------------------------------------------===//

#include <mlir/Conversion/LLVMCommon/TypeConverter.h>
#include <mlir/Dialect/Arith/IR/Arith.h>
#include <mlir/Dialect/DLTI/DLTI.h>
#include <mlir/Dialect/Func/IR/FuncOps.h>
#include <mlir/Dialect/Func/Transforms/FuncConversions.h>
#include <mlir/Dialect/LLVMIR/LLVMDialect.h>
#include <mlir/IR/Builders.h>
#include <mlir/IR/BuiltinOps.h>
#include <mlir/Dialect/SCF/IR/SCF.h>
#include <mlir/Pass/Pass.h>
#include <mlir/Pass/PassRegistry.h>
#include <mlir/Interfaces/DataLayoutInterfaces.h>
#include <mlir/Transforms/DialectConversion.h>

#include <memory>
#include <string>

#include "llvmdsdl/IR/DSDLOps.h"
#include "llvmdsdl/IR/DSDLTypes.h"
#include "llvmdsdl/Transforms/Passes.h"

namespace llvmdsdl
{
namespace
{

/// @brief Declares a runtime entry point once, and answers its symbol.
///
/// The plan calls into the DSDL runtime, whose definitions live outside this module. What
/// resolves them is the object lane's to decide -- inlining the bit work, emitting the
/// primitives here, or linking a compiled runtime -- and until it does, these are the
/// undefined references that say so.
constexpr llvm::StringLiteral kSizeBitsAttr{"llvmdsdl.size_bits"};

unsigned targetSizeBits(mlir::ModuleOp module, const unsigned fallback)
{
    if (const auto recorded = module->getAttrOfType<mlir::IntegerAttr>(kSizeBitsAttr))
    {
        return static_cast<unsigned>(recorded.getInt());
    }
    if (module->hasAttr(mlir::DLTIDialect::kDataLayoutAttrName))
    {
        const mlir::DataLayout layout(module);
        const auto             ptr = mlir::LLVM::LLVMPointerType::get(module.getContext());
        if (const auto bits = layout.getTypeSizeInBits(ptr); bits > 0)
        {
            return static_cast<unsigned>(bits);
        }
    }
    return fallback;
}

/// @brief The parameter and result types @p name is defined with in the runtime header.
///
/// A C function's signature is a property of the function, not of a call. Taking it from
/// whatever a call site happened to hold gives one declaration per spelling of the same
/// function -- and, where two call sites disagree, a call that passes its arguments somewhere
/// the callee does not read them.
///
/// @param[in] ctx MLIR context.
/// @param[in] name The runtime primitive's symbol.
/// @param[in] sizeTy What the target spells `size_t` as.
/// @return The signature, or null when @p name is not a runtime primitive.
mlir::LLVM::LLVMFunctionType runtimeSignature(mlir::MLIRContext* ctx,
                                              llvm::StringRef    name,
                                              mlir::Type         sizeTy)
{
    auto ptr  = mlir::LLVM::LLVMPointerType::get(ctx);
    auto i1   = mlir::IntegerType::get(ctx, 1);
    auto i8   = mlir::IntegerType::get(ctx, 8);
    auto i64  = mlir::IntegerType::get(ctx, 64);
    auto f32  = mlir::Float32Type::get(ctx);
    auto f64  = mlir::Float64Type::get(ctx);
    auto none = mlir::LLVM::LLVMVoidType::get(ctx);

    const auto fn = [](mlir::Type result, mlir::ArrayRef<mlir::Type> args) {
        return mlir::LLVM::LLVMFunctionType::get(result, args);
    };

    if (name == "dsdl_runtime_set_bit")
    {
        return fn(i8, {ptr, sizeTy, sizeTy, i1});
    }
    if ((name == "dsdl_runtime_set_uxx") || (name == "dsdl_runtime_set_ixx"))
    {
        // The value is widened to the runtime's own 64-bit carrier, and the width it occupies
        // on the wire rides beside it.
        return fn(i8, {ptr, sizeTy, sizeTy, i64, i8});
    }
    if ((name == "dsdl_runtime_set_f16") || (name == "dsdl_runtime_set_f32"))
    {
        return fn(i8, {ptr, sizeTy, sizeTy, f32});
    }
    if (name == "dsdl_runtime_set_f64")
    {
        return fn(i8, {ptr, sizeTy, sizeTy, f64});
    }
    if (name == "dsdl_runtime_get_bit")
    {
        return fn(i1, {ptr, sizeTy, sizeTy});
    }
    if ((name == "dsdl_runtime_get_f16") || (name == "dsdl_runtime_get_f32"))
    {
        return fn(f32, {ptr, sizeTy, sizeTy});
    }
    if (name == "dsdl_runtime_get_f64")
    {
        return fn(f64, {ptr, sizeTy, sizeTy});
    }
    if (name.starts_with("dsdl_runtime_get_u") || name.starts_with("dsdl_runtime_get_i"))
    {
        const llvm::StringRef width = name.drop_front(llvm::StringRef("dsdl_runtime_get_u").size());
        unsigned              bits  = 0;
        if (!width.getAsInteger(10, bits))
        {
            return fn(mlir::IntegerType::get(ctx, bits), {ptr, sizeTy, sizeTy, i8});
        }
    }
    if (name == "dsdl_runtime_get_bits")
    {
        return fn(none, {ptr, ptr, sizeTy, sizeTy, sizeTy});
    }
    if (name == "dsdl_runtime_copy_bits")
    {
        return fn(none, {ptr, sizeTy, sizeTy, ptr, sizeTy});
    }
    return {};
}

mlir::LLVM::LLVMFuncOp declareRuntime(mlir::ConversionPatternRewriter& rewriter,
                                      mlir::ModuleOp                   module,
                                      llvm::StringRef                  name,
                                      mlir::Type                       result,
                                      mlir::ArrayRef<mlir::Type>       arguments)
{
    if (auto existing = module.lookupSymbol<mlir::LLVM::LLVMFuncOp>(name))
    {
        return existing;
    }
    mlir::OpBuilder::InsertionGuard const guard(rewriter);
    rewriter.setInsertionPointToStart(module.getBody());
    return mlir::LLVM::LLVMFuncOp::create(rewriter,
                                          module.getLoc(),
                                          name,
                                          mlir::LLVM::LLVMFunctionType::get(result, arguments));
}

/// @brief Converts @p value to @p target, which the callee's signature asks for.
///
/// The plan holds a scalar at whatever width it was read or computed at, and the runtime
/// takes it at the width it declares. Only integers and floats reach here, and a pointer is
/// already the one opaque type.
mlir::Value coerce(mlir::ConversionPatternRewriter& rewriter,
                   mlir::Location                   loc,
                   mlir::Value                      value,
                   mlir::Type                       target,
                   const bool                       isSigned)
{
    const mlir::Type from = value.getType();
    if (from == target)
    {
        return value;
    }
    if (mlir::isa<mlir::IntegerType>(from) && mlir::isa<mlir::IntegerType>(target))
    {
        const unsigned fromWidth = mlir::cast<mlir::IntegerType>(from).getWidth();
        const unsigned to   = mlir::cast<mlir::IntegerType>(target).getWidth();
        if (fromWidth == to)
        {
            return value;
        }
        if (fromWidth > to)
        {
            return mlir::LLVM::TruncOp::create(rewriter, loc, target, value);
        }
        return isSigned ? mlir::Value{mlir::LLVM::SExtOp::create(rewriter, loc, target, value)}
                        : mlir::Value{mlir::LLVM::ZExtOp::create(rewriter, loc, target, value)};
    }
    if (mlir::isa<mlir::FloatType>(from) && mlir::isa<mlir::FloatType>(target))
    {
        const unsigned fromWidth = mlir::cast<mlir::FloatType>(from).getWidth();
        const unsigned to   = mlir::cast<mlir::FloatType>(target).getWidth();
        if (fromWidth == to)
        {
            return value;
        }
        return (fromWidth > to) ? mlir::Value{mlir::LLVM::FPTruncOp::create(rewriter, loc, target, value)}
                           : mlir::Value{mlir::LLVM::FPExtOp::create(rewriter, loc, target, value)};
    }
    return value;
}

mlir::Value callRuntime(mlir::ConversionPatternRewriter& rewriter,
                        mlir::Location                   loc,
                        mlir::ModuleOp                   module,
                        llvm::StringRef                  name,
                        mlir::Type                       result,
                        mlir::ValueRange                 arguments,
                        const bool                       isSigned = false)
{
    auto* ctx = rewriter.getContext();
    // The runtime's own signature, so that every call to one primitive agrees on what it takes.
    const auto declared =
        runtimeSignature(ctx, name, mlir::IntegerType::get(ctx, targetSizeBits(module, 64)));

    mlir::SmallVector<mlir::Value, 6> coerced(arguments.begin(), arguments.end());
    mlir::Type                        resultType = result;
    if (declared)
    {
        resultType = mlir::isa<mlir::LLVM::LLVMVoidType>(declared.getReturnType()) ? mlir::Type{}
                                                                                  : declared.getReturnType();
        for (std::size_t i = 0; (i < coerced.size()) && (i < declared.getParams().size()); ++i)
        {
            coerced[i] = coerce(rewriter, loc, coerced[i], declared.getParams()[i], isSigned);
        }
    }

    mlir::SmallVector<mlir::Type, 6> argumentTypes;
    for (mlir::Value argument : coerced)
    {
        argumentTypes.push_back(argument.getType());
    }
    auto callee = declareRuntime(rewriter,
                                 module,
                                 name,
                                 resultType ? resultType : mlir::LLVM::LLVMVoidType::get(ctx),
                                 argumentTypes);
    auto call = mlir::LLVM::CallOp::create(rewriter, loc, callee, coerced);
    if (call.getNumResults() == 0)
    {
        return {};
    }
    // The runtime answers at its own width -- a bit as `bool`, a narrow field in the holder it
    // fits -- and the plan carries the value at the width it computes on.
    return result ? coerce(rewriter, loc, call.getResult(), result, isSigned) : call.getResult();
}

mlir::Type voidType(mlir::MLIRContext* ctx)
{
    return mlir::LLVM::LLVMVoidType::get(ctx);
}

//===----------------------------------------------------------------------===//
// Pointers and the storage behind them
//===----------------------------------------------------------------------===//

struct IsNullLowering final : public mlir::OpConversionPattern<mlir::dsdl::IsNullOp>
{
    using mlir::OpConversionPattern<mlir::dsdl::IsNullOp>::OpConversionPattern;

    mlir::LogicalResult matchAndRewrite(mlir::dsdl::IsNullOp             op,
                                        OpAdaptor                        adaptor,
                                        mlir::ConversionPatternRewriter& rewriter) const override
    {
        auto null = mlir::LLVM::ZeroOp::create(rewriter, op.getLoc(), adaptor.getPointer().getType());
        rewriter.replaceOpWithNewOp<mlir::LLVM::ICmpOp>(op,
                                                        mlir::LLVM::ICmpPredicate::eq,
                                                        adaptor.getPointer(),
                                                        null);
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
        rewriter.replaceOpWithNewOp<mlir::LLVM::LoadOp>(op, op.getValue().getType(), adaptor.getPointer());
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
        rewriter.replaceOpWithNewOp<mlir::LLVM::StoreOp>(op, adaptor.getValue(), adaptor.getPointer());
        return mlir::success();
    }
};

/// @brief `buffer + byte_offset`, as a byte-addressed walk.
struct BufferAtLowering final : public mlir::OpConversionPattern<mlir::dsdl::BufferAtOp>
{
    using mlir::OpConversionPattern<mlir::dsdl::BufferAtOp>::OpConversionPattern;

    mlir::LogicalResult matchAndRewrite(mlir::dsdl::BufferAtOp           op,
                                        OpAdaptor                        adaptor,
                                        mlir::ConversionPatternRewriter& rewriter) const override
    {
        auto byteTy = rewriter.getI8Type();
        rewriter.replaceOpWithNewOp<mlir::LLVM::GEPOp>(op,
                                                       adaptor.getBuffer().getType(),
                                                       byteTy,
                                                       adaptor.getBuffer(),
                                                       mlir::ValueRange{adaptor.getByteOffset()});
        return mlir::success();
    }
};

/// @brief A stack slot holding the value, addressed.
struct LocalLowering final : public mlir::OpConversionPattern<mlir::dsdl::LocalOp>
{
    using mlir::OpConversionPattern<mlir::dsdl::LocalOp>::OpConversionPattern;

    mlir::LogicalResult matchAndRewrite(mlir::dsdl::LocalOp              op,
                                        OpAdaptor                        adaptor,
                                        mlir::ConversionPatternRewriter& rewriter) const override
    {
        const mlir::Location loc  = op.getLoc();
        auto                 one  = mlir::LLVM::ConstantOp::create(rewriter, loc, rewriter.getI64Type(),
                                                 rewriter.getI64IntegerAttr(1));
        auto slot = mlir::LLVM::AllocaOp::create(rewriter,
                                                 loc,
                                                 mlir::LLVM::LLVMPointerType::get(rewriter.getContext()),
                                                 adaptor.getInit().getType(),
                                                 one);
        mlir::LLVM::StoreOp::create(rewriter, loc, adaptor.getInit(), slot);
        rewriter.replaceOp(op, slot.getResult());
        return mlir::success();
    }
};

/// @brief The buffer, or somewhere safe to read no bytes from.
///
/// The runtime requires a non-null pointer even where it reads nothing. C reaches for a string
/// literal; here it is a zero-sized constant of this module's own.
struct BufferOrEmptyLowering final : public mlir::OpConversionPattern<mlir::dsdl::BufferOrEmptyOp>
{
    using mlir::OpConversionPattern<mlir::dsdl::BufferOrEmptyOp>::OpConversionPattern;

    mlir::LogicalResult matchAndRewrite(mlir::dsdl::BufferOrEmptyOp      op,
                                        OpAdaptor                        adaptor,
                                        mlir::ConversionPatternRewriter& rewriter) const override
    {
        const mlir::Location loc     = op.getLoc();
        auto                 module  = op->getParentOfType<mlir::ModuleOp>();
        auto                 ptrTy   = mlir::LLVM::LLVMPointerType::get(rewriter.getContext());
        constexpr llvm::StringRef kEmpty = "llvmdsdl_empty_buffer";

        if (!module.lookupSymbol<mlir::LLVM::GlobalOp>(kEmpty))
        {
            mlir::OpBuilder::InsertionGuard const guard(rewriter);
            rewriter.setInsertionPointToStart(module.getBody());
            auto byteTy = mlir::LLVM::LLVMArrayType::get(rewriter.getI8Type(), 1);
            mlir::LLVM::GlobalOp::create(rewriter,
                                         module.getLoc(),
                                         byteTy,
                                         /*isConstant=*/true,
                                         mlir::LLVM::Linkage::Private,
                                         kEmpty,
                                         rewriter.getZeroAttr(mlir::RankedTensorType::get({1}, rewriter.getI8Type())));
        }
        auto empty  = mlir::LLVM::AddressOfOp::create(rewriter, loc, ptrTy, kEmpty);
        auto null   = mlir::LLVM::ZeroOp::create(rewriter, loc, adaptor.getBuffer().getType());
        auto isNull = mlir::LLVM::ICmpOp::create(rewriter,
                                                 loc,
                                                 mlir::LLVM::ICmpPredicate::eq,
                                                 adaptor.getBuffer(),
                                                 null);
        rewriter.replaceOpWithNewOp<mlir::LLVM::SelectOp>(op, isNull, empty, adaptor.getBuffer());
        return mlir::success();
    }
};


//===----------------------------------------------------------------------===//
// The struct a member is addressed within
//===----------------------------------------------------------------------===//

/// @brief The LLVM type the C backend holds one value of this shape in.
///
/// A DSDL width is not a C width: eleven bits are held in two bytes, and a float16 in a
/// `float`, there being no narrower one to put it in. Held against the generated headers by
/// llvmdsdl-member-layout-crosscheck, which is what makes this safe to write twice.
mlir::Type scalarStorage(mlir::MLIRContext* ctx, llvm::StringRef category, const std::int64_t bits)
{
    if (category == "bool")
    {
        return mlir::IntegerType::get(ctx, 8);
    }
    if (category == "float")
    {
        return (bits <= 32) ? mlir::Type(mlir::Float32Type::get(ctx))
                            : mlir::Type(mlir::Float64Type::get(ctx));
    }
    const unsigned holder = (bits <= 8) ? 8U : (bits <= 16) ? 16U : (bits <= 32) ? 32U : 64U;
    return mlir::IntegerType::get(ctx, holder);
}

/// @brief The storage one field occupies in the generated struct.
/// @brief The width, in bits, the module's target spells `size_t` at.
///
/// A variable-length array holds its count in one, and every runtime primitive takes the
/// buffer size and bit offset in one. Both come from here so a target cannot be 64-bit in the
/// struct and 32-bit at the call.
/// @param[in] module The module being converted.
/// @param[in] fallback What to answer when the module states no layout.
/// @return The width in bits.
mlir::Type fieldStorage(mlir::MLIRContext* ctx,
                        llvm::StringRef    category,
                        const std::int64_t bits,
                        llvm::StringRef    arrayKind,
                        const std::int64_t capacity,
                        llvm::StringRef    compositeType,
                        llvm::DenseMap<llvm::StringRef, mlir::Type>& composites,
                        const unsigned                               sizeBits)
{
    const bool isArray = !arrayKind.empty() && (arrayKind != "none");
    // A bool array is bitpacked, one bit per element, whether or not its length varies.
    const bool packed = isArray && (category == "bool");

    mlir::Type element;
    if (!compositeType.empty())
    {
        const auto found = composites.find(compositeType);
        // A nested type whose own struct has not been seen is addressed as an opaque body: a
        // GEP past it would need its size, and nothing here has one to offer.
        if (found == composites.end())
        {
            return {};
        }
        element = found->second;
    }
    else
    {
        element = packed ? mlir::IntegerType::get(ctx, 8) : scalarStorage(ctx, category, bits);
    }
    if (!isArray)
    {
        return element;
    }
    const auto extent = packed ? ((capacity + 7) / 8) : capacity;
    auto       storage = mlir::LLVM::LLVMArrayType::get(element, static_cast<unsigned>(extent));
    if (arrayKind == "fixed")
    {
        return storage;
    }
    // A variable-length array holds its elements and then its count, which is what the
    // member's own two positions address.
    return mlir::LLVM::LLVMStructType::getLiteral(ctx, {storage, mlir::IntegerType::get(ctx, sizeBits)});
}

/// @brief Builds a struct per schema section, matching what the C backend emits.
///
/// Keyed by the spelling `!dsdl.opaque` carries, which is how a plan names the thing it was
/// handed. A type whose members cannot all be described is left out rather than guessed at.
/// @brief What each published serdes wrapper is called beneath the header.
///
/// A generated header publishes `X__serialize_` as a static inline that calls the body the plan
/// was built into. Only the body is a symbol, so a call between objects has to name it.
llvm::DenseMap<llvm::StringRef, std::string> buildSerdesBodies(mlir::ModuleOp module)
{
    llvm::DenseMap<llvm::StringRef, std::string> bodies;
    for (mlir::Operation& schema : module.getBodyRegion().front())
    {
        if (schema.getName().getStringRef() != "dsdl.schema")
        {
            continue;
        }
        const auto stem = schema.getAttrOfType<mlir::StringAttr>("sym_name");
        if (!stem || (schema.getNumRegions() == 0) || schema.getRegion(0).empty())
        {
            continue;
        }
        for (mlir::Operation& plan : schema.getRegion(0).front())
        {
            if (plan.getName().getStringRef() != "dsdl.serialization_plan")
            {
                continue;
            }
            const auto section = plan.getAttrOfType<mlir::StringAttr>("section");
            const std::string suffix =
                section ? ("__" + section.getValue().str()) : std::string{};
            for (const char* which : {"serialize", "deserialize"})
            {
                const auto published =
                    plan.getAttrOfType<mlir::StringAttr>(std::string("c_") + which + "_symbol");
                if (published)
                {
                    bodies[published.getValue()] = stem.getValue().str() + suffix + "__" + which + "_ir_";
                }
            }
        }
    }
    return bodies;
}

llvm::DenseMap<llvm::StringRef, mlir::Type> buildStructs(mlir::ModuleOp module, const unsigned sizeBits)
{
    auto* ctx = module.getContext();
    llvm::DenseMap<llvm::StringRef, mlir::Type> composites;

    // A nested type has to be described before the type holding it can be, and a chain of them
    // takes one round per link. This runs until a round describes nothing new; DSDL forbids a
    // type reaching itself, so the fixed point exists.
    for (std::size_t described = std::numeric_limits<std::size_t>::max(); described != composites.size();)
    {
        described = composites.size();
        for (mlir::Operation& schema : module.getBodyRegion().front())
        {
            if (schema.getName().getStringRef() != "dsdl.schema")
            {
                continue;
            }
            if ((schema.getNumRegions() == 0) || schema.getRegion(0).empty())
            {
                continue;
            }
            for (mlir::Operation& plan : schema.getRegion(0).front())
            {
                if (plan.getName().getStringRef() != "dsdl.serialization_plan")
                {
                    continue;
                }
                const auto nameAttr = plan.getAttrOfType<mlir::StringAttr>("c_type_name");
                if (!nameAttr || ((plan.getNumRegions() > 0) && plan.getRegion(0).empty()))
                {
                    continue;
                }
                mlir::SmallVector<mlir::Type, 8> members;
                bool                             describable = true;
                for (mlir::Operation& io : plan.getRegion(0).front())
                {
                    if (io.getName().getStringRef() != "dsdl.io")
                    {
                        continue;
                    }
                    const auto kind = io.getAttrOfType<mlir::StringAttr>("kind");
                    if (!kind || (kind.getValue() != "field"))
                    {
                        continue;  // Padding reserves wire bits and holds no member.
                    }
                    const auto category = io.getAttrOfType<mlir::StringAttr>("scalar_category");
                    const auto arrayKind = io.getAttrOfType<mlir::StringAttr>("array_kind");
                    const auto composite = io.getAttrOfType<mlir::StringAttr>("composite_c_type_name");
                    const auto bitsAttr  = io.getAttrOfType<mlir::IntegerAttr>("bit_length");
                    const auto capAttr   = io.getAttrOfType<mlir::IntegerAttr>("array_capacity");
                    auto       storage   = fieldStorage(ctx,
                                                category ? category.getValue() : llvm::StringRef{},
                                                bitsAttr ? bitsAttr.getInt() : 0,
                                                arrayKind ? arrayKind.getValue() : llvm::StringRef{},
                                                capAttr ? capAttr.getInt() : 0,
                                                composite ? composite.getValue() : llvm::StringRef{},
                                                composites,
                                                sizeBits);
                    if (!storage)
                    {
                        describable = false;
                        break;
                    }
                    members.push_back(storage);
                }
                if (!describable)
                {
                    continue;
                }
                if (plan.hasAttr("is_union"))
                {
                    const auto tagBits = plan.getAttrOfType<mlir::IntegerAttr>("union_tag_bits");
                    members.push_back(scalarStorage(ctx, "unsigned", tagBits ? tagBits.getInt() : 8));
                }
                if (members.empty())
                {
                    // C has no empty struct, so the backend gives it a member nothing maps to.
                    members.push_back(mlir::IntegerType::get(ctx, 8));
                }
                composites[nameAttr.getValue()] = mlir::LLVM::LLVMStructType::getLiteral(ctx, members);
            }
        }
    }
    return composites;
}

/// @brief The struct a pointer's own spelling names, if one was described.
mlir::Type structBehind(mlir::Type pointee, const llvm::DenseMap<llvm::StringRef, mlir::Type>& composites)
{
    auto named = mlir::dyn_cast<mlir::dsdl::OpaqueType>(pointee);
    if (!named)
    {
        return {};
    }
    llvm::StringRef spelling = named.getName();
    spelling.consume_front("const ");
    const auto found = composites.find(spelling);
    return (found == composites.end()) ? mlir::Type{} : found->second;
}

//===----------------------------------------------------------------------===//
// Calls out of the plan
//===----------------------------------------------------------------------===//

/// @brief The runtime primitive a scalar access resolves to.
///
/// The same selection convert-dsdl-to-emitc makes, on the same grounds: the runtime spells one
/// primitive per value shape rather than one generic call.
std::string runtimePrimitiveName(const bool write, mlir::Type valueType, const std::int64_t width, const bool isSigned)
{
    if (mlir::isa<mlir::FloatType>(valueType))
    {
        return std::string(write ? "dsdl_runtime_set_f" : "dsdl_runtime_get_f") + std::to_string(width);
    }
    if ((width == 1) && !isSigned)
    {
        return write ? "dsdl_runtime_set_bit" : "dsdl_runtime_get_bit";
    }
    if (write)
    {
        return isSigned ? "dsdl_runtime_set_ixx" : "dsdl_runtime_set_uxx";
    }
    const unsigned holder = (width <= 8) ? 8U : (width <= 16) ? 16U : (width <= 32) ? 32U : 64U;
    return std::string(isSigned ? "dsdl_runtime_get_i" : "dsdl_runtime_get_u") + std::to_string(holder);
}

struct WriteBitsLowering final : public mlir::OpConversionPattern<mlir::dsdl::WriteBitsOp>
{
    using mlir::OpConversionPattern<mlir::dsdl::WriteBitsOp>::OpConversionPattern;

    mlir::LogicalResult matchAndRewrite(mlir::dsdl::WriteBitsOp          op,
                                        OpAdaptor                        adaptor,
                                        mlir::ConversionPatternRewriter& rewriter) const override
    {
        const mlir::Location loc    = op.getLoc();
        auto                 module = op->getParentOfType<mlir::ModuleOp>();
        const std::string    callee =
            runtimePrimitiveName(true, op.getValue().getType(), op.getWidth(), op.getIsSigned());

        mlir::SmallVector<mlir::Value, 5> arguments{adaptor.getBuffer(),
                                                    adaptor.getBufferSizeBytes(),
                                                    adaptor.getBitOffset(),
                                                    adaptor.getValue()};
        if ((callee == "dsdl_runtime_set_uxx") || (callee == "dsdl_runtime_set_ixx"))
        {
            arguments.push_back(mlir::LLVM::ConstantOp::create(
                rewriter, loc, rewriter.getI8Type(), rewriter.getI8IntegerAttr(static_cast<std::int8_t>(op.getWidth()))));
        }
        auto result =
            callRuntime(rewriter, loc, module, callee, rewriter.getI8Type(), arguments, op.getIsSigned());
        rewriter.replaceOp(op, result);
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
        auto                 module = op->getParentOfType<mlir::ModuleOp>();
        const std::string    callee =
            runtimePrimitiveName(false, op.getValue().getType(), op.getWidth(), op.getIsSigned());

        mlir::SmallVector<mlir::Value, 4> arguments{adaptor.getBuffer(),
                                                    adaptor.getBufferSizeBytes(),
                                                    adaptor.getBitOffset()};
        if ((callee.find("_get_u") != std::string::npos) || (callee.find("_get_i") != std::string::npos))
        {
            arguments.push_back(mlir::LLVM::ConstantOp::create(
                rewriter, loc, rewriter.getI8Type(), rewriter.getI8IntegerAttr(static_cast<std::int8_t>(op.getWidth()))));
        }
        auto result =
            callRuntime(rewriter, loc, module, callee, op.getValue().getType(), arguments, op.getIsSigned());
        rewriter.replaceOp(op, result);
        return mlir::success();
    }
};

struct BitWriteLowering final : public mlir::OpConversionPattern<mlir::dsdl::BitWriteOp>
{
    using mlir::OpConversionPattern<mlir::dsdl::BitWriteOp>::OpConversionPattern;

    mlir::LogicalResult matchAndRewrite(mlir::dsdl::BitWriteOp           op,
                                        OpAdaptor                        adaptor,
                                        mlir::ConversionPatternRewriter& rewriter) const override
    {
        auto module = op->getParentOfType<mlir::ModuleOp>();
        (void) callRuntime(rewriter,
                           op.getLoc(),
                           module,
                           "dsdl_runtime_copy_bits",
                           voidType(rewriter.getContext()),
                           mlir::ValueRange{adaptor.getDestination(),
                                            adaptor.getDestinationBitOffset(),
                                            adaptor.getWidth(),
                                            adaptor.getSource(),
                                            adaptor.getSourceBitOffset()});
        rewriter.eraseOp(op);
        return mlir::success();
    }
};

struct BitReadLowering final : public mlir::OpConversionPattern<mlir::dsdl::BitReadOp>
{
    using mlir::OpConversionPattern<mlir::dsdl::BitReadOp>::OpConversionPattern;

    mlir::LogicalResult matchAndRewrite(mlir::dsdl::BitReadOp            op,
                                        OpAdaptor                        adaptor,
                                        mlir::ConversionPatternRewriter& rewriter) const override
    {
        auto module = op->getParentOfType<mlir::ModuleOp>();
        (void) callRuntime(rewriter,
                           op.getLoc(),
                           module,
                           "dsdl_runtime_get_bits",
                           voidType(rewriter.getContext()),
                           mlir::ValueRange{adaptor.getDestination(),
                                            adaptor.getBuffer(),
                                            adaptor.getBufferSizeBytes(),
                                            adaptor.getBitOffset(),
                                            adaptor.getWidth()});
        rewriter.eraseOp(op);
        return mlir::success();
    }
};

/// @brief A nested type's own entry point, which lives in its own object.
struct CallSerdesLowering final : public mlir::OpConversionPattern<mlir::dsdl::CallSerdesOp>
{
    CallSerdesLowering(const mlir::TypeConverter&                      converter,
                       mlir::MLIRContext*                              ctx,
                       const llvm::DenseMap<llvm::StringRef, std::string>& bodies)
        : mlir::OpConversionPattern<mlir::dsdl::CallSerdesOp>(converter, ctx)
        , bodies_(bodies)
    {
    }

    mlir::LogicalResult matchAndRewrite(mlir::dsdl::CallSerdesOp         op,
                                        OpAdaptor                        adaptor,
                                        mlir::ConversionPatternRewriter& rewriter) const override
    {
        auto              module   = op->getParentOfType<mlir::ModuleOp>();
        const auto        found    = bodies_.find(op.getCallee());
        const std::string callee   = (found == bodies_.end()) ? op.getCallee().str() : found->second;
        const mlir::SmallVector<mlir::Value, 3> arguments{adaptor.getObject(), adaptor.getBuffer(), adaptor.getSize()};

        // A nested type in this same module is called directly; one from another is declared.
        if (auto body = module.lookupSymbol<mlir::func::FuncOp>(callee))
        {
            auto call = mlir::func::CallOp::create(rewriter, op.getLoc(), body, arguments);
            rewriter.replaceOp(op, call.getResult(0));
            return mlir::success();
        }
        auto result =
            callRuntime(rewriter, op.getLoc(), module, callee, rewriter.getI8Type(), arguments);
        rewriter.replaceOp(op, result);
        return mlir::success();
    }

private:
    const llvm::DenseMap<llvm::StringRef, std::string>& bodies_;
};


//===----------------------------------------------------------------------===//
// Members, by position
//===----------------------------------------------------------------------===//

/// @brief Base for the accesses that walk into the object.
///
/// Each carries the member's position and the struct is derived from the schema, so the GEP is
/// the two put together and LLVM computes the offset from its own data layout. Nothing here
/// adds up bytes by hand, which is the one thing that could disagree with the C struct without
/// anything noticing.
template <typename OpT>
struct MemberAccess : public mlir::OpConversionPattern<OpT>
{
    MemberAccess(const mlir::TypeConverter&                          converter,
                 mlir::MLIRContext*                                  ctx,
                 const llvm::DenseMap<llvm::StringRef, mlir::Type>&  composites)
        : mlir::OpConversionPattern<OpT>(converter, ctx)
        , structs(composites)
    {
    }

    /// @brief The address the operation's path and indices designate.
    mlir::Value address(OpT                              op,
                        mlir::Value                      object,
                        mlir::ConversionPatternRewriter& rewriter,
                        mlir::Value                      elementIndex = {}) const
    {
        auto pointerType = mlir::dyn_cast<mlir::dsdl::PtrType>(op.getObject().getType());
        if (!pointerType)
        {
            return {};
        }
        const mlir::Type owner = structBehind(pointerType.getPointee(), structs);
        if (!owner)
        {
            return {};
        }
        mlir::SmallVector<mlir::LLVM::GEPArg, 4> path{0};
        for (const std::int64_t index : op.getIndices())
        {
            path.push_back(static_cast<std::int32_t>(index));
        }
        if (elementIndex)
        {
            // Into the element storage the path just reached, at a position only known here.
            path.push_back(mlir::LLVM::GEPArg{elementIndex});
        }
        return mlir::LLVM::GEPOp::create(rewriter,
                                         op.getLoc(),
                                         mlir::LLVM::LLVMPointerType::get(rewriter.getContext()),
                                         owner,
                                         object,
                                         path);
    }

    /// @brief The type of the member the path reaches, which is what a load and a store move.
    ///
    /// A plan carries scalars at the width it computes on, and the struct holds them at the
    /// width C declares them. Reading the wider type would take the neighbouring members with
    /// it, which is a value that then saturates rather than one that is wrong-looking.
    mlir::Type memberType(OpT op, const bool element) const
    {
        auto pointerType = mlir::dyn_cast<mlir::dsdl::PtrType>(op.getObject().getType());
        if (!pointerType)
        {
            return {};
        }
        mlir::Type at = structBehind(pointerType.getPointee(), structs);
        for (const std::int64_t index : op.getIndices())
        {
            auto owner = mlir::dyn_cast_or_null<mlir::LLVM::LLVMStructType>(at);
            if (!owner || (index < 0) || (index >= static_cast<std::int64_t>(owner.getBody().size())))
            {
                return {};
            }
            at = owner.getBody()[static_cast<std::size_t>(index)];
        }
        if (element)
        {
            auto array = mlir::dyn_cast_or_null<mlir::LLVM::LLVMArrayType>(at);
            if (!array)
            {
                return {};
            }
            at = array.getElementType();
        }
        return at;
    }

    const llvm::DenseMap<llvm::StringRef, mlir::Type>& structs;
};

/// @brief Converts @p value between the width a member is held at and the width a plan uses.
mlir::Value fit(mlir::ConversionPatternRewriter& rewriter,
                mlir::Location                   loc,
                mlir::Value                      value,
                mlir::Type                       target,
                const bool                       isSigned)
{
    const mlir::Type from = value.getType();
    if (from == target)
    {
        return value;
    }
    if (mlir::isa<mlir::IntegerType>(from) && mlir::isa<mlir::IntegerType>(target))
    {
        const unsigned was = mlir::cast<mlir::IntegerType>(from).getWidth();
        const unsigned to  = mlir::cast<mlir::IntegerType>(target).getWidth();
        if (was > to)
        {
            return mlir::LLVM::TruncOp::create(rewriter, loc, target, value);
        }
        return isSigned ? mlir::Value{mlir::LLVM::SExtOp::create(rewriter, loc, target, value)}
                        : mlir::Value{mlir::LLVM::ZExtOp::create(rewriter, loc, target, value)};
    }
    if (mlir::isa<mlir::FloatType>(from) && mlir::isa<mlir::FloatType>(target))
    {
        const unsigned was = mlir::cast<mlir::FloatType>(from).getWidth();
        const unsigned to  = mlir::cast<mlir::FloatType>(target).getWidth();
        return (was > to) ? mlir::Value{mlir::LLVM::FPTruncOp::create(rewriter, loc, target, value)}
                          : mlir::Value{mlir::LLVM::FPExtOp::create(rewriter, loc, target, value)};
    }
    return value;
}

struct LoadMemberLowering final : public MemberAccess<mlir::dsdl::LoadMemberOp>
{
    using MemberAccess<mlir::dsdl::LoadMemberOp>::MemberAccess;

    mlir::LogicalResult matchAndRewrite(mlir::dsdl::LoadMemberOp         op,
                                        OpAdaptor                        adaptor,
                                        mlir::ConversionPatternRewriter& rewriter) const override
    {
        const mlir::Value at   = address(op, adaptor.getObject(), rewriter);
        const mlir::Type  held = memberType(op, false);
        if (!at || !held)
        {
            return mlir::failure();
        }
        mlir::Value loaded = mlir::LLVM::LoadOp::create(rewriter, op.getLoc(), held, at);
        rewriter.replaceOp(
            op,
            fit(rewriter, op.getLoc(), loaded, op.getValue().getType(), op->hasAttr("llvmdsdl.is_signed")));
        return mlir::success();
    }
};

struct StoreMemberLowering final : public MemberAccess<mlir::dsdl::StoreMemberOp>
{
    using MemberAccess<mlir::dsdl::StoreMemberOp>::MemberAccess;

    mlir::LogicalResult matchAndRewrite(mlir::dsdl::StoreMemberOp        op,
                                        OpAdaptor                        adaptor,
                                        mlir::ConversionPatternRewriter& rewriter) const override
    {
        const mlir::Value at   = address(op, adaptor.getObject(), rewriter);
        const mlir::Type  held = memberType(op, false);
        if (!at || !held)
        {
            return mlir::failure();
        }
        rewriter.replaceOpWithNewOp<mlir::LLVM::StoreOp>(
            op, fit(rewriter, op.getLoc(), adaptor.getValue(), held, false), at);
        return mlir::success();
    }
};

struct LoadElementLowering final : public MemberAccess<mlir::dsdl::LoadElementOp>
{
    using MemberAccess<mlir::dsdl::LoadElementOp>::MemberAccess;

    mlir::LogicalResult matchAndRewrite(mlir::dsdl::LoadElementOp        op,
                                        OpAdaptor                        adaptor,
                                        mlir::ConversionPatternRewriter& rewriter) const override
    {
        const mlir::Value at   = address(op, adaptor.getObject(), rewriter, adaptor.getIndex());
        const mlir::Type  held = memberType(op, true);
        if (!at || !held)
        {
            return mlir::failure();
        }
        mlir::Value loaded = mlir::LLVM::LoadOp::create(rewriter, op.getLoc(), held, at);
        rewriter.replaceOp(
            op,
            fit(rewriter, op.getLoc(), loaded, op.getValue().getType(), op->hasAttr("llvmdsdl.is_signed")));
        return mlir::success();
    }
};

struct StoreElementLowering final : public MemberAccess<mlir::dsdl::StoreElementOp>
{
    using MemberAccess<mlir::dsdl::StoreElementOp>::MemberAccess;

    mlir::LogicalResult matchAndRewrite(mlir::dsdl::StoreElementOp       op,
                                        OpAdaptor                        adaptor,
                                        mlir::ConversionPatternRewriter& rewriter) const override
    {
        const mlir::Value at   = address(op, adaptor.getObject(), rewriter, adaptor.getIndex());
        const mlir::Type  held = memberType(op, true);
        if (!at || !held)
        {
            return mlir::failure();
        }
        rewriter.replaceOpWithNewOp<mlir::LLVM::StoreOp>(
            op, fit(rewriter, op.getLoc(), adaptor.getValue(), held, false), at);
        return mlir::success();
    }
};

struct MemberAddrLowering final : public MemberAccess<mlir::dsdl::MemberAddrOp>
{
    using MemberAccess<mlir::dsdl::MemberAddrOp>::MemberAccess;

    mlir::LogicalResult matchAndRewrite(mlir::dsdl::MemberAddrOp         op,
                                        OpAdaptor                        adaptor,
                                        mlir::ConversionPatternRewriter& rewriter) const override
    {
        const mlir::Value at = address(op, adaptor.getObject(), rewriter);
        if (!at)
        {
            return mlir::failure();
        }
        rewriter.replaceOp(op, at);
        return mlir::success();
    }
};

struct ElementAddrLowering final : public MemberAccess<mlir::dsdl::ElementAddrOp>
{
    using MemberAccess<mlir::dsdl::ElementAddrOp>::MemberAccess;

    mlir::LogicalResult matchAndRewrite(mlir::dsdl::ElementAddrOp        op,
                                        OpAdaptor                        adaptor,
                                        mlir::ConversionPatternRewriter& rewriter) const override
    {
        const mlir::Value at = address(op, adaptor.getObject(), rewriter, adaptor.getIndex());
        if (!at)
        {
            return mlir::failure();
        }
        rewriter.replaceOp(op, at);
        return mlir::success();
    }
};

//===----------------------------------------------------------------------===//

struct ConvertDSDLToLLVMPass
    : public mlir::PassWrapper<ConvertDSDLToLLVMPass, mlir::OperationPass<mlir::ModuleOp>>
{
    ConvertDSDLToLLVMPass() = default;
    ConvertDSDLToLLVMPass(const ConvertDSDLToLLVMPass& other)
        : mlir::PassWrapper<ConvertDSDLToLLVMPass, mlir::OperationPass<mlir::ModuleOp>>(other)
    {
    }

    llvm::StringRef getArgument() const final
    {
        return "convert-dsdl-to-llvm";
    }
    llvm::StringRef getDescription() const final
    {
        return "Lower DSDL plan operations into the LLVM dialect";
    }
    void getDependentDialects(mlir::DialectRegistry& registry) const override
    {
        registry.insert<mlir::LLVM::LLVMDialect>();
    }

    /// What the target spells `size_t` at, for a module carrying no data layout of its own.
    Pass::Option<unsigned> sizeBitsOption{*this,
                                          "size-bits",
                                          llvm::cl::desc("Width of the target's size_t in bits"),
                                          llvm::cl::init(64)};

    // NOLINTNEXTLINE(misc-override-with-different-visibility) -- MLIR declares passes this way.
    void runOnOperation() override
    {
        auto module = getOperation();

        // An LLVM pointer carries no pointee, so every dialect pointer converts to the same
        // type and the spelling `!dsdl.opaque` holds is simply not consulted here. What the C
        // path needs that name for, this path answers with an index instead.
        mlir::TypeConverter converter;
        converter.addConversion([](mlir::Type type) { return type; });
        converter.addConversion([](mlir::dsdl::PtrType ptr) -> mlir::Type {
            return mlir::LLVM::LLVMPointerType::get(ptr.getContext());
        });

        // The struct each plan addresses within, derived from the schema before it is erased.
        const unsigned sizeBits = targetSizeBits(module, static_cast<unsigned>(sizeBitsOption));
        module->setAttr(kSizeBitsAttr,
                        mlir::IntegerAttr::get(mlir::IntegerType::get(&getContext(), 32), sizeBits));
        const llvm::DenseMap<llvm::StringRef, mlir::Type>   composites = buildStructs(module, sizeBits);
        const llvm::DenseMap<llvm::StringRef, std::string>  bodies     = buildSerdesBodies(module);

        mlir::RewritePatternSet patterns(&getContext());
        patterns.add<LoadMemberLowering,
                     StoreMemberLowering,
                     LoadElementLowering,
                     StoreElementLowering,
                     MemberAddrLowering,
                     ElementAddrLowering>(converter, &getContext(), composites);
        patterns.add<IsNullLowering,
                     LoadScalarLowering,
                     StoreScalarLowering,
                     BufferAtLowering,
                     LocalLowering,
                     BufferOrEmptyLowering,
                     WriteBitsLowering,
                     ReadBitsLowering,
                     BitWriteLowering,
                     BitReadLowering>(converter, &getContext());
        patterns.add<CallSerdesLowering>(converter, &getContext(), bodies);
        mlir::populateFunctionOpInterfaceTypeConversionPattern<mlir::func::FuncOp>(patterns, converter);
        // A signature is not only its arguments. A body that answers with a pointer would
        // otherwise leave the conversion stranded at its own return.
        mlir::populateReturnOpTypeConversionPattern(patterns, converter);
        mlir::populateCallOpTypeConversionPattern(patterns, converter);

        mlir::ConversionTarget target(getContext());
        target.addLegalDialect<mlir::LLVM::LLVMDialect, mlir::arith::ArithDialect, mlir::func::FuncDialect,
                               mlir::scf::SCFDialect>();
        target.addLegalDialect<mlir::dsdl::DSDLDialect>();
        target.addIllegalOp<mlir::dsdl::LoadMemberOp,
                            mlir::dsdl::StoreMemberOp,
                            mlir::dsdl::LoadElementOp,
                            mlir::dsdl::StoreElementOp,
                            mlir::dsdl::MemberAddrOp,
                            mlir::dsdl::ElementAddrOp,
                            mlir::dsdl::IsNullOp,
                            mlir::dsdl::LoadScalarOp,
                            mlir::dsdl::StoreScalarOp,
                            mlir::dsdl::BufferAtOp,
                            mlir::dsdl::LocalOp,
                            mlir::dsdl::BufferOrEmptyOp,
                            mlir::dsdl::WriteBitsOp,
                            mlir::dsdl::ReadBitsOp,
                            mlir::dsdl::BitWriteOp,
                            mlir::dsdl::BitReadOp,
                            mlir::dsdl::CallSerdesOp>();
        target.addDynamicallyLegalOp<mlir::func::FuncOp>([&converter](mlir::func::FuncOp fn) {
            return converter.isSignatureLegal(fn.getFunctionType());
        });
        target.addDynamicallyLegalOp<mlir::func::ReturnOp>([&converter](mlir::func::ReturnOp op) {
            return converter.isLegal(op.getOperandTypes());
        });
        target.addDynamicallyLegalOp<mlir::func::CallOp>([&converter](mlir::func::CallOp op) {
            return converter.isLegal(op.getOperandTypes()) && converter.isLegal(op.getResultTypes());
        });

        if (mlir::failed(mlir::applyPartialConversion(module, target, std::move(patterns))))
        {
            module.emitError("failed to lower dsdl plan operations to the llvm dialect");
            signalPassFailure();
            return;
        }

        // The schema is description, not code: it says what the type is, which the plan bodies
        // have already been built from. An object has nowhere to put it.
        mlir::SmallVector<mlir::Operation*, 16> schemas;
        for (mlir::Operation& op : module.getBodyRegion().front())
        {
            if (op.getName().getStringRef() == "dsdl.schema")
            {
                schemas.push_back(&op);
            }
        }
        for (mlir::Operation* schema : schemas)
        {
            schema->erase();
        }
    }
};

}  // namespace

std::unique_ptr<mlir::Pass> createConvertDSDLToLLVMPass()
{
    return std::make_unique<ConvertDSDLToLLVMPass>();
}

std::unique_ptr<mlir::Pass> createConvertDSDLToLLVMPass(const unsigned sizeBits)
{
    auto pass           = std::make_unique<ConvertDSDLToLLVMPass>();
    pass->sizeBitsOption = sizeBits;
    return pass;
}

void registerDSDLToLLVMPasses()
{
    static bool once = false;
    if (once)
    {
        return;
    }
    once = true;
    static mlir::PassRegistration<ConvertDSDLToLLVMPass> const reg;
}

}  // namespace llvmdsdl
