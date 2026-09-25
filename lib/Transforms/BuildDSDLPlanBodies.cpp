//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Builds serialisation plan bodies as dialect operations.
///
/// `build-dsdl-plan-bodies` turns every `dsdl.serialization_plan` into a serialise and a
/// deserialise `func.func` over the plan operations, for whichever target converts them next. A
/// plan it cannot express is an error; there is no other way to render one.
///
//===----------------------------------------------------------------------===//
#include "llvmdsdl/IR/DSDLDialect.h"
#include "llvmdsdl/IR/DSDLOps.h"
#include "llvmdsdl/IR/DSDLTypes.h"
#include "llvmdsdl/Support/DefinitionNaming.h"
#include "llvmdsdl/Transforms/LoweredSerDesContract.h"
#include "llvmdsdl/Transforms/LoweredSerDesContractValidation.h"
#include "llvmdsdl/Transforms/Passes.h"
#include "llvmdsdl/Transforms/PlanSteps.h"

#include <llvm/ADT/SmallVector.h>
#include <llvm/ADT/StringRef.h>
#include <mlir/Dialect/Arith/IR/Arith.h>
#include <mlir/Dialect/Func/IR/FuncOps.h>
#include <mlir/Dialect/SCF/IR/SCF.h>
#include <mlir/IR/Attributes.h>
#include <mlir/IR/Builders.h>
#include <mlir/IR/BuiltinAttributes.h>
#include <mlir/IR/BuiltinOps.h>
#include <mlir/IR/BuiltinTypes.h>
#include <mlir/IR/DialectRegistry.h>
#include <mlir/IR/Location.h>
#include <mlir/IR/MLIRContext.h>
#include <mlir/IR/Matchers.h>
#include <mlir/IR/Operation.h>
#include <mlir/IR/OperationSupport.h>
#include <mlir/IR/PatternMatch.h>
#include <mlir/IR/Region.h>
#include <mlir/IR/Value.h>
#include <mlir/IR/ValueRange.h>
#include <mlir/Pass/Pass.h>
#include <mlir/Pass/PassRegistry.h>
#include <mlir/Rewrite/FrozenRewritePatternSet.h>
#include <mlir/Support/LLVM.h>
#include <mlir/Transforms/GreedyPatternRewriteDriver.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace llvmdsdl
{
namespace
{

constexpr std::int64_t kRuntimeErrorInvalidArgument             = 2;
constexpr std::int64_t kRuntimeErrorSerializationBufferTooSmall = 3;
constexpr std::int64_t kDelimiterHeaderBits                     = 32;

/// @brief What a step carries forward: how far into the wire it got, and what went wrong.
///
/// While every step so far had one width, the offset is also a number, and `staticBits` holds
/// it: on the path where nothing has failed, `bitOffset` equals it. A step whose width the
/// object or the wire decides -- a variable-length array, a nested type of varying size, a
/// union -- ends that, and from there on only the value is known. What usually survives it is
/// the offset modulo eight, which `staticResidue` holds: an array of whole-byte elements, a
/// nested composite and a delimiter header each move the offset by whole bytes, so the bits to
/// the next boundary stay a number after them.
struct PlanCursor final
{
    mlir::Value                 bitOffset;
    mlir::Value                 error;
    std::optional<std::int64_t> staticBits;
    std::optional<std::int64_t> staticResidue;
};

/// @brief The offset modulo eight that every arm of a union agrees on after it, or nothing.
struct ResidueMeet final
{
    bool                        any{false};
    std::optional<std::int64_t> value;

    void meet(const std::optional<std::int64_t> residue)
    {
        if (!any)
        {
            value = residue;
        }
        else if (value != residue)
        {
            value = std::nullopt;
        }
        any = true;
    }
};

/// @brief The roles a plan's own structure gives to results that no operation in it describes.
constexpr llvm::StringLiteral RoleOffset   = "offset";
constexpr llvm::StringLiteral RoleError    = "error";
constexpr llvm::StringLiteral RoleRejected = "rejected";
constexpr llvm::StringLiteral RoleSize     = "size";

/// @brief Names what each result of @p op holds, for a backend to declare it by.
///
/// The offset a plan threads through its steps and the error it carries alongside are `scf`
/// results of the plan's own shape, and the size a getter may read is a select on whether its
/// buffer is null. This pass knows which is which as it builds them, and writes that down for the
/// backends.
void stampResultRoles(mlir::Operation* const op, const llvm::ArrayRef<llvm::StringRef> roles)
{
    mlir::SmallVector<mlir::Attribute, 2> names;
    names.reserve(roles.size());
    for (const llvm::StringRef role : roles)
    {
        names.push_back(mlir::StringAttr::get(op->getContext(), role));
    }
    op->setAttr("llvmdsdl.result_roles", mlir::ArrayAttr::get(op->getContext(), names));
}

/// @brief Why one field cannot be built as operations, or nothing when it can.
///
/// Shared by the two shapes a plan comes in. A union is one field per option and a struct is
/// a sequence of them, but what a single field needs is the same either way, and having the
/// two disagree is how an option gets accepted that the arm builder cannot emit.
std::optional<std::string> unsupportedFieldReason(const PlanStep& step)
{
    const std::string field = "field '" + step.name + "'";
    if (!isSupportedArrayKind(step.arrayKind))
    {
        return field + " has array kind '" + step.arrayKind + "'";
    }
    if (step.arrayKind != "none")
    {
        if (isVariableArrayKind(step.arrayKind))
        {
            if ((step.arrayLengthPrefixBits <= 0) || (step.arrayLengthPrefixBits > 64))
            {
                return field + " has a length prefix of " + std::to_string(step.arrayLengthPrefixBits) + " bits";
            }
            if (step.serArrayLengthPrefixHelper.empty() || step.deserArrayLengthPrefixHelper.empty())
            {
                return field + " carries no array-length-prefix helpers; run lower-dsdl-exec first";
            }
            if (step.arrayLengthValidateHelper.empty())
            {
                return field + " carries no array-length validation helper; run lower-dsdl-exec first";
            }
        }
        else if (step.arrayCapacity <= 0)
        {
            return field + " is a fixed array of " + std::to_string(step.arrayCapacity) + " elements";
        }
    }
    if (step.scalarCategory == "composite")
    {
        if (step.compositeFullName.empty())
        {
            return field + " names no composite type";
        }
        if (!step.compositeSealed && step.delimiterValidateHelper.empty())
        {
            return field + " carries no delimiter-header validation helper; run lower-dsdl-exec first";
        }
        if (step.heldAsView && !step.compositeFixedBits)
        {
            return field + " is held as a view of a composite whose plan has no single width";
        }
        return std::nullopt;
    }
    if (step.scalarCategory == "bool")
    {
        if (step.bitLength != 1)
        {
            return field + " is a bool of " + std::to_string(step.bitLength) + " bits";
        }
        return std::nullopt;
    }
    if (step.scalarCategory == "float")
    {
        if ((step.bitLength != 16) && (step.bitLength != 32) && (step.bitLength != 64))
        {
            return field + " is a float of " + std::to_string(step.bitLength) + " bits";
        }
        if (step.serFloatHelper.empty() || step.deserFloatHelper.empty())
        {
            return field + " carries no float helpers; run lower-dsdl-exec first";
        }
        return std::nullopt;
    }
    if ((step.bitLength <= 0) || (step.bitLength > 64))
    {
        return field + " is " + std::to_string(step.bitLength) + " bits wide";
    }
    if (step.scalarCategory == "signed")
    {
        if (step.serSignedHelper.empty() || step.deserSignedHelper.empty())
        {
            return field + " carries no signed helpers; run lower-dsdl-exec first";
        }
        return std::nullopt;
    }
    if ((step.scalarCategory == "unsigned") || (step.scalarCategory == "byte") || (step.scalarCategory == "utf8"))
    {
        if (step.serUnsignedHelper.empty() || step.deserUnsignedHelper.empty())
        {
            return field + " carries no unsigned helpers; run lower-dsdl-exec first";
        }
        return std::nullopt;
    }
    return field + " has scalar category '" + step.scalarCategory + "', which no builder covers";
}

/// @brief Why a plan cannot be built as operations, or nothing when it can.
///
/// Every reason is malformed or unlowered input: a width the wire format does not allow, a
/// helper the lowering did not stamp, a member the backend did not name. A plan that arrives
/// well-formed is built, and one that does not is an error. There is no second way to render it.
std::optional<std::string> unsupportedPlanReason(const std::vector<PlanStep>& steps,
                                                 const bool                   isUnion,
                                                 const std::int64_t           unionTagBits,
                                                 const bool                   constrainsCapacity,
                                                 llvm::StringRef              capacityCheckSymbol,
                                                 llvm::StringRef              unionTagValidateSymbol,
                                                 llvm::StringRef              unionTagSerializeHelper,
                                                 llvm::StringRef              unionTagDeserializeHelper)
{
    if (constrainsCapacity && capacityCheckSymbol.empty())
    {
        return "plan carries no capacity-check helper; run lower-dsdl-exec first";
    }
    if (isUnion)
    {
        if ((unionTagBits <= 0) || (unionTagBits > 64))
        {
            return "union tag is " + std::to_string(unionTagBits) + " bits wide";
        }
        if (unionTagSerializeHelper.empty() || unionTagDeserializeHelper.empty())
        {
            return "union carries no tag IO helpers; run lower-dsdl-exec first";
        }
        if (unionTagValidateSymbol.empty())
        {
            return "union carries no tag validation helper; run lower-dsdl-exec first";
        }
        if (std::ranges::none_of(steps, [](const PlanStep& step) { return step.kind == PlanStepKind::Field; }))
        {
            return "union has no options";
        }
    }
    for (const auto& step : steps)
    {
        if (step.kind == PlanStepKind::Align)
        {
            if (step.bits <= 0)
            {
                return "alignment step of " + std::to_string(step.bits) + " bits";
            }
            continue;
        }
        if (step.kind == PlanStepKind::Padding)
        {
            if (isUnion)
            {
                return "union holds a void field";
            }
            if (step.bits <= 0)
            {
                return "void field of " + std::to_string(step.bits) + " bits";
            }
            continue;
        }
        if (const auto reason = unsupportedFieldReason(step))
        {
            return reason;
        }
    }
    return std::nullopt;
}

mlir::Value constantI64(mlir::OpBuilder& b, mlir::Location loc, const std::int64_t value)
{
    return mlir::arith::ConstantOp::create(b, loc, b.getI64IntegerAttr(value));
}

mlir::Value constantI8(mlir::OpBuilder& b, mlir::Location loc, const std::int64_t value)
{
    return mlir::arith::ConstantOp::create(b, loc, b.getI8IntegerAttr(static_cast<std::int8_t>(value)));
}

mlir::Value isHealthy(mlir::OpBuilder& b, mlir::Location loc, mlir::Value error)
{
    return mlir::arith::CmpIOp::create(b, loc, mlir::arith::CmpIPredicate::eq, error, constantI8(b, loc, 0));
}

/// @brief The cursor @p bits past @p cursor, carrying @p error, and still a number when the
///        cursor was one.
///
/// A value that is not yet a constant is added to rather than replaced, so that whatever
/// defined it is still read.
PlanCursor advancedBy(mlir::OpBuilder&   b,
                      mlir::Location     loc,
                      const PlanCursor&  cursor,
                      mlir::Value        error,
                      const std::int64_t bits)
{
    const auto residue = cursor.staticResidue ? std::optional{(*cursor.staticResidue + bits) % 8} : std::nullopt;
    if (cursor.staticBits && mlir::matchPattern(cursor.bitOffset, mlir::m_Constant()))
    {
        const std::int64_t at = *cursor.staticBits + bits;
        return PlanCursor{constantI64(b, loc, at), error, at, residue};
    }
    return PlanCursor{mlir::arith::AddIOp::create(b, loc, cursor.bitOffset, constantI64(b, loc, bits)),
                      error,
                      cursor.staticBits ? std::optional{*cursor.staticBits + bits} : std::nullopt,
                      residue};
}

/// @brief The count @p value holds when it is a constant.
std::optional<std::int64_t> constantCount(mlir::Value value)
{
    llvm::APInt count;
    if (mlir::matchPattern(value, mlir::m_ConstantInt(&count)))
    {
        return count.getSExtValue();
    }
    return std::nullopt;
}

/// @brief The cursor past @p count elements of @p elementBits each, carrying @p error.
PlanCursor advancedByRun(mlir::OpBuilder&   b,
                         mlir::Location     loc,
                         const PlanCursor&  cursor,
                         mlir::Value        error,
                         mlir::Value        count,
                         const std::int64_t elementBits)
{
    if (const auto elements = constantCount(count))
    {
        return advancedBy(b, loc, cursor, error, *elements * elementBits);
    }
    // A run of whole-byte elements moves the offset by whole bytes, however many there are.
    const mlir::Value run = mlir::arith::MulIOp::create(b, loc, count, constantI64(b, loc, elementBits));
    return PlanCursor{mlir::arith::AddIOp::create(b, loc, cursor.bitOffset, run),
                      error,
                      std::nullopt,
                      ((elementBits % 8) == 0) ? cursor.staticResidue : std::nullopt};
}

/// @brief An `scf.for` over `[0, count)` carrying @p carried, with the body left to the caller.
mlir::scf::ForOp countedLoop(mlir::OpBuilder& b, mlir::Location loc, mlir::Value count, mlir::ValueRange carried)
{
    const mlir::Value zero  = mlir::arith::ConstantIndexOp::create(b, loc, 0);
    const mlir::Value one   = mlir::arith::ConstantIndexOp::create(b, loc, 1);
    const mlir::Value bound = mlir::arith::IndexCastOp::create(b, loc, b.getIndexType(), count);
    return mlir::scf::ForOp::create(b, loc, zero, bound, one, carried);
}

/// @brief Runs @p body only while @p error is clear, and answers the error it leaves.
///
/// The shape of one element inside a counted loop: the loop carries the error and nothing
/// else, so the guard answers with the code alone.
template <typename BodyFn>
mlir::Value errorGuarded(mlir::OpBuilder& b, mlir::Location loc, mlir::Value error, BodyFn body)
{
    auto guard = mlir::scf::IfOp::create(b, loc, mlir::TypeRange{b.getIntegerType(8)}, isHealthy(b, loc, error), true);
    stampResultRoles(guard, {RoleError});
    {
        mlir::OpBuilder::InsertionGuard const g(b);
        b.setInsertionPointToStart(guard.thenBlock());
        const mlir::Value next = body();
        mlir::scf::YieldOp::create(b, loc, mlir::ValueRange{next});
    }
    {
        mlir::OpBuilder::InsertionGuard const g(b);
        b.setInsertionPointToStart(guard.elseBlock());
        mlir::scf::YieldOp::create(b, loc, mlir::ValueRange{error});
    }
    return guard.getResult(0);
}

bool stepIsComposite(const PlanStep& step)
{
    return !step.compositeFullName.empty();
}

/// @brief The identity of a step's nested type, as `!dsdl.object` carries it.
std::string nestedIdentity(const PlanStep& step)
{
    return planIdentity(step.compositeFullName, step.compositeMajor, step.compositeMinor, {});
}

/// @brief The nested type's body for the direction, as this pass names it.
mlir::FlatSymbolRefAttr nestedCallee(mlir::OpBuilder& b, const PlanStep& step, const bool writing)
{
    return mlir::FlatSymbolRefAttr::get(b.getContext(),
                                        planBodySymbol(step.compositeFullName,
                                                       step.compositeMajor,
                                                       step.compositeMinor,
                                                       {},
                                                       writing));
}

/// @brief The nested type's initialise body, as this pass names it.
mlir::FlatSymbolRefAttr nestedInitializeCallee(mlir::OpBuilder& b, const PlanStep& step)
{
    return mlir::FlatSymbolRefAttr::get(b.getContext(),
                                        planInitializeSymbol(step.compositeFullName,
                                                             step.compositeMajor,
                                                             step.compositeMinor,
                                                             {}));
}

bool stepIsBitpackedArray(const PlanStep& step);

/// A bool array moves as one run of bits, and an array of composites element by element; both
/// are defined below, beside the other nesting builders, and reached from the array builders
/// above them.
PlanCursor buildBitpackedArray(mlir::OpBuilder& b,
                               mlir::Location   loc,
                               const PlanStep&  step,
                               mlir::Value      object,
                               mlir::Value      buffer,
                               mlir::Value      capacityBytes,
                               PlanCursor       cursor,
                               mlir::Value      count,
                               bool             writing);

PlanCursor buildCompositeElementLoop(mlir::OpBuilder& b,
                                     mlir::Location   loc,
                                     const PlanStep&  step,
                                     mlir::Value      object,
                                     mlir::Value      buffer,
                                     mlir::Value      capacityBytes,
                                     PlanCursor       cursor,
                                     mlir::Value      count,
                                     bool             writing);

bool stepIsArray(const PlanStep& step)
{
    return !step.arrayKind.empty() && (step.arrayKind != "none");
}

/// @brief The value type a step's saturation helper and wire access are expressed in.
mlir::Type stepValueType(mlir::OpBuilder& b, const PlanStep& step)
{
    if (step.scalarCategory == "float")
    {
        // An upcast, not a copy: an MLIR type is a handle to interned storage, and the
        // derived class adds nothing the base does not already point at.
        if (step.bitLength <= 32)
        {
            return mlir::cast<mlir::Type>(b.getF32Type());
        }
        return mlir::cast<mlir::Type>(b.getF64Type());
    }
    return b.getIntegerType(64);
}

/// @brief The width of one array element's storage: a float's holder or an integer's.
std::int64_t storageBitsFor(const PlanStep& step)
{
    if (step.scalarCategory == "float")
    {
        return (step.bitLength <= 32) ? 32 : 64;
    }
    return holderWidthFor(step.bitLength);
}

bool stepIsFixedArray(const PlanStep& step)
{
    return stepIsArray(step) && !isVariableArrayKind(step.arrayKind);
}

/// @brief Marks an access whose member is signed, which decides how a load widens it.
///
/// The C path gets this from the member's declared type; an object lowering has only the
/// struct, where a width carries no sign.
void markSigned(mlir::Operation* op, const PlanStep& step)
{
    if (step.scalarCategory == "signed")
    {
        op->setAttr("llvmdsdl.is_signed", mlir::UnitAttr::get(op->getContext()));
    }
}

std::string serHelperFor(const PlanStep& step)
{
    if (step.scalarCategory == "float")
    {
        return step.serFloatHelper;
    }
    if (step.scalarCategory == "signed")
    {
        return step.serSignedHelper;
    }
    return step.serUnsignedHelper;
}

std::string deserHelperFor(const PlanStep& step)
{
    if (step.scalarCategory == "float")
    {
        return step.deserFloatHelper;
    }
    if (step.scalarCategory == "signed")
    {
        return step.deserSignedHelper;
    }
    return step.deserUnsignedHelper;
}

/// @brief Passes @p value through a lowered helper, named by symbol.
///
/// The builder pass establishes that every helper a plan names is
/// present before a body is built, so a call here always has a callee.
mlir::Value applyHelper(mlir::OpBuilder& b, mlir::Location loc, llvm::StringRef helper, mlir::Value value)
{
    auto call = mlir::func::CallOp::create(b,
                                           loc,
                                           mlir::SymbolRefAttr::get(b.getContext(), helper),
                                           mlir::TypeRange{value.getType()},
                                           mlir::ValueRange{value});
    return call.getResult(0);
}

/// @brief Normalises a scalar through the plan's own helper, in the direction given.
///
/// A bool has no helper: it is one bit, with nothing to saturate or extend.
mlir::Value normaliseScalar(mlir::OpBuilder& b,
                            mlir::Location   loc,
                            const PlanStep&  step,
                            mlir::Value      value,
                            const bool       writing)
{
    if (step.scalarCategory == "bool")
    {
        return value;
    }
    return applyHelper(b, loc, writing ? serHelperFor(step) : deserHelperFor(step), value);
}

/// @brief Keeps the first error. A later check does not get to overwrite an earlier failure.
///
/// The plan stops at the first thing that goes wrong, so a check that runs after one has
/// already failed reports what was already known rather than what it makes of state the
/// failure left behind.
mlir::Value foldError(mlir::OpBuilder& b, mlir::Location loc, mlir::Value existing, mlir::Value next)
{
    return mlir::arith::SelectOp::create(b, loc, isHealthy(b, loc, existing), next, existing);
}

/// @brief Calls a helper that answers an error code.
mlir::Value callErrorHelper(mlir::OpBuilder& b, mlir::Location loc, llvm::StringRef symbol, mlir::ValueRange arguments)
{
    auto call = mlir::func::CallOp::create(b,
                                           loc,
                                           mlir::SymbolRefAttr::get(b.getContext(), symbol),
                                           mlir::TypeRange{b.getIntegerType(8)},
                                           arguments);
    return call.getResult(0);
}

/// @brief Writes one already-normalised value and advances the cursor past it.
PlanCursor emitWrite(mlir::OpBuilder&   b,
                     mlir::Location     loc,
                     mlir::Value        buffer,
                     mlir::Value        capacityBytes,
                     PlanCursor         cursor,
                     mlir::Value        value,
                     const std::int64_t width,
                     const bool         isSigned)
{
    auto write = mlir::dsdl::WriteBitsOp::create(b,
                                                 loc,
                                                 b.getIntegerType(8),
                                                 buffer,
                                                 capacityBytes,
                                                 cursor.bitOffset,
                                                 value,
                                                 b.getI64IntegerAttr(width),
                                                 isSigned ? b.getUnitAttr() : nullptr);
    return advancedBy(b, loc, cursor, write.getError(), width);
}

/// @brief Writes @p bits zero bits at the cursor: a void field, or the padding to a boundary.
///
/// One write per 64 bits, which is the most the wire primitives take at once. A single bit
/// travels as a bool, as every one-bit write does.
PlanCursor emitZeroBits(mlir::OpBuilder& b,
                        mlir::Location   loc,
                        mlir::Value      buffer,
                        mlir::Value      capacityBytes,
                        PlanCursor       cursor,
                        std::int64_t     bits)
{
    while (bits > 0)
    {
        const std::int64_t width = std::min<std::int64_t>(bits, 64);
        const mlir::Value  zero =
            (width == 1) ? mlir::arith::ConstantOp::create(b, loc, b.getBoolAttr(false)) : constantI64(b, loc, 0);
        cursor = emitWrite(b, loc, buffer, capacityBytes, cursor, zero, width, false);
        bits -= width;
    }
    return cursor;
}

/// @brief Runs @p body only while nothing has failed, threading the cursor either way.
///
/// The guard answers the offset only when the body left it a value. When the body left it a
/// number, the next step spells that number itself, and nothing reads the offset on the path
/// that failed: the guard is rebuilt to answer the error alone, so no target declares a
/// variable for a result nothing uses.
template <typename BodyFn>
PlanCursor guarded(mlir::OpBuilder& b, mlir::Location loc, PlanCursor cursor, BodyFn body)
{
    auto i64Ty = b.getIntegerType(64);
    auto i8Ty  = b.getIntegerType(8);

    auto guard = mlir::scf::IfOp::create(b, loc, mlir::TypeRange{i64Ty, i8Ty}, isHealthy(b, loc, cursor.error), true);
    PlanCursor next;
    {
        mlir::OpBuilder::InsertionGuard const g(b);
        b.setInsertionPointToStart(guard.thenBlock());
        next = body(cursor);
        mlir::scf::YieldOp::create(b, loc, mlir::ValueRange{next.bitOffset, next.error});
    }
    {
        mlir::OpBuilder::InsertionGuard const g(b);
        b.setInsertionPointToStart(guard.elseBlock());
        mlir::scf::YieldOp::create(b, loc, mlir::ValueRange{cursor.bitOffset, cursor.error});
    }
    if (!next.staticBits)
    {
        stampResultRoles(guard, {RoleOffset, RoleError});
        return PlanCursor{guard.getResult(0), guard.getResult(1), std::nullopt, next.staticResidue};
    }

    mlir::scf::IfOp slim;
    {
        mlir::OpBuilder::InsertionGuard const g(b);
        b.setInsertionPoint(guard);
        slim = mlir::scf::IfOp::create(b, loc, mlir::TypeRange{i8Ty}, guard.getCondition(), true);
    }
    slim.getThenRegion().takeBody(guard.getThenRegion());
    slim.getElseRegion().takeBody(guard.getElseRegion());
    for (mlir::Region* region : {&slim.getThenRegion(), &slim.getElseRegion()})
    {
        auto yield = mlir::cast<mlir::scf::YieldOp>(region->front().getTerminator());
        yield->setOperands(mlir::ValueRange{yield.getOperand(1)});
    }
    guard.erase();
    stampResultRoles(slim, {RoleError});
    return PlanCursor{constantI64(b, loc, *next.staticBits), slim.getResult(0), next.staticBits, next.staticResidue};
}

/// @brief Serialises one scalar field.
PlanCursor buildScalarWrite(mlir::OpBuilder& b,
                            mlir::Location   loc,
                            const PlanStep&  step,
                            mlir::Value      object,
                            mlir::Value      buffer,
                            mlir::Value      capacityBytes,
                            PlanCursor       cursor)
{
    return guarded(b, loc, cursor, [&](PlanCursor inner) {
        const mlir::Type valueType = stepValueType(b, step);
        mlir::Value member = mlir::dsdl::LoadMemberOp::create(b, loc, valueType, object, b.getStringAttr(step.name));
        markSigned(member.getDefiningOp(), step);
        member = normaliseScalar(b, loc, step, member, true);
        return emitWrite(b, loc, buffer, capacityBytes, inner, member, step.bitLength, step.scalarCategory == "signed");
    });
}

/// @brief Serialises one variable-length array: a validated count, its prefix, then elements.
PlanCursor buildArrayWrite(mlir::OpBuilder& b,
                           mlir::Location   loc,
                           const PlanStep&  step,
                           mlir::Value      object,
                           mlir::Value      buffer,
                           mlir::Value      capacityBytes,
                           PlanCursor       cursor)
{
    return guarded(b, loc, cursor, [&](PlanCursor inner) {
        auto       i64Ty = b.getIntegerType(64);
        const bool fixed = stepIsFixedArray(step);

        // A fixed array's length is in its declaration, so there is nothing to read from the
        // object, nothing that could be out of range, and nothing to announce on the wire.
        mlir::Value count = constantI64(b, loc, step.arrayCapacity);
        if (!fixed)
        {
            count = mlir::dsdl::ArrayLengthOp::create(b, loc, i64Ty, object, b.getStringAttr(step.name));

            // A count past the declared capacity is the plan's error to report, not the wire's.
            inner.error = callErrorHelper(b, loc, step.arrayLengthValidateHelper, mlir::ValueRange{count});
        }

        return guarded(b, loc, inner, [&](PlanCursor afterCheck) {
            PlanCursor afterPrefix = afterCheck;
            if (!fixed)
            {
                const mlir::Value wireLength = applyHelper(b, loc, step.serArrayLengthPrefixHelper, count);
                afterPrefix =
                    emitWrite(b, loc, buffer, capacityBytes, afterCheck, wireLength, step.arrayLengthPrefixBits, false);
            }

            if (stepIsComposite(step))
            {
                return buildCompositeElementLoop(b, loc, step, object, buffer, capacityBytes, afterPrefix, count, true);
            }
            if (stepIsBitpackedArray(step))
            {
                return buildBitpackedArray(b, loc, step, object, buffer, capacityBytes, afterPrefix, count, true);
            }

            // Counted with scf.for, whose induction variable is not among its results. The
            // loop answers the error alone; where each element lies is the start plus the
            // index times the width, and where the array ends is the start plus the run.
            const mlir::Value start = afterPrefix.bitOffset;
            const mlir::Value width = constantI64(b, loc, step.bitLength);
            auto              loop  = countedLoop(b, loc, count, mlir::ValueRange{afterPrefix.error});
            stampResultRoles(loop, {RoleError});
            {
                mlir::OpBuilder::InsertionGuard const g(b);
                b.setInsertionPointToStart(loop.getBody());
                const mlir::Value index = mlir::arith::IndexCastOp::create(b, loc, i64Ty, loop.getInductionVar());
                const mlir::Value at =
                    mlir::arith::AddIOp::create(b, loc, start, mlir::arith::MulIOp::create(b, loc, index, width));
                // An scf.for runs to its bound, so an element after a failure writes nothing.
                const mlir::Value carried = errorGuarded(b, loc, loop.getRegionIterArg(0), [&]() {
                    const mlir::Type valueType = stepValueType(b, step);
                    mlir::Value element = mlir::dsdl::LoadElementOp::create(b,
                                                                            loc,
                                                                            valueType,
                                                                            object,
                                                                            b.getStringAttr(step.name),
                                                                            index,
                                                                            b.getStringAttr(step.scalarCategory),
                                                                            b.getI64IntegerAttr(storageBitsFor(step)));
                    markSigned(element.getDefiningOp(), step);
                    element = normaliseScalar(b, loc, step, element, true);
                    return emitWrite(b,
                                     loc,
                                     buffer,
                                     capacityBytes,
                                     PlanCursor{at, loop.getRegionIterArg(0), std::nullopt, std::nullopt},
                                     element,
                                     step.bitLength,
                                     step.scalarCategory == "signed")
                        .error;
                });
                mlir::scf::YieldOp::create(b, loc, mlir::ValueRange{carried});
            }
            return advancedByRun(b, loc, afterPrefix, loop.getResult(0), count, step.bitLength);
        });
    });
}

/// @brief Writes zero bits from the cursor up to @p end, one bit at a time.
///
/// For an end that is only known at run time: after a variable-length array of elements that
/// are not whole bytes, or after a union whose arms end at different bits. Everywhere the end is
/// a number, `emitZeroBits` writes the run at once.
PlanCursor buildZeroBitsTo(mlir::OpBuilder& b,
                           mlir::Location   loc,
                           mlir::Value      buffer,
                           mlir::Value      capacityBytes,
                           PlanCursor       cursor,
                           mlir::Value      end)
{
    auto i64Ty = b.getIntegerType(64);
    auto i8Ty  = b.getIntegerType(8);

    auto loop = mlir::scf::WhileOp::create(b,
                                           loc,
                                           mlir::TypeRange{i64Ty, i8Ty},
                                           mlir::ValueRange{cursor.bitOffset, cursor.error});
    stampResultRoles(loop, {RoleOffset, RoleError});
    {
        mlir::OpBuilder::InsertionGuard const g(b);
        mlir::Block*                          before = b.createBlock(&loop.getBefore(), {}, {i64Ty, i8Ty}, {loc, loc});
        b.setInsertionPointToStart(before);
        const mlir::Value more =
            mlir::arith::CmpIOp::create(b, loc, mlir::arith::CmpIPredicate::ult, before->getArgument(0), end);
        const mlir::Value keep = mlir::arith::AndIOp::create(b, loc, more, isHealthy(b, loc, before->getArgument(1)));
        mlir::scf::ConditionOp::create(b, loc, keep, before->getArguments());
    }
    {
        mlir::OpBuilder::InsertionGuard const g(b);
        mlir::Block*                          after = b.createBlock(&loop.getAfter(), {}, {i64Ty, i8Ty}, {loc, loc});
        b.setInsertionPointToStart(after);
        const mlir::Value zeroBit  = mlir::arith::ConstantOp::create(b, loc, b.getBoolAttr(false));
        const mlir::Value incoming = after->getArgument(1);
        const PlanCursor  written  = emitWrite(b,
                                               loc,
                                               buffer,
                                               capacityBytes,
                                               PlanCursor{after->getArgument(0), incoming, std::nullopt, std::nullopt},
                                               zeroBit,
                                               1,
                                               false);
        // Every value a loop carries has to be read in its body. The condition already
        // guarantees this one is clear, so the select always takes the write's own result --
        // but binding it and dropping it leaves a declaration the emitted C never uses.
        const mlir::Value carried =
            mlir::arith::SelectOp::create(b, loc, isHealthy(b, loc, incoming), written.error, incoming);
        mlir::scf::YieldOp::create(b, loc, mlir::ValueRange{written.bitOffset, carried});
    }
    return PlanCursor{loop.getResult(0), loop.getResult(1), std::nullopt, 0};
}

/// @brief Encodes or skips a void field of @p bits.
///
/// Reserved bits are written as zeros and read as nothing: a decoder has no name to put them
/// under, so it only steps over them.
PlanCursor buildPaddingStep(mlir::OpBuilder&   b,
                            mlir::Location     loc,
                            mlir::Value        buffer,
                            mlir::Value        capacityBytes,
                            PlanCursor         cursor,
                            const std::int64_t bits,
                            const bool         writing)
{
    if (!writing)
    {
        return advancedBy(b, loc, cursor, cursor.error, bits);
    }
    return guarded(b, loc, cursor, [&](PlanCursor inner) {
        return emitZeroBits(b, loc, buffer, capacityBytes, inner, bits);
    });
}

/// @brief Rounds the running offset up to a byte boundary.
///
/// Serialising writes the bits it skips. Reading does not: the offset moves. The bits to skip
/// are a number whenever the offset modulo eight is one, whether or not the offset itself is.
PlanCursor buildAlignment(mlir::OpBuilder& b,
                          mlir::Location   loc,
                          mlir::Value      buffer,
                          mlir::Value      capacityBytes,
                          PlanCursor       cursor,
                          const bool       writing)
{
    if (cursor.staticResidue)
    {
        const std::int64_t pad = (8 - *cursor.staticResidue) % 8;
        if (pad == 0)
        {
            return cursor;
        }
        if (!writing)
        {
            return advancedBy(b, loc, cursor, cursor.error, pad);
        }
        return guarded(b, loc, cursor, [&](PlanCursor inner) {
            return emitZeroBits(b, loc, buffer, capacityBytes, inner, pad);
        });
    }
    const mlir::Value seven   = constantI64(b, loc, 7);
    const mlir::Value eight   = constantI64(b, loc, 8);
    const mlir::Value rounded = mlir::arith::AddIOp::create(b, loc, cursor.bitOffset, seven);
    const mlir::Value bytes   = mlir::arith::DivUIOp::create(b, loc, rounded, eight);
    const mlir::Value aligned = mlir::arith::MulIOp::create(b, loc, bytes, eight);
    if (!writing)
    {
        return PlanCursor{aligned, cursor.error, std::nullopt, 0};
    }
    return buildZeroBitsTo(b, loc, buffer, capacityBytes, cursor, aligned);
}

/// @brief Where the cursor at @p bitOffset stands in a buffer of @p capacityBytes: the byte it
/// reached and the bytes left from there.
///
/// A read past the end zero-extends and still advances the cursor, so a reader's bit offset can
/// exceed the capacity; the pointer handed to a nested type is formed at the bounded byte, which
/// every backend can address. A writer's cannot: the capacity check at the top of the plan
/// established room for the whole layout, and a write that ran out of it stopped the plan.
///
/// A cursor at bit nought stands at the start of any buffer, with all of it left, so it is not
/// bounded: the bounded byte would be nought for every capacity.
struct BufferPosition
{
    mlir::Value byteOffset;
    mlir::Value remaining;
};

BufferPosition bufferPosition(mlir::OpBuilder& b,
                              mlir::Location   loc,
                              mlir::Value      capacityBytes,
                              mlir::Value      bitOffset,
                              const bool       writing)
{
    if (mlir::matchPattern(bitOffset, mlir::m_Zero()))
    {
        return BufferPosition{constantI64(b, loc, 0), capacityBytes};
    }
    mlir::Value taken = mlir::arith::DivUIOp::create(b, loc, bitOffset, constantI64(b, loc, 8));
    if (!writing)
    {
        const mlir::Value fits =
            mlir::arith::CmpIOp::create(b, loc, mlir::arith::CmpIPredicate::ult, taken, capacityBytes);
        taken = mlir::arith::SelectOp::create(b, loc, fits, taken, capacityBytes);
    }
    return BufferPosition{taken, mlir::arith::SubIOp::create(b, loc, capacityBytes, taken)};
}

/// @brief The dialect's pointer to @p step's nested type, qualified for the direction.
mlir::dsdl::PtrType nestedPointerType(mlir::MLIRContext* ctx, const PlanStep& step, const bool writing)
{
    return mlir::dsdl::PtrType::get(ctx, mlir::dsdl::ObjectType::get(ctx, nestedIdentity(step)), writing);
}

/// @brief The dialect's pointer into the wire buffer, qualified for the direction.
mlir::dsdl::PtrType wirePointerType(mlir::MLIRContext* ctx, const bool writing)
{
    return mlir::dsdl::PtrType::get(ctx, mlir::dsdl::ByteType::get(ctx), !writing);
}

/// @brief Encodes or decodes one sealed nested composite at @p target through its own entry point.
///
/// The nested type is handed its storage, the point the container reached, and the space left.
/// A nested type of one size advances the container by that size: on the wire it occupies
/// exactly that, and a reader that ran out of bytes inside it finds only zeros after it
/// wherever the cursor stands. One whose size varies reports what it used, and the container
/// reads that back.
PlanCursor buildSealedNested(mlir::OpBuilder& b,
                             mlir::Location   loc,
                             const PlanStep&  step,
                             mlir::Value      target,
                             mlir::Value      buffer,
                             mlir::Value      capacityBytes,
                             PlanCursor       inner,
                             const bool       writing)
{
    auto* ctx     = b.getContext();
    auto  i64Ty   = b.getIntegerType(64);
    auto  sizePtr = mlir::dsdl::PtrType::get(ctx, mlir::dsdl::SizeType::get(ctx));

    const BufferPosition position = bufferPosition(b, loc, capacityBytes, inner.bitOffset, writing);
    const mlir::Value    sizeSlot = mlir::dsdl::LocalOp::create(b, loc, sizePtr, position.remaining);
    const mlir::Value    at =
        mlir::dsdl::BufferAtOp::create(b, loc, wirePointerType(ctx, writing), buffer, position.byteOffset);

    auto call = mlir::dsdl::CallSerdesOp::create(b,
                                                 loc,
                                                 b.getIntegerType(8),
                                                 nestedCallee(b, step, writing),
                                                 b.getStringAttr(step.name),
                                                 b.getStringAttr(writing ? "serialize" : "deserialize"),
                                                 target,
                                                 at,
                                                 sizeSlot);

    if (step.compositeFixedBits)
    {
        return advancedBy(b, loc, inner, call.getError(), *step.compositeFixedBits);
    }
    const mlir::Value used = mlir::dsdl::LoadScalarOp::create(b, loc, i64Ty, sizeSlot);
    const mlir::Value advanced =
        mlir::arith::AddIOp::create(b,
                                    loc,
                                    inner.bitOffset,
                                    mlir::arith::MulIOp::create(b, loc, used, constantI64(b, loc, 8)));
    return PlanCursor{advanced, call.getError(), std::nullopt, inner.staticResidue};
}

/// @brief Encodes or decodes one delimited nested composite at @p target.
///
/// A delimited nested type is preceded by its own length in bytes, so that a reader which
/// does not know the type can step over it. The decoder advances by the length it was told
/// rather than by what the nested decode
/// consumed: a newer sender may have written fields this reader has no name for, and skipping
/// only what was understood would leave the cursor inside them.
PlanCursor buildDelimitedNested(mlir::OpBuilder& b,
                                mlir::Location   loc,
                                const PlanStep&  step,
                                mlir::Value      target,
                                mlir::Value      buffer,
                                mlir::Value      capacityBytes,
                                PlanCursor       inner,
                                const bool       writing)
{
    auto* ctx     = b.getContext();
    auto  i64Ty   = b.getIntegerType(64);
    auto  sizePtr = mlir::dsdl::PtrType::get(ctx, mlir::dsdl::SizeType::get(ctx));

    const mlir::Value eight        = constantI64(b, loc, 8);
    const mlir::Value headerOffset = inner.bitOffset;

    mlir::Value declared;
    if (!writing)
    {
        declared = mlir::dsdl::ReadBitsOp::create(b,
                                                  loc,
                                                  i64Ty,
                                                  buffer,
                                                  capacityBytes,
                                                  headerOffset,
                                                  b.getI64IntegerAttr(kDelimiterHeaderBits),
                                                  nullptr);
    }
    const PlanCursor     afterHeader = advancedBy(b, loc, inner, inner.error, kDelimiterHeaderBits);
    const BufferPosition position    = bufferPosition(b, loc, capacityBytes, afterHeader.bitOffset, writing);

    // Serialising does not know the length until the nested type reports it, so the header
    // is reserved here and written once the encoding below has run.
    const mlir::Value sizeInit = writing ? position.remaining : declared;
    const mlir::Value sizeSlot = mlir::dsdl::LocalOp::create(b, loc, sizePtr, sizeInit);

    mlir::Value error = inner.error;
    if (!writing)
    {
        error = foldError(b,
                          loc,
                          error,
                          callErrorHelper(b,
                                          loc,
                                          step.delimiterValidateHelper,
                                          mlir::ValueRange{declared, position.remaining}));
    }

    // Serialising a nested type of one size knows the length it will write; the header is
    // that number and the slot is never read back.
    const std::optional<std::int64_t> fixedBytes =
        (writing && step.compositeFixedBits) ? std::optional{*step.compositeFixedBits / 8} : std::nullopt;

    return guarded(b,
                   loc,
                   PlanCursor{afterHeader.bitOffset, error, afterHeader.staticBits, afterHeader.staticResidue},
                   [&](PlanCursor ready) {
                       const mlir::Value at = mlir::dsdl::BufferAtOp::create(b,
                                                                             loc,
                                                                             wirePointerType(ctx, writing),
                                                                             buffer,
                                                                             position.byteOffset);

                       auto call =
                           mlir::dsdl::CallSerdesOp::create(b,
                                                            loc,
                                                            b.getIntegerType(8),
                                                            nestedCallee(b, step, writing),
                                                            b.getStringAttr(step.name),
                                                            b.getStringAttr(writing ? "serialize" : "deserialize"),
                                                            target,
                                                            at,
                                                            sizeSlot);

                       mlir::Value err = call.getError();

                       // The length the reader will be told. Serialising learns it from the nested type,
                       // reading it back out of the slot the callee wrote unless the type has one size.
                       // Decoding was told it up front and steps that far regardless of what the nested
                       // decode consumed, so it never reads the slot back at all.
                       mlir::Value span = declared;
                       if (fixedBytes)
                       {
                           span = constantI64(b, loc, *fixedBytes);
                       }
                       else if (writing)
                       {
                           span = mlir::dsdl::LoadScalarOp::create(b, loc, i64Ty, sizeSlot);
                       }

                       if (writing)
                       {
                           err = foldError(b,
                                           loc,
                                           err,
                                           callErrorHelper(b,
                                                           loc,
                                                           step.delimiterValidateHelper,
                                                           mlir::ValueRange{span, position.remaining}));
                       }

                       return guarded(b,
                                      loc,
                                      PlanCursor{ready.bitOffset, err, ready.staticBits, ready.staticResidue},
                                      [&](PlanCursor done) {
                                          PlanCursor after;
                                          if (fixedBytes)
                                          {
                                              after = advancedBy(b, loc, done, done.error, *fixedBytes * 8);
                                          }
                                          else
                                          {
                                              after = PlanCursor{mlir::arith::AddIOp::
                                                                     create(b,
                                                                            loc,
                                                                            done.bitOffset,
                                                                            mlir::arith::MulIOp::create(b,
                                                                                                        loc,
                                                                                                        span,
                                                                                                        eight)),
                                                                 done.error,
                                                                 std::nullopt,
                                                                 done.staticResidue};
                                          }
                                          if (writing)
                                          {
                                              auto header = mlir::dsdl::WriteBitsOp::create(b,
                                                                                            loc,
                                                                                            b.getIntegerType(8),
                                                                                            buffer,
                                                                                            capacityBytes,
                                                                                            headerOffset,
                                                                                            span,
                                                                                            b.getI64IntegerAttr(
                                                                                                kDelimiterHeaderBits),
                                                                                            nullptr);
                                              after.error = foldError(b, loc, after.error, header.getError());
                                          }
                                          return after;
                                      });
                   });
}

/// @brief Encodes or decodes the nested composite at @p target, sealed or delimited.
PlanCursor buildNested(mlir::OpBuilder& b,
                       mlir::Location   loc,
                       const PlanStep&  step,
                       mlir::Value      target,
                       mlir::Value      buffer,
                       mlir::Value      capacityBytes,
                       PlanCursor       inner,
                       const bool       writing)
{
    return step.compositeSealed ? buildSealedNested(b, loc, step, target, buffer, capacityBytes, inner, writing)
                                : buildDelimitedNested(b, loc, step, target, buffer, capacityBytes, inner, writing);
}

/// @brief Holds, or writes out, the member's view of the field's bytes, in place of the nested call.
///
/// Reading takes the buffer from the field's offset with what remains of it, bounded by the
/// field's width: a short buffer leaves a short view, which the nested type's accessors
/// zero-extend. Writing copies the view's bytes to the field's offset and zero-fills to the
/// width, so an empty view is the nested type's default. Either way the cursor advances by the
/// width, which the step has because an asserted type has one size. With @p index the member is
/// an array of views and this is one element of it, at the element's offset.
PlanCursor buildViewStep(mlir::OpBuilder& b,
                         mlir::Location   loc,
                         const PlanStep&  step,
                         mlir::Value      object,
                         mlir::Value      buffer,
                         mlir::Value      capacityBytes,
                         PlanCursor       inner,
                         const bool       writing,
                         mlir::Value      index)
{
    auto*              ctx        = b.getContext();
    const std::int64_t widthBytes = *step.compositeFixedBits / 8;
    const auto         member     = b.getStringAttr(step.name);

    const BufferPosition position = bufferPosition(b, loc, capacityBytes, inner.bitOffset, writing);
    const mlir::Value    at =
        mlir::dsdl::BufferAtOp::create(b, loc, wirePointerType(ctx, writing), buffer, position.byteOffset);
    if (writing)
    {
        auto view = mlir::dsdl::LoadViewOp::create(b,
                                                   loc,
                                                   wirePointerType(ctx, /*writing=*/false),
                                                   b.getIntegerType(64),
                                                   object,
                                                   member,
                                                   index);
        mlir::dsdl::CopyBytesOp::create(b,
                                        loc,
                                        at,
                                        view.getBytes(),
                                        view.getSizeBytes(),
                                        b.getI64IntegerAttr(widthBytes));
    }
    else
    {
        const mlir::Value width = constantI64(b, loc, widthBytes);
        const mlir::Value short_ =
            mlir::arith::CmpIOp::create(b, loc, mlir::arith::CmpIPredicate::ult, position.remaining, width);
        const mlir::Value held = mlir::arith::SelectOp::create(b, loc, short_, position.remaining, width);
        mlir::dsdl::StoreViewOp::create(b, loc, object, member, index, at, held);
    }
    return advancedBy(b, loc, inner, inner.error, *step.compositeFixedBits);
}

/// @brief Encodes or decodes one nested composite member.
PlanCursor buildCompositeStep(mlir::OpBuilder& b,
                              mlir::Location   loc,
                              const PlanStep&  step,
                              mlir::Value      object,
                              mlir::Value      buffer,
                              mlir::Value      capacityBytes,
                              PlanCursor       cursor,
                              const bool       writing)
{
    return guarded(b, loc, cursor, [&](PlanCursor inner) {
        if (step.heldAsView)
        {
            return buildViewStep(b, loc, step, object, buffer, capacityBytes, inner, writing, {});
        }
        const mlir::Value target = mlir::dsdl::MemberAddrOp::create(b,
                                                                    loc,
                                                                    nestedPointerType(b.getContext(), step, writing),
                                                                    object,
                                                                    b.getStringAttr(step.name));
        return buildNested(b, loc, step, target, buffer, capacityBytes, inner, writing);
    });
}

/// @brief The union's options, one field each, in tag order.
std::vector<const PlanStep*> unionOptionsOf(const std::vector<PlanStep>& steps)
{
    std::vector<const PlanStep*> options;
    for (const auto& step : steps)
    {
        if (step.kind == PlanStepKind::Field)
        {
            options.push_back(&step);
        }
    }
    std::ranges::sort(options, [](const PlanStep* lhs, const PlanStep* rhs) {
        return lhs->unionOptionIndex < rhs->unionOptionIndex;
    });
    return options;
}

/// @brief Runs one option's steps when the tag selects it, and passes @p chain through
///        untouched otherwise.
///
/// A union encodes exactly one of its options, so the chain of tests is the plan: whichever
/// arm the tag selects contributes, and the rest contribute nothing. An arm begins at
/// @p afterTag, where the selected option always begins, rather than at the chain: the two are
/// the same cursor when the arm runs, and only the former is a number. Where the arm ends,
/// modulo eight, goes into @p armsEnd: the union ends where its selected arm does, so what every
/// arm agrees on holds after the chain.
template <typename StepFn>
PlanCursor buildUnionOption(mlir::OpBuilder& b,
                            mlir::Location   loc,
                            mlir::Value      tag,
                            const PlanStep&  option,
                            PlanCursor       chain,
                            PlanCursor       afterTag,
                            ResidueMeet&     armsEnd,
                            StepFn           emitStep)
{
    const mlir::Value selected = mlir::arith::CmpIOp::create(b,
                                                             loc,
                                                             mlir::arith::CmpIPredicate::eq,
                                                             tag,
                                                             constantI64(b, loc, option.unionOptionIndex));

    const mlir::SmallVector<mlir::Type, 2> types{b.getIntegerType(64), b.getIntegerType(8)};
    auto                                   arm = mlir::scf::IfOp::create(b, loc, types, selected, true);
    stampResultRoles(arm, {RoleOffset, RoleError});
    {
        mlir::OpBuilder::InsertionGuard const g(b);
        b.setInsertionPointToStart(arm.thenBlock());
        const PlanCursor next = emitStep(afterTag);
        armsEnd.meet(next.staticResidue);
        mlir::scf::YieldOp::create(b, loc, mlir::ValueRange{next.bitOffset, next.error});
    }
    {
        mlir::OpBuilder::InsertionGuard const g(b);
        b.setInsertionPointToStart(arm.elseBlock());
        mlir::scf::YieldOp::create(b, loc, mlir::ValueRange{chain.bitOffset, chain.error});
    }
    return PlanCursor{arm.getResult(0), arm.getResult(1), std::nullopt, std::nullopt};
}

bool stepIsBitpackedArray(const PlanStep& step)
{
    return stepIsArray(step) && (step.scalarCategory == "bool");
}

/// @brief Moves a bool array as one run of wire bits rather than a loop over elements.
///
/// The copy's length in bits is the array's count. A spelling maps the run onto its own
/// storage, bitpacked or one element per bool.
PlanCursor buildBitpackedArray(mlir::OpBuilder& b,
                               mlir::Location   loc,
                               const PlanStep&  step,
                               mlir::Value      object,
                               mlir::Value      buffer,
                               mlir::Value      capacityBytes,
                               PlanCursor       cursor,
                               mlir::Value      count,
                               const bool       writing)
{
    auto* ctx     = b.getContext();
    auto  bytePtr = mlir::dsdl::PtrType::get(ctx, mlir::dsdl::ByteType::get(ctx), writing);
    // The storage is bytes whether or not the length varies; a target that keeps a variable-length
    // array's bits beside its count reaches them from the member.
    const mlir::Value packed = mlir::dsdl::ElementAddrOp::create(b,
                                                                 loc,
                                                                 bytePtr,
                                                                 object,
                                                                 b.getStringAttr(step.name),
                                                                 constantI64(b, loc, 0),
                                                                 b.getStringAttr("bool"),
                                                                 b.getI64IntegerAttr(8));
    if (writing)
    {
        mlir::dsdl::BitWriteOp::create(b, loc, buffer, cursor.bitOffset, count, packed, constantI64(b, loc, 0));
    }
    else
    {
        mlir::dsdl::BitReadOp::create(b, loc, packed, buffer, capacityBytes, cursor.bitOffset, count);
    }
    if (const auto elements = constantCount(count))
    {
        return advancedBy(b, loc, cursor, cursor.error, *elements);
    }
    return PlanCursor{mlir::arith::AddIOp::create(b, loc, cursor.bitOffset, count),
                      cursor.error,
                      std::nullopt,
                      std::nullopt};
}

/// @brief Encodes or decodes an array of nested composites, one element at a time.
///
/// Each element goes through the nested type's own entry point and reports its own length, so
/// unlike an array of scalars the stride is not known and the loop cannot be driven by the
/// offset. It is counted instead, with `scf.for`, whose induction variable is not among its
/// results -- an `scf.while` would make the index a result nothing reads, and that reaches
/// the emitted C as a variable nothing uses.
PlanCursor buildCompositeElementLoop(mlir::OpBuilder& b,
                                     mlir::Location   loc,
                                     const PlanStep&  step,
                                     mlir::Value      object,
                                     mlir::Value      buffer,
                                     mlir::Value      capacityBytes,
                                     PlanCursor       cursor,
                                     mlir::Value      count,
                                     const bool       writing)
{
    auto* ctx   = b.getContext();
    auto  i64Ty = b.getIntegerType(64);

    const auto elementAddress = [&](mlir::Value index) {
        return mlir::dsdl::ElementAddrOp::create(b,
                                                 loc,
                                                 nestedPointerType(ctx, step, writing),
                                                 object,
                                                 b.getStringAttr(step.name),
                                                 index,
                                                 b.getStringAttr("composite"),
                                                 b.getI64IntegerAttr(0));
    };

    // Elements of one width lie at the start plus the index times that width, so the loop
    // carries the error alone. A reader of a delimited element steps by the header it read,
    // whatever the nested plan's width, so only a sealed one or a writer has that.
    if (step.compositeFixedBits && (step.compositeSealed || writing))
    {
        const std::int64_t elementBits = *step.compositeFixedBits + (step.compositeSealed ? 0 : kDelimiterHeaderBits);
        const mlir::Value  start       = cursor.bitOffset;
        auto               loop        = countedLoop(b, loc, count, mlir::ValueRange{cursor.error});
        stampResultRoles(loop, {RoleError});
        {
            mlir::OpBuilder::InsertionGuard const g(b);
            b.setInsertionPointToStart(loop.getBody());
            const mlir::Value index = mlir::arith::IndexCastOp::create(b, loc, i64Ty, loop.getInductionVar());
            const mlir::Value at =
                mlir::arith::AddIOp::create(b,
                                            loc,
                                            start,
                                            mlir::arith::MulIOp::create(b,
                                                                        loc,
                                                                        index,
                                                                        constantI64(b, loc, elementBits)));
            const mlir::Value carried = errorGuarded(b, loc, loop.getRegionIterArg(0), [&]() {
                const PlanCursor inner{at, loop.getRegionIterArg(0), std::nullopt, std::nullopt};
                // An element held as a view is the buffer at the element's offset, not a decode.
                if (step.heldAsView)
                {
                    return buildViewStep(b, loc, step, object, buffer, capacityBytes, inner, writing, index).error;
                }
                return buildNested(b, loc, step, elementAddress(index), buffer, capacityBytes, inner, writing).error;
            });
            mlir::scf::YieldOp::create(b, loc, mlir::ValueRange{carried});
        }
        return advancedByRun(b, loc, cursor, loop.getResult(0), count, elementBits);
    }

    auto loop = countedLoop(b, loc, count, mlir::ValueRange{cursor.bitOffset, cursor.error});
    stampResultRoles(loop, {RoleOffset, RoleError});
    {
        mlir::OpBuilder::InsertionGuard const g(b);
        b.setInsertionPointToStart(loop.getBody());
        const mlir::Value index = mlir::arith::IndexCastOp::create(b, loc, i64Ty, loop.getInductionVar());
        const PlanCursor  carried{loop.getRegionIterArg(0), loop.getRegionIterArg(1), std::nullopt, std::nullopt};

        const PlanCursor next = guarded(b, loc, carried, [&](PlanCursor inner) {
            return buildNested(b, loc, step, elementAddress(index), buffer, capacityBytes, inner, writing);
        });
        mlir::scf::YieldOp::create(b, loc, mlir::ValueRange{next.bitOffset, next.error});
    }
    return PlanCursor{loop.getResult(0), loop.getResult(1), std::nullopt, cursor.staticResidue};
}

/// @brief Builds a typed serialise body as operations.
///
/// The published header declares this symbol, so the parameter spellings are the header's.
/// The C path has no branch-graph conversion, so control flow is structured: what the
/// hand-written text says with an early return this says by carrying an error through the
/// cursor.
mlir::LogicalResult buildTypedSerializeBody(mlir::OpBuilder&             builder,
                                            mlir::ModuleOp               module,
                                            mlir::Location               loc,
                                            llvm::StringRef              functionName,
                                            llvm::StringRef              identity,
                                            const std::vector<PlanStep>& steps,
                                            llvm::StringRef              capacityCheckSymbol,
                                            const bool                   isUnion,
                                            const std::int64_t           unionTagBits,
                                            llvm::StringRef              unionTagValidateSymbol,
                                            llvm::StringRef              unionTagHelper)
{
    // A plan that needs no bits names no capacity-check helper, and the body calls none.
    if (!capacityCheckSymbol.empty() && !module.lookupSymbol<mlir::func::FuncOp>(capacityCheckSymbol))
    {
        return mlir::failure();
    }

    mlir::OpBuilder::InsertionGuard const outer(builder);
    builder.setInsertionPointToEnd(&module.getBodyRegion().front());

    auto* ctx    = builder.getContext();
    auto  objTy  = mlir::dsdl::PtrType::get(ctx, mlir::dsdl::ObjectType::get(ctx, identity), true);
    auto  bufTy  = mlir::dsdl::PtrType::get(ctx, mlir::dsdl::ByteType::get(ctx));
    auto  sizeTy = mlir::dsdl::PtrType::get(ctx, mlir::dsdl::SizeType::get(ctx));
    auto  i8Ty   = builder.getIntegerType(8);
    auto  i64Ty  = builder.getIntegerType(64);
    auto  fnType = builder.getFunctionType(mlir::TypeRange{objTy, bufTy, sizeTy}, mlir::TypeRange{i8Ty});
    auto  fn     = mlir::func::FuncOp::create(builder, loc, functionName, fnType);
    fn->setAttr("llvmdsdl.plan_origin", builder.getStringAttr(kLoweredSerDesContractProducer));

    mlir::Block* entry = fn.addEntryBlock();
    builder.setInsertionPointToStart(entry);
    const mlir::Value object  = entry->getArgument(0);
    const mlir::Value buffer  = entry->getArgument(1);
    const mlir::Value sizePtr = entry->getArgument(2);

    mlir::Value anyNull = mlir::dsdl::IsNullOp::create(builder, loc, builder.getI1Type(), object);
    for (const mlir::Value pointer : {buffer, sizePtr})
    {
        auto next = mlir::dsdl::IsNullOp::create(builder, loc, builder.getI1Type(), pointer);
        anyNull   = mlir::arith::OrIOp::create(builder, loc, anyNull, next);
    }

    auto outerIf = mlir::scf::IfOp::create(builder, loc, mlir::TypeRange{i8Ty}, anyNull, true);
    stampResultRoles(outerIf, {RoleError});
    {
        mlir::OpBuilder::InsertionGuard const g(builder);
        builder.setInsertionPointToStart(outerIf.thenBlock());
        mlir::scf::YieldOp::create(builder,
                                   loc,
                                   mlir::ValueRange{constantI8(builder, loc, -kRuntimeErrorInvalidArgument)});
    }
    {
        mlir::OpBuilder::InsertionGuard const g(builder);
        builder.setInsertionPointToStart(outerIf.elseBlock());

        const mlir::Value capacityBytes = mlir::dsdl::LoadScalarOp::create(builder, loc, i64Ty, sizePtr);
        const mlir::Value capacityBits =
            mlir::arith::MulIOp::create(builder, loc, capacityBytes, constantI64(builder, loc, 8));
        const mlir::Value capacityError =
            capacityCheckSymbol.empty()
                ? constantI8(builder, loc, 0)
                : callErrorHelper(builder, loc, capacityCheckSymbol, mlir::ValueRange{capacityBits});

        PlanCursor cursor{constantI64(builder, loc, 0), capacityError, 0, 0};

        if (isUnion)
        {
            // The tag comes off the object, is normalised, and is validated before anything
            // is written: a tag naming no option selects nothing, and the plan stops there.
            const mlir::Value rawTag   = mlir::dsdl::UnionTagOp::create(builder, loc, i64Ty, object);
            const mlir::Value tagValue = applyHelper(builder, loc, unionTagHelper, rawTag);
            cursor.error = foldError(builder,
                                     loc,
                                     cursor.error,
                                     callErrorHelper(builder, loc, unionTagValidateSymbol, mlir::ValueRange{tagValue}));
            cursor       = guarded(builder, loc, cursor, [&](PlanCursor inner) {
                return emitWrite(builder, loc, buffer, capacityBytes, inner, tagValue, unionTagBits, false);
            });

            const PlanCursor afterTag = cursor;
            ResidueMeet      armsEnd;
            for (const PlanStep* option : unionOptionsOf(steps))
            {
                cursor =
                    buildUnionOption(builder, loc, tagValue, *option, cursor, afterTag, armsEnd, [&](PlanCursor arm) {
                        if (option->alignmentBits > 1)
                        {
                            arm = buildAlignment(builder, loc, buffer, capacityBytes, arm, true);
                        }
                        if (stepIsArray(*option))
                        {
                            return buildArrayWrite(builder, loc, *option, object, buffer, capacityBytes, arm);
                        }
                        if (stepIsComposite(*option))
                        {
                            return buildCompositeStep(builder, loc, *option, object, buffer, capacityBytes, arm, true);
                        }
                        return buildScalarWrite(builder, loc, *option, object, buffer, capacityBytes, arm);
                    });
            }
            cursor.staticResidue = armsEnd.value;
        }
        for (std::size_t index = 0; isUnion ? false : (index < steps.size()); ++index)
        {
            const PlanStep& step = steps[index];
            if (step.kind == PlanStepKind::Align)
            {
                cursor = buildAlignment(builder, loc, buffer, capacityBytes, cursor, true);
            }
            else if (step.kind == PlanStepKind::Padding)
            {
                cursor = buildPaddingStep(builder, loc, buffer, capacityBytes, cursor, step.bits, true);
            }
            else if (stepIsArray(step))
            {
                cursor = buildArrayWrite(builder, loc, step, object, buffer, capacityBytes, cursor);
            }
            else if (stepIsComposite(step))
            {
                cursor = buildCompositeStep(builder, loc, step, object, buffer, capacityBytes, cursor, true);
            }
            else
            {
                cursor = buildScalarWrite(builder, loc, step, object, buffer, capacityBytes, cursor);
            }
        }
        cursor = buildAlignment(builder, loc, buffer, capacityBytes, cursor, true);

        auto epilogue =
            mlir::scf::IfOp::create(builder, loc, mlir::TypeRange{i8Ty}, isHealthy(builder, loc, cursor.error), true);
        stampResultRoles(epilogue, {RoleError});
        {
            mlir::OpBuilder::InsertionGuard const g3(builder);
            builder.setInsertionPointToStart(epilogue.thenBlock());
            mlir::dsdl::StoreScalarOp::create(builder,
                                              loc,
                                              sizePtr,
                                              mlir::arith::DivUIOp::create(builder,
                                                                           loc,
                                                                           cursor.bitOffset,
                                                                           constantI64(builder, loc, 8)));
            mlir::scf::YieldOp::create(builder, loc, mlir::ValueRange{constantI8(builder, loc, 0)});
        }
        {
            mlir::OpBuilder::InsertionGuard const g3(builder);
            builder.setInsertionPointToStart(epilogue.elseBlock());
            mlir::scf::YieldOp::create(builder, loc, mlir::ValueRange{cursor.error});
        }
        mlir::scf::YieldOp::create(builder, loc, mlir::ValueRange{epilogue.getResult(0)});
    }

    builder.setInsertionPointToEnd(entry);
    mlir::func::ReturnOp::create(builder, loc, mlir::ValueRange{outerIf.getResult(0)});
    return mlir::success();
}

/// @brief Deserialises one scalar field, advancing the offset past it.
///
/// No guard, unlike the serialise side. A read cannot fail: the runtime answers a short
/// buffer by zero-extending, which is the tolerance a deserialiser is required to have.
PlanCursor buildScalarRead(mlir::OpBuilder& b,
                           mlir::Location   loc,
                           const PlanStep&  step,
                           mlir::Value      object,
                           mlir::Value      buffer,
                           mlir::Value      capacityBytes,
                           PlanCursor       cursor)
{
    const mlir::Value bitOffset = cursor.bitOffset;
    const mlir::Type  valueType = stepValueType(b, step);
    mlir::Value raw = mlir::dsdl::ReadBitsOp::create(b,
                                                     loc,
                                                     valueType,
                                                     buffer,
                                                     capacityBytes,
                                                     bitOffset,
                                                     b.getI64IntegerAttr(step.bitLength),
                                                     (step.scalarCategory == "signed") ? b.getUnitAttr() : nullptr);
    raw             = normaliseScalar(b, loc, step, raw, false);
    markSigned(mlir::dsdl::StoreMemberOp::create(b, loc, object, b.getStringAttr(step.name), raw), step);
    return advancedBy(b, loc, cursor, cursor.error, step.bitLength);
}

/// @brief Reads @p count elements into the object, advancing past them.
///
/// The count is the declared length of a fixed array or, for a variable one, the length prefix
/// after the plan has validated it against the declared capacity: a prefix is
/// attacker-controlled, and a decoder that trusted it would write past the elements it has.
PlanCursor buildArrayElementReads(mlir::OpBuilder& b,
                                  mlir::Location   loc,
                                  const PlanStep&  step,
                                  mlir::Value      object,
                                  mlir::Value      buffer,
                                  mlir::Value      capacityBytes,
                                  PlanCursor       cursor,
                                  mlir::Value      count)
{
    if (stepIsComposite(step))
    {
        return buildCompositeElementLoop(b, loc, step, object, buffer, capacityBytes, cursor, count, false);
    }
    if (stepIsBitpackedArray(step))
    {
        return buildBitpackedArray(b, loc, step, object, buffer, capacityBytes, cursor, count, false);
    }
    auto              i64Ty = b.getIntegerType(64);
    const mlir::Value start = cursor.bitOffset;
    const mlir::Value width = constantI64(b, loc, step.bitLength);
    // Counted, like the serialise side, and carrying nothing: a read cannot fail.
    auto loop = countedLoop(b, loc, count, mlir::ValueRange{});
    {
        mlir::OpBuilder::InsertionGuard const g(b);
        b.setInsertionPointToStart(loop.getBody());
        const mlir::Value index = mlir::arith::IndexCastOp::create(b, loc, i64Ty, loop.getInductionVar());
        const mlir::Value at =
            mlir::arith::AddIOp::create(b, loc, start, mlir::arith::MulIOp::create(b, loc, index, width));

        const mlir::Type valueType = stepValueType(b, step);
        mlir::Value      element =
            mlir::dsdl::ReadBitsOp::create(b,
                                           loc,
                                           valueType,
                                           buffer,
                                           capacityBytes,
                                           at,
                                           b.getI64IntegerAttr(step.bitLength),
                                           (step.scalarCategory == "signed") ? b.getUnitAttr() : nullptr);
        element = normaliseScalar(b, loc, step, element, false);
        markSigned(mlir::dsdl::StoreElementOp::create(b,
                                                      loc,
                                                      object,
                                                      b.getStringAttr(step.name),
                                                      index,
                                                      element,
                                                      b.getStringAttr(step.scalarCategory),
                                                      b.getI64IntegerAttr(storageBitsFor(step))),
                   step);
    }
    return advancedByRun(b, loc, cursor, cursor.error, count, step.bitLength);
}

PlanCursor buildArrayRead(mlir::OpBuilder& b,
                          mlir::Location   loc,
                          const PlanStep&  step,
                          mlir::Value      object,
                          mlir::Value      buffer,
                          mlir::Value      capacityBytes,
                          PlanCursor       cursor)
{
    // The whole read is guarded, not just the element loop. A step before this one may
    // already have failed -- a nested union rejecting its tag, say -- and the reference
    // returns at that point. Running on regardless would replace the error it reported with
    // whatever this array makes of bytes that were never meant to be an array.
    return guarded(b, loc, cursor, [&](PlanCursor outer) {
        const mlir::Value bitOffset = outer.bitOffset;
        auto              i64Ty     = b.getIntegerType(64);
        const bool        fixed     = stepIsFixedArray(step);

        if (fixed)
        {
            // No prefix, no count member, and no length to judge: the declaration says how many.
            return guarded(b, loc, outer, [&](PlanCursor inner) {
                const mlir::Value count = constantI64(b, loc, step.arrayCapacity);
                return buildArrayElementReads(b, loc, step, object, buffer, capacityBytes, inner, count);
            });
        }

        mlir::Value wireLength       = mlir::dsdl::ReadBitsOp::create(b,
                                                                      loc,
                                                                      i64Ty,
                                                                      buffer,
                                                                      capacityBytes,
                                                                      bitOffset,
                                                                      b.getI64IntegerAttr(step.arrayLengthPrefixBits),
                                                                      nullptr);
        wireLength                   = applyHelper(b, loc, step.deserArrayLengthPrefixHelper, wireLength);
        const PlanCursor afterPrefix = advancedBy(b, loc, outer, outer.error, step.arrayLengthPrefixBits);

        // The length off the wire is validated before anything is sized by it. The helper rejects
        // a length past the declared capacity or beyond the index width of the target, so the
        // store that follows narrows exactly and a rejected message leaves the array untouched.
        const mlir::Value count = wireLength;
        const mlir::Value error = callErrorHelper(b, loc, step.arrayLengthValidateHelper, mlir::ValueRange{count});

        return guarded(b,
                       loc,
                       PlanCursor{afterPrefix.bitOffset, error, afterPrefix.staticBits, afterPrefix.staticResidue},
                       [&](PlanCursor inner) {
                           mlir::dsdl::SetArrayLengthOp::create(b, loc, object, b.getStringAttr(step.name), count);
                           return buildArrayElementReads(b, loc, step, object, buffer, capacityBytes, inner, count);
                       });
    });
}

/// @brief Builds a typed deserialise body as operations.
///
/// The argument check is not the serialise one reversed, and its order is load-bearing. A null
/// buffer is legal when the declared size is zero, and C reaches that clause by short-circuit,
/// having already established the size pointer is non-null. Reading the size eagerly would
/// dereference null on exactly the call the check exists to reject.
mlir::LogicalResult buildTypedDeserializeBody(mlir::OpBuilder&             builder,
                                              mlir::ModuleOp               module,
                                              mlir::Location               loc,
                                              llvm::StringRef              functionName,
                                              llvm::StringRef              identity,
                                              const std::vector<PlanStep>& steps,
                                              const bool                   isUnion,
                                              const std::int64_t           unionTagBits,
                                              llvm::StringRef              unionTagValidateSymbol,
                                              llvm::StringRef              unionTagHelper)
{
    mlir::OpBuilder::InsertionGuard const outer(builder);
    builder.setInsertionPointToEnd(&module.getBodyRegion().front());

    auto* ctx    = builder.getContext();
    auto  objTy  = mlir::dsdl::PtrType::get(ctx, mlir::dsdl::ObjectType::get(ctx, identity));
    auto  bufTy  = mlir::dsdl::PtrType::get(ctx, mlir::dsdl::ByteType::get(ctx), true);
    auto  sizeTy = mlir::dsdl::PtrType::get(ctx, mlir::dsdl::SizeType::get(ctx));
    auto  i8Ty   = builder.getIntegerType(8);
    auto  i64Ty  = builder.getIntegerType(64);
    auto  fnType = builder.getFunctionType(mlir::TypeRange{objTy, bufTy, sizeTy}, mlir::TypeRange{i8Ty});
    auto  fn     = mlir::func::FuncOp::create(builder, loc, functionName, fnType);
    fn->setAttr("llvmdsdl.plan_origin", builder.getStringAttr(kLoweredSerDesContractProducer));

    mlir::Block* entry = fn.addEntryBlock();
    builder.setInsertionPointToStart(entry);
    const mlir::Value object  = entry->getArgument(0);
    const mlir::Value buffer  = entry->getArgument(1);
    const mlir::Value sizePtr = entry->getArgument(2);

    const mlir::Value objNull    = mlir::dsdl::IsNullOp::create(builder, loc, builder.getI1Type(), object);
    const mlir::Value sizeNull   = mlir::dsdl::IsNullOp::create(builder, loc, builder.getI1Type(), sizePtr);
    const mlir::Value bufNull    = mlir::dsdl::IsNullOp::create(builder, loc, builder.getI1Type(), buffer);
    const mlir::Value cannotRead = mlir::arith::OrIOp::create(builder, loc, objNull, sizeNull);

    auto rejected = mlir::scf::IfOp::create(builder, loc, mlir::TypeRange{builder.getI1Type()}, cannotRead, true);
    stampResultRoles(rejected, {RoleRejected});
    {
        mlir::OpBuilder::InsertionGuard const g(builder);
        builder.setInsertionPointToStart(rejected.thenBlock());
        mlir::scf::YieldOp::create(builder,
                                   loc,
                                   mlir::ValueRange{
                                       mlir::arith::ConstantOp::create(builder, loc, builder.getBoolAttr(true))});
    }
    {
        mlir::OpBuilder::InsertionGuard const g(builder);
        builder.setInsertionPointToStart(rejected.elseBlock());
        const mlir::Value capacity = mlir::dsdl::LoadScalarOp::create(builder, loc, i64Ty, sizePtr);
        const mlir::Value nonEmpty = mlir::arith::CmpIOp::create(builder,
                                                                 loc,
                                                                 mlir::arith::CmpIPredicate::ne,
                                                                 capacity,
                                                                 constantI64(builder, loc, 0));
        mlir::scf::YieldOp::create(builder,
                                   loc,
                                   mlir::ValueRange{mlir::arith::AndIOp::create(builder, loc, bufNull, nonEmpty)});
    }

    auto outerIf = mlir::scf::IfOp::create(builder, loc, mlir::TypeRange{i8Ty}, rejected.getResult(0), true);
    stampResultRoles(outerIf, {RoleError});
    {
        mlir::OpBuilder::InsertionGuard const g(builder);
        builder.setInsertionPointToStart(outerIf.thenBlock());
        mlir::scf::YieldOp::create(builder,
                                   loc,
                                   mlir::ValueRange{constantI8(builder, loc, -kRuntimeErrorInvalidArgument)});
    }
    {
        mlir::OpBuilder::InsertionGuard const g(builder);
        builder.setInsertionPointToStart(outerIf.elseBlock());

        const mlir::Value capacityBytes = mlir::dsdl::LoadScalarOp::create(builder, loc, i64Ty, sizePtr);
        const mlir::Value readable      = mlir::dsdl::BufferOrEmptyOp::create(builder, loc, bufTy, buffer);

        PlanCursor cursor{constantI64(builder, loc, 0), constantI8(builder, loc, 0), 0, 0};

        if (isUnion)
        {
            const mlir::Value rawTag   = mlir::dsdl::ReadBitsOp::create(builder,
                                                                        loc,
                                                                        i64Ty,
                                                                        readable,
                                                                        capacityBytes,
                                                                        cursor.bitOffset,
                                                                        builder.getI64IntegerAttr(unionTagBits),
                                                                        nullptr);
            const mlir::Value tagValue = applyHelper(builder, loc, unionTagHelper, rawTag);
            cursor.error = foldError(builder,
                                     loc,
                                     cursor.error,
                                     callErrorHelper(builder, loc, unionTagValidateSymbol, mlir::ValueRange{tagValue}));
            cursor       = guarded(builder, loc, cursor, [&](PlanCursor inner) {
                mlir::dsdl::SetUnionTagOp::create(builder, loc, object, tagValue);
                return advancedBy(builder, loc, inner, inner.error, unionTagBits);
            });

            const PlanCursor afterTag = cursor;
            ResidueMeet      armsEnd;
            for (const PlanStep* option : unionOptionsOf(steps))
            {
                cursor =
                    buildUnionOption(builder, loc, tagValue, *option, cursor, afterTag, armsEnd, [&](PlanCursor arm) {
                        if (option->alignmentBits > 1)
                        {
                            arm = buildAlignment(builder, loc, readable, capacityBytes, arm, false);
                        }
                        if (stepIsArray(*option))
                        {
                            return buildArrayRead(builder, loc, *option, object, readable, capacityBytes, arm);
                        }
                        if (stepIsComposite(*option))
                        {
                            return buildCompositeStep(builder,
                                                      loc,
                                                      *option,
                                                      object,
                                                      readable,
                                                      capacityBytes,
                                                      arm,
                                                      false);
                        }
                        return buildScalarRead(builder, loc, *option, object, readable, capacityBytes, arm);
                    });
            }
            cursor.staticResidue = armsEnd.value;
        }
        for (std::size_t index = 0; isUnion ? false : (index < steps.size()); ++index)
        {
            const PlanStep& step = steps[index];
            if (step.kind == PlanStepKind::Align)
            {
                cursor = buildAlignment(builder, loc, readable, capacityBytes, cursor, false);
            }
            else if (step.kind == PlanStepKind::Padding)
            {
                cursor = buildPaddingStep(builder, loc, readable, capacityBytes, cursor, step.bits, false);
            }
            else if (stepIsArray(step))
            {
                cursor = buildArrayRead(builder, loc, step, object, readable, capacityBytes, cursor);
            }
            else if (stepIsComposite(step))
            {
                cursor = buildCompositeStep(builder, loc, step, object, readable, capacityBytes, cursor, false);
            }
            else
            {
                cursor = buildScalarRead(builder, loc, step, object, readable, capacityBytes, cursor);
            }
        }
        const mlir::Value offset = cursor.bitOffset;

        const mlir::Value seven   = constantI64(builder, loc, 7);
        const mlir::Value eight   = constantI64(builder, loc, 8);
        const mlir::Value rounded = mlir::arith::AddIOp::create(builder, loc, offset, seven);
        const mlir::Value bytes   = mlir::arith::DivUIOp::create(builder, loc, rounded, eight);
        const mlir::Value aligned = mlir::arith::MulIOp::create(builder, loc, bytes, eight);

        // What was consumed, clamped to what was there: a truncated input is decoded as far
        // as it went rather than rejected.
        const mlir::Value capacityBits = mlir::arith::MulIOp::create(builder, loc, capacityBytes, eight);
        const mlir::Value fits =
            mlir::arith::CmpIOp::create(builder, loc, mlir::arith::CmpIPredicate::ult, aligned, capacityBits);
        const mlir::Value clamped = mlir::arith::SelectOp::create(builder, loc, fits, aligned, capacityBits);

        // A nested type can refuse what it was given, and then nothing was consumed to
        // report: the size is written only on the path that succeeded.
        auto epilogue =
            mlir::scf::IfOp::create(builder, loc, mlir::TypeRange{i8Ty}, isHealthy(builder, loc, cursor.error), true);
        stampResultRoles(epilogue, {RoleError});
        {
            mlir::OpBuilder::InsertionGuard const g2(builder);
            builder.setInsertionPointToStart(epilogue.thenBlock());
            mlir::dsdl::StoreScalarOp::create(builder,
                                              loc,
                                              sizePtr,
                                              mlir::arith::DivUIOp::create(builder, loc, clamped, eight));
            mlir::scf::YieldOp::create(builder, loc, mlir::ValueRange{constantI8(builder, loc, 0)});
        }
        {
            mlir::OpBuilder::InsertionGuard const g2(builder);
            builder.setInsertionPointToStart(epilogue.elseBlock());
            mlir::scf::YieldOp::create(builder, loc, mlir::ValueRange{cursor.error});
        }
        mlir::scf::YieldOp::create(builder, loc, mlir::ValueRange{epilogue.getResult(0)});
    }

    builder.setInsertionPointToEnd(entry);
    mlir::func::ReturnOp::create(builder, loc, mlir::ValueRange{outerIf.getResult(0)});
    return mlir::success();
}

/// @brief The zero of a scalar's holder: what a field is before anything has been read into it.
mlir::Value zeroOf(mlir::OpBuilder& b, mlir::Location loc, mlir::Type type)
{
    if (mlir::isa<mlir::FloatType>(type))
    {
        return mlir::arith::ConstantOp::create(b, loc, b.getFloatAttr(type, 0.0));
    }
    return mlir::arith::ConstantOp::create(b, loc, b.getIntegerAttr(type, 0));
}

/// @brief Sets one field to its default, and folds what a nested initialiser answered into @p error.
///
/// A scalar is its holder's zero. A variable-length array is a length of nought and nothing else:
/// what lies beyond the length is not part of the value. A fixed array is every element set. A
/// nested composite is handed to its own initialise body, which is the only step that can answer
/// anything, and it answers only for a null address this body never hands it.
///
/// A bool array is a run of bits in every target, bitpacked or one element per bool, and the one
/// operation every target spells for that run is a bit read. Reading it from a buffer of no bytes
/// is implicit zero extension applied to this array -- the spec's own definition of its default --
/// through an operation each target has already given the meaning it needs here.
mlir::Value buildInitializeStep(mlir::OpBuilder& b,
                                mlir::Location   loc,
                                const PlanStep&  step,
                                mlir::Value      object,
                                mlir::Value      error)
{
    auto*      ctx   = b.getContext();
    auto       i8Ty  = b.getIntegerType(8);
    auto       i64Ty = b.getIntegerType(64);
    const auto name  = b.getStringAttr(step.name);

    if (stepIsArray(step))
    {
        if (!stepIsFixedArray(step))
        {
            mlir::dsdl::SetArrayLengthOp::create(b, loc, object, name, constantI64(b, loc, 0));
            return error;
        }
        const mlir::Value count = constantI64(b, loc, step.arrayCapacity);
        if (stepIsBitpackedArray(step))
        {
            // The byte the read is pointed at is a local, and a local is written once to hold its
            // value: it is not const, whatever the read would accept.
            auto              bytePtr  = mlir::dsdl::PtrType::get(ctx, mlir::dsdl::ByteType::get(ctx), false);
            auto              emptyPtr = bytePtr;
            const mlir::Value packed   = mlir::dsdl::ElementAddrOp::create(b,
                                                                           loc,
                                                                           bytePtr,
                                                                           object,
                                                                           name,
                                                                           constantI64(b, loc, 0),
                                                                           b.getStringAttr("bool"),
                                                                           b.getI64IntegerAttr(8));
            const mlir::Value nothing  = mlir::dsdl::LocalOp::create(b, loc, emptyPtr, constantI8(b, loc, 0));
            mlir::dsdl::BitReadOp::create(b,
                                          loc,
                                          packed,
                                          nothing,
                                          constantI64(b, loc, 0),
                                          constantI64(b, loc, 0),
                                          count);
            return error;
        }
        if (stepIsComposite(step))
        {
            // A fixed array of views defaults to no bytes in every element, in one clear.
            if (step.heldAsView)
            {
                mlir::dsdl::ClearViewOp::create(b, loc, object, name);
                return error;
            }
            auto loop = countedLoop(b, loc, count, mlir::ValueRange{error});
            stampResultRoles(loop, {RoleError});
            {
                mlir::OpBuilder::InsertionGuard const g(b);
                b.setInsertionPointToStart(loop.getBody());
                const mlir::Value index   = mlir::arith::IndexCastOp::create(b, loc, i64Ty, loop.getInductionVar());
                const mlir::Value element = mlir::dsdl::ElementAddrOp::create(b,
                                                                              loc,
                                                                              nestedPointerType(ctx, step, false),
                                                                              object,
                                                                              name,
                                                                              index,
                                                                              b.getStringAttr("composite"),
                                                                              b.getI64IntegerAttr(0));
                auto              call =
                    mlir::dsdl::CallInitializeOp::create(b, loc, i8Ty, nestedInitializeCallee(b, step), name, element);
                mlir::scf::YieldOp::create(b,
                                           loc,
                                           mlir::ValueRange{
                                               foldError(b, loc, loop.getRegionIterArg(0), call.getError())});
            }
            return loop.getResult(0);
        }
        auto loop = countedLoop(b, loc, count, mlir::ValueRange{});
        {
            mlir::OpBuilder::InsertionGuard const g(b);
            b.setInsertionPointToStart(loop.getBody());
            const mlir::Value index = mlir::arith::IndexCastOp::create(b, loc, i64Ty, loop.getInductionVar());
            markSigned(mlir::dsdl::StoreElementOp::create(b,
                                                          loc,
                                                          object,
                                                          name,
                                                          index,
                                                          zeroOf(b, loc, stepValueType(b, step)),
                                                          b.getStringAttr(step.scalarCategory),
                                                          b.getI64IntegerAttr(storageBitsFor(step))),
                       step);
        }
        return error;
    }
    if (stepIsComposite(step))
    {
        // A view's default is no bytes.
        if (step.heldAsView)
        {
            mlir::dsdl::ClearViewOp::create(b, loc, object, name);
            return error;
        }
        const mlir::Value target =
            mlir::dsdl::MemberAddrOp::create(b, loc, nestedPointerType(ctx, step, false), object, name);
        auto call = mlir::dsdl::CallInitializeOp::create(b, loc, i8Ty, nestedInitializeCallee(b, step), name, target);
        return foldError(b, loc, error, call.getError());
    }
    markSigned(mlir::dsdl::StoreMemberOp::create(b, loc, object, name, zeroOf(b, loc, stepValueType(b, step))), step);
    return error;
}

/// @brief Builds a typed initialise body as operations.
///
/// The object at its defaults, which the spec defines as what deserialising nothing produces:
/// every field is what implicit zero extension makes it. There is no wire, so there is no cursor,
/// no size, and no error but a null object. A union initialises every arm rather than the one its
/// tag selects. The value is observable only through the selected arm, so the two agree wherever
/// a caller can look, and a type with no indeterminate storage is what three backends already
/// produce and what a caller who reaches for `_initialize_` is asking for.
mlir::LogicalResult buildTypedInitializeBody(mlir::OpBuilder&             builder,
                                             mlir::ModuleOp               module,
                                             mlir::Location               loc,
                                             llvm::StringRef              functionName,
                                             llvm::StringRef              identity,
                                             const std::vector<PlanStep>& steps,
                                             const bool                   isUnion)
{
    mlir::OpBuilder::InsertionGuard const outer(builder);
    builder.setInsertionPointToEnd(&module.getBodyRegion().front());

    auto* ctx    = builder.getContext();
    auto  objTy  = mlir::dsdl::PtrType::get(ctx, mlir::dsdl::ObjectType::get(ctx, identity));
    auto  i8Ty   = builder.getIntegerType(8);
    auto  fnType = builder.getFunctionType(mlir::TypeRange{objTy}, mlir::TypeRange{i8Ty});
    auto  fn     = mlir::func::FuncOp::create(builder, loc, functionName, fnType);
    fn->setAttr("llvmdsdl.plan_origin", builder.getStringAttr(kLoweredSerDesContractProducer));

    mlir::Block* entry = fn.addEntryBlock();
    builder.setInsertionPointToStart(entry);
    const mlir::Value object  = entry->getArgument(0);
    const mlir::Value objNull = mlir::dsdl::IsNullOp::create(builder, loc, builder.getI1Type(), object);

    auto outerIf = mlir::scf::IfOp::create(builder, loc, mlir::TypeRange{i8Ty}, objNull, true);
    stampResultRoles(outerIf, {RoleError});
    {
        mlir::OpBuilder::InsertionGuard const g(builder);
        builder.setInsertionPointToStart(outerIf.thenBlock());
        mlir::scf::YieldOp::create(builder,
                                   loc,
                                   mlir::ValueRange{constantI8(builder, loc, -kRuntimeErrorInvalidArgument)});
    }
    {
        mlir::OpBuilder::InsertionGuard const g(builder);
        builder.setInsertionPointToStart(outerIf.elseBlock());
        mlir::Value error = constantI8(builder, loc, 0);
        if (isUnion)
        {
            mlir::dsdl::SetUnionTagOp::create(builder, loc, object, constantI64(builder, loc, 0));
            for (const PlanStep* option : unionOptionsOf(steps))
            {
                error = buildInitializeStep(builder, loc, *option, object, error);
            }
        }
        else
        {
            for (const PlanStep& step : steps)
            {
                if (step.kind == PlanStepKind::Field)
                {
                    error = buildInitializeStep(builder, loc, step, object, error);
                }
            }
        }
        mlir::scf::YieldOp::create(builder, loc, mlir::ValueRange{error});
    }

    builder.setInsertionPointToEnd(entry);
    mlir::func::ReturnOp::create(builder, loc, mlir::ValueRange{outerIf.getResult(0)});
    return mlir::success();
}

/// @brief Builds every serialisation plan's bodies as operations, before a target is chosen.
///
/// A plan becomes a serialise and a deserialise `func.func` over the plan operations, in the
/// dialect's own vocabulary. Which target converts them next is not this pass's concern: the C
/// conversion and the LLVM conversion read the same bodies, and neither renders a body of its
/// own. A plan this pass cannot express fails it, naming the step and the reason.
///
/// Whether a body was built is asked of the module by symbol rather than recorded in state, so
/// a later pass cannot come to disagree with this one about it.
/// @brief A field of a wire-flat plan with the bit offset the steps place it at.
struct FixedField final
{
    const PlanStep* step;
    std::int64_t    bitOffset;
};

/// @brief The fields of a wire-flat plan at their offsets, by walking the steps as the bodies do:
///        an alignment rounds up to a byte, and padding and every field advance by a fixed width.
///
/// Answers nothing where a width is not fixed. The `wire_flat` verdict excludes that and the
/// verifier holds the plan to it, so a plan reaching here with one is malformed.
std::optional<std::vector<FixedField>> fixedFieldOffsets(const std::vector<PlanStep>& steps)
{
    std::vector<FixedField> out;
    std::int64_t            offset = 0;
    for (const PlanStep& step : steps)
    {
        if (step.kind == PlanStepKind::Align)
        {
            offset = ((offset + 7) / 8) * 8;
            continue;
        }
        if (step.kind == PlanStepKind::Padding)
        {
            offset += step.bits;
            continue;
        }
        if (isVariableArrayKind(step.arrayKind))
        {
            return std::nullopt;
        }
        std::int64_t width = step.bitLength;
        if (stepIsComposite(step))
        {
            if (!step.compositeFixedBits)
            {
                return std::nullopt;
            }
            width = *step.compositeFixedBits;
        }
        out.push_back(FixedField{&step, offset});
        offset += width * (stepIsArray(step) ? step.arrayCapacity : 1);
    }
    return out;
}

/// @brief Whether a union's wire form is a tag and one option at a fixed offset: sealed, of one
///        length, every option whole bytes wide and, for a composite, wire-flat itself.
///
/// Such a union has no single layout, so it is not wire-flat; but its tag sits at a fixed offset,
/// and every option after the tag, so an accessor set can read the tag and then the option. The
/// tag's width is a whole number of bytes by the wire format.
bool unionIsFlat(mlir::dsdl::SerializationPlanOp plan)
{
    if (!plan.getIsUnion() || !plan.getSealed() || !plan.getFixedSize() || plan.getBody().empty())
    {
        return false;
    }
    auto module = plan->getParentOfType<mlir::ModuleOp>();
    bool any    = false;
    for (mlir::dsdl::IOOp io : plan.getBody().front().getOps<mlir::dsdl::IOOp>())
    {
        if (io.isPadding() || io.isVariableArray())
        {
            return false;
        }
        any = true;
        if (io.isComposite())
        {
            auto schema =
                module ? module.lookupSymbol<mlir::dsdl::SchemaOp>(
                             renderDefinitionSymbolBase(io.getCompositeFullName().value_or(llvm::StringRef{}),
                                                        static_cast<std::uint32_t>(io.getCompositeMajor().value_or(0)),
                                                        static_cast<std::uint32_t>(io.getCompositeMinor().value_or(0))))
                       : mlir::dsdl::SchemaOp{};
            if (!schema || schema.getBody().empty())
            {
                return false;
            }
            bool flat = false;
            for (mlir::dsdl::SerializationPlanOp nested :
                 schema.getBody().front().getOps<mlir::dsdl::SerializationPlanOp>())
            {
                if (!nested.getSection())
                {
                    flat = nested.getWireFlat();
                }
            }
            if (!flat)
            {
                return false;
            }
            continue;
        }
        const std::int64_t count = io.isArray() ? io.getArrayCapacity() : 1;
        if ((io.getBitLength() <= 0) || (((io.getBitLength() * count) % 8) != 0))
        {
            return false;
        }
    }
    return any && ((plan.getUnionTagBits().value_or(0) % 8) == 0);
}

/// @brief The union's tag as a step: unsigned, of the tag's width, normalised through the plan's
///        own tag helpers, which mask to that width.
PlanStep unionTagAsStep(mlir::dsdl::SerializationPlanOp plan)
{
    PlanStep tag;
    tag.kind                = PlanStepKind::Field;
    tag.name                = "_tag_";
    tag.scalarCategory      = "unsigned";
    tag.castMode            = "saturated";
    tag.arrayKind           = "none";
    tag.bitLength           = plan.getUnionTagBits().value_or(0);
    tag.alignmentBits       = 8;
    tag.serUnsignedHelper   = plan.getLoweredSerUnionTagHelper().value_or(llvm::StringRef{}).str();
    tag.deserUnsignedHelper = plan.getLoweredDeserUnionTagHelper().value_or(llvm::StringRef{}).str();
    return tag;
}

/// @brief Stamps what a translator needs to find an accessor and place it: the schema, the
///        section, which of the two it is, and the member it reaches.
void tagAccessor(mlir::func::FuncOp    fn,
                 mlir::dsdl::SchemaOp  schema,
                 const llvm::StringRef section,
                 const llvm::StringRef kind,
                 const PlanStep&       step)
{
    mlir::OpBuilder b(fn.getContext());
    fn->setAttr("llvmdsdl.plan_origin", b.getStringAttr(kLoweredSerDesContractProducer));
    fn->setAttr("llvmdsdl.schema_sym", schema.getSymNameAttr());
    fn->setAttr("llvmdsdl.plan_body", b.getStringAttr(kind));
    fn->setAttr("llvmdsdl.member", b.getStringAttr(step.name));
    if (!section.empty())
    {
        fn->setAttr("llvmdsdl.section", b.getStringAttr(section));
    }
    markSigned(fn, step);
}

/// @brief The number of bytes a getter may read from its buffer.
///
/// A null buffer is read as an empty one, whatever size it is handed. `deserialize_` refuses a null
/// buffer with a non-zero size, but a getter has no error to answer with, and an empty buffer is one
/// it can answer for: every read zero-extends, as a read past the end of any buffer does.
/// `dsdl-fold-null-guards` folds the test away for a target whose buffer cannot be null.
mlir::Value readableSize(mlir::OpBuilder&     builder,
                         const mlir::Location loc,
                         const mlir::Value    buffer,
                         const mlir::Value    size)
{
    const mlir::Value null  = mlir::dsdl::IsNullOp::create(builder, loc, builder.getI1Type(), buffer);
    const mlir::Value empty = constantI64(builder, loc, 0);
    auto              held  = mlir::arith::SelectOp::create(builder, loc, null, empty, size);
    stampResultRoles(held, {RoleSize});
    return held.getResult();
}

/// @brief The buffer a getter reads, an empty one standing in where it is null, and the number of
///        bytes it may read there (`readableSize`).
std::pair<mlir::Value, mlir::Value> readableBuffer(mlir::OpBuilder&     builder,
                                                   const mlir::Location loc,
                                                   const mlir::Type     type,
                                                   const mlir::Value    buffer,
                                                   const mlir::Value    size)
{
    const mlir::Value readable = mlir::dsdl::BufferOrEmptyOp::create(builder, loc, type, buffer);
    return {readable, readableSize(builder, loc, buffer, size)};
}

/// @brief Builds the getter and the setter of one field of a wire-flat section: a scalar, or one
///        element of a fixed array of scalars, which the accessors then take an index for.
///
/// A getter is one read at the field's constant offset, normalised by the deserialise helper,
/// and answers the value alone: a read cannot fail, and a short buffer zero-extends, so the answer
/// is what `deserialize_` puts in the field. An element at or past the array's capacity reads as
/// zero, as bytes past the buffer do: the read stays within the field and the answer is selected
/// after it. A null buffer reads as an empty one (`readableBuffer`). A setter is one write of the
/// serialise-normalised value, answering the runtime's error code as the serialise body does: a
/// null buffer or an index past the capacity is refused as an invalid argument, and a buffer too
/// short for the field is refused before the write, as the body's capacity check refuses one too
/// short for the whole.
mlir::LogicalResult buildFieldAccessors(mlir::OpBuilder&                           builder,
                                        mlir::ModuleOp                             module,
                                        mlir::Location                             loc,
                                        llvm::StringRef                            fnStem,
                                        mlir::dsdl::SchemaOp                       schema,
                                        llvm::StringRef                            section,
                                        const PlanStep&                            step,
                                        const std::int64_t                         bitOffset,
                                        mlir::SmallVectorImpl<mlir::func::FuncOp>& built)
{
    mlir::OpBuilder::InsertionGuard const outer(builder);
    builder.setInsertionPointToEnd(&module.getBodyRegion().front());

    auto*                   ctx        = builder.getContext();
    auto                    readTy     = mlir::dsdl::PtrType::get(ctx, mlir::dsdl::ByteType::get(ctx), true);
    auto                    writeTy    = mlir::dsdl::PtrType::get(ctx, mlir::dsdl::ByteType::get(ctx));
    auto                    i8Ty       = builder.getIntegerType(8);
    auto                    i64Ty      = builder.getIntegerType(64);
    const mlir::Type        valueTy    = stepValueType(builder, step);
    const mlir::UnitAttr    signedAttr = (step.scalarCategory == "signed") ? builder.getUnitAttr() : nullptr;
    const mlir::IntegerAttr widthAttr  = builder.getI64IntegerAttr(step.bitLength);
    const bool              indexed    = stepIsArray(step);

    // The element's offset, and whether the index names one: a scalar's is the constant.
    const auto locate = [&](const mlir::Value index) -> std::pair<mlir::Value, mlir::Value> {
        if (!indexed)
        {
            return {constantI64(builder, loc, bitOffset), mlir::Value{}};
        }
        const mlir::Value inRange = mlir::arith::CmpIOp::create(builder,
                                                                loc,
                                                                mlir::arith::CmpIPredicate::ult,
                                                                index,
                                                                constantI64(builder, loc, step.arrayCapacity));
        // Built one at a time, for the reason the setter's guard is.
        const mlir::Value base   = constantI64(builder, loc, bitOffset);
        const mlir::Value width  = constantI64(builder, loc, step.bitLength);
        const mlir::Value scaled = mlir::arith::MulIOp::create(builder, loc, index, width);
        const mlir::Value offset = mlir::arith::AddIOp::create(builder, loc, base, scaled);
        return {offset, inRange};
    };

    {
        mlir::SmallVector<mlir::Type, 3> arguments{readTy, i64Ty};
        if (indexed)
        {
            arguments.push_back(i64Ty);
        }
        auto fn = mlir::func::FuncOp::create(builder,
                                             loc,
                                             (fnStem + "__get_" + step.name + "_ir_").str(),
                                             builder.getFunctionType(arguments, mlir::TypeRange{valueTy}));
        tagAccessor(fn, schema, section, "get", step);
        mlir::Block* entry = fn.addEntryBlock();
        builder.setInsertionPointToStart(entry);
        const auto [readable, size] =
            readableBuffer(builder, loc, readTy, entry->getArgument(0), entry->getArgument(1));
        auto [offset, inRange] = locate(indexed ? entry->getArgument(2) : mlir::Value{});
        mlir::Value at         = offset;
        if (inRange)
        {
            // The read stays within the field for any index; the answer is decided after it. A
            // slice read carries no size beside it, so the answer is what has to be selected.
            at = mlir::arith::SelectOp::create(builder, loc, inRange, offset, constantI64(builder, loc, 0));
        }
        mlir::Value raw =
            mlir::dsdl::ReadBitsOp::create(builder, loc, valueTy, readable, size, at, widthAttr, signedAttr);
        raw = normaliseScalar(builder, loc, step, raw, false);
        if (inRange)
        {
            raw = mlir::arith::SelectOp::create(builder, loc, inRange, raw, zeroOf(builder, loc, valueTy));
        }
        mlir::func::ReturnOp::create(builder, loc, mlir::ValueRange{raw});
        built.push_back(fn);
    }

    builder.setInsertionPointToEnd(&module.getBodyRegion().front());
    {
        mlir::SmallVector<mlir::Type, 4> arguments{writeTy, i64Ty};
        if (indexed)
        {
            arguments.push_back(i64Ty);
        }
        arguments.push_back(valueTy);
        auto fn = mlir::func::FuncOp::create(builder,
                                             loc,
                                             (fnStem + "__set_" + step.name + "_ir_").str(),
                                             builder.getFunctionType(arguments, mlir::TypeRange{i8Ty}));
        tagAccessor(fn, schema, section, "set", step);
        mlir::Block* entry = fn.addEntryBlock();
        builder.setInsertionPointToStart(entry);
        const mlir::Value buffer       = entry->getArgument(0);
        const mlir::Value size         = entry->getArgument(1);
        const mlir::Value value        = entry->getArgument(indexed ? 3 : 2);
        const mlir::Value null         = mlir::dsdl::IsNullOp::create(builder, loc, builder.getI1Type(), buffer);
        auto [offset, inRange]         = locate(indexed ? entry->getArgument(2) : mlir::Value{});
        const mlir::Value capacityBits = mlir::arith::MulIOp::create(builder, loc, size, constantI64(builder, loc, 8));
        const mlir::Value need =
            mlir::arith::AddIOp::create(builder, loc, offset, constantI64(builder, loc, step.bitLength));
        const mlir::Value fits =
            mlir::arith::CmpIOp::create(builder, loc, mlir::arith::CmpIPredicate::uge, capacityBits, need);
        // Each constant is built before the select that reads it. Built as two arguments of one
        // call they would be built in whichever order the compiler chose, and the operations
        // reach the body in the order they are built, so the emitted body would follow the
        // compiler that built dsdlc.
        const mlir::Value ok       = constantI8(builder, loc, 0);
        const mlir::Value tooSmall = constantI8(builder, loc, -kRuntimeErrorSerializationBufferTooSmall);
        mlir::Value       code     = mlir::arith::SelectOp::create(builder, loc, fits, ok, tooSmall);
        if (inRange)
        {
            code = mlir::arith::SelectOp::create(builder,
                                                 loc,
                                                 inRange,
                                                 code,
                                                 constantI8(builder, loc, -kRuntimeErrorInvalidArgument));
        }
        code       = mlir::arith::SelectOp::create(builder,
                                                   loc,
                                                   null,
                                                   constantI8(builder, loc, -kRuntimeErrorInvalidArgument),
                                                   code);
        auto guard = mlir::scf::IfOp::create(builder, loc, mlir::TypeRange{i8Ty}, isHealthy(builder, loc, code), true);
        stampResultRoles(guard, {RoleError});
        {
            mlir::OpBuilder::InsertionGuard const g(builder);
            builder.setInsertionPointToStart(guard.elseBlock());
            mlir::scf::YieldOp::create(builder, loc, mlir::ValueRange{code});
        }
        {
            mlir::OpBuilder::InsertionGuard const g(builder);
            builder.setInsertionPointToStart(guard.thenBlock());
            const mlir::Value normalised = normaliseScalar(builder, loc, step, value, true);
            auto              write      = mlir::dsdl::WriteBitsOp::create(builder,
                                                                           loc,
                                                                           i8Ty,
                                                                           buffer,
                                                                           size,
                                                                           offset,
                                                                           normalised,
                                                                           widthAttr,
                                                                           signedAttr);
            mlir::scf::YieldOp::create(builder, loc, mlir::ValueRange{write.getError()});
        }
        mlir::func::ReturnOp::create(builder, loc, mlir::ValueRange{guard.getResult(0)});
        built.push_back(fn);
    }
    return mlir::success();
}

/// @brief Builds the getter of one nested composite field of a wire-flat section, or of one
///        element of a fixed array of them.
///
/// A nested type's accessors read from the start of the buffer they are given, so its getter
/// answers the buffer from the field's byte offset and, through the size pointer, what remains:
/// `Vec3::get_x(Pose::get_position(buffer))` composes. A field at offset nought answers the buffer
/// it was handed, null or not, with the size a getter reads there (`readableSize`): no offset is
/// added to the buffer, so a null one needs no stand-in, and the nested getters read a null buffer
/// as empty. Elsewhere the offset is clamped to the size, so a short buffer yields an empty one and
/// every nested read zero-extends, as `deserialize_` does; an element at or past the capacity
/// yields the same, and so does a null buffer (`readableBuffer`). There is no setter: a nested
/// field is set through its own fields' setters on the buffer the getter answers.
mlir::LogicalResult buildCompositeAccessor(mlir::OpBuilder&                           builder,
                                           mlir::ModuleOp                             module,
                                           mlir::Location                             loc,
                                           llvm::StringRef                            fnStem,
                                           mlir::dsdl::SchemaOp                       schema,
                                           llvm::StringRef                            section,
                                           const PlanStep&                            step,
                                           const std::int64_t                         bitOffset,
                                           mlir::SmallVectorImpl<mlir::func::FuncOp>& built)
{
    mlir::OpBuilder::InsertionGuard const outer(builder);
    builder.setInsertionPointToEnd(&module.getBodyRegion().front());

    auto*      ctx     = builder.getContext();
    auto       readTy  = mlir::dsdl::PtrType::get(ctx, mlir::dsdl::ByteType::get(ctx), true);
    auto       sizeTy  = mlir::dsdl::PtrType::get(ctx, mlir::dsdl::SizeType::get(ctx));
    auto       i64Ty   = builder.getIntegerType(64);
    const bool indexed = stepIsArray(step);
    if (!step.compositeFixedBits)
    {
        return mlir::failure();
    }
    const std::int64_t nestedBytes = *step.compositeFixedBits / 8;

    mlir::SmallVector<mlir::Type, 4> arguments{readTy, i64Ty};
    if (indexed)
    {
        arguments.push_back(i64Ty);
    }
    arguments.push_back(sizeTy);
    auto fn = mlir::func::FuncOp::create(builder,
                                         loc,
                                         (fnStem + "__get_" + step.name + "_ir_").str(),
                                         builder.getFunctionType(arguments, mlir::TypeRange{readTy}));
    tagAccessor(fn, schema, section, "get", step);
    mlir::Block* entry = fn.addEntryBlock();
    builder.setInsertionPointToStart(entry);
    const mlir::Value  buffer     = entry->getArgument(0);
    const mlir::Value  outSize    = entry->getArgument(indexed ? 3 : 2);
    const std::int64_t byteOffset = bitOffset / 8;
    if (!indexed && (byteOffset == 0))
    {
        mlir::dsdl::StoreScalarOp::create(builder,
                                          loc,
                                          outSize,
                                          readableSize(builder, loc, buffer, entry->getArgument(1)));
        mlir::func::ReturnOp::create(builder, loc, mlir::ValueRange{buffer});
        built.push_back(fn);
        return mlir::success();
    }
    const auto [readable, size] = readableBuffer(builder, loc, readTy, buffer, entry->getArgument(1));

    mlir::Value offset = constantI64(builder, loc, byteOffset);
    mlir::Value within;
    if (!indexed)
    {
        within = mlir::arith::CmpIOp::create(builder, loc, mlir::arith::CmpIPredicate::ule, offset, size);
    }
    else
    {
        const mlir::Value index   = entry->getArgument(2);
        const mlir::Value inRange = mlir::arith::CmpIOp::create(builder,
                                                                loc,
                                                                mlir::arith::CmpIPredicate::ult,
                                                                index,
                                                                constantI64(builder, loc, step.arrayCapacity));
        offset = mlir::arith::AddIOp::create(builder,
                                             loc,
                                             offset,
                                             mlir::arith::MulIOp::create(builder,
                                                                         loc,
                                                                         index,
                                                                         constantI64(builder, loc, nestedBytes)));
        within = mlir::arith::AndIOp::create(builder,
                                             loc,
                                             inRange,
                                             mlir::arith::CmpIOp::create(builder,
                                                                         loc,
                                                                         mlir::arith::CmpIPredicate::ule,
                                                                         offset,
                                                                         size));
    }
    const mlir::Value at        = mlir::arith::SelectOp::create(builder, loc, within, offset, size);
    const mlir::Value remaining = mlir::arith::SubIOp::create(builder, loc, size, at);
    mlir::dsdl::StoreScalarOp::create(builder, loc, outSize, remaining);
    const mlir::Value sub = mlir::dsdl::BufferAtOp::create(builder, loc, readTy, readable, at);
    mlir::func::ReturnOp::create(builder, loc, mlir::ValueRange{sub});
    built.push_back(fn);
    return mlir::success();
}

struct BuildDSDLPlanBodiesPass : public mlir::PassWrapper<BuildDSDLPlanBodiesPass, mlir::OperationPass<mlir::ModuleOp>>
{
    llvm::StringRef getArgument() const final
    {
        return "build-dsdl-plan-bodies";
    }
    llvm::StringRef getDescription() const final
    {
        return "Build DSDL serialisation plan bodies as dialect operations";
    }
    void getDependentDialects(mlir::DialectRegistry& registry) const override
    {
        registry.insert<mlir::dsdl::DSDLDialect,
                        mlir::func::FuncDialect,
                        mlir::arith::ArithDialect,
                        mlir::scf::SCFDialect>();
    }

    /// @brief Builds the three bodies of @p plan, or says why it cannot.
    mlir::LogicalResult buildPlan(mlir::ModuleOp                             module,
                                  mlir::dsdl::SchemaOp                       schema,
                                  mlir::dsdl::SerializationPlanOp            plan,
                                  mlir::SmallVectorImpl<mlir::func::FuncOp>& built)
    {
        if (const auto envelope = findLoweredContractEnvelopeViolation(plan.getOperation()))
        {
            switch (envelope->kind)
            {
            case LoweredContractEnvelopeViolationKind::MissingVersion:
                return plan.emitOpError("missing lowered contract version; run lower-dsdl-exec before "
                                        "build-dsdl-plan-bodies");
            case LoweredContractEnvelopeViolationKind::UnsupportedMajorVersion:
                return plan.emitOpError("unsupported lowered contract major version: " +
                                        loweredSerDesUnsupportedMajorVersionDiagnosticDetail(envelope->encodedVersion) +
                                        "; run matching lower-dsdl-exec before build-dsdl-plan-bodies");
            case LoweredContractEnvelopeViolationKind::ProducerMismatch:
                return plan.emitOpError("missing lowered contract producer marker; run lower-dsdl-exec before "
                                        "build-dsdl-plan-bodies");
            }
        }
        if (const auto violation = findLoweredPlanContractViolation(module, plan.getOperation()))
        {
            return violation->operation->emitOpError(violation->message);
        }

        const std::string section = plan.getSection().value_or(llvm::StringRef{}).str();
        const std::string fnStem  = schema.getSymName().str() + renderSectionSymbolSuffix(section);

        const std::string identity = planIdentity(schema, plan);

        const auto stringOrEmpty = [](const std::optional<llvm::StringRef> value) {
            return value ? value->str() : std::string{};
        };
        const std::string  capacityCheckSymbol       = stringOrEmpty(plan.getLoweredCapacityCheckHelper());
        const bool         isUnion                   = plan.getIsUnion();
        const std::int64_t unionTagBits              = nonNegative(plan.getUnionTagBits().value_or(0));
        const std::string  unionTagSerializeHelper   = stringOrEmpty(plan.getLoweredSerUnionTagHelper());
        const std::string  unionTagDeserializeHelper = stringOrEmpty(plan.getLoweredDeserUnionTagHelper());
        const std::string  unionTagValidateSymbol =
            isUnion ? stringOrEmpty(plan.getLoweredUnionTagValidateHelper()) : std::string{};

        const auto steps = collectPlanSteps(plan);
        if (const auto reason = unsupportedPlanReason(steps,
                                                      isUnion,
                                                      unionTagBits,
                                                      nonNegative(plan.getMaxBits()) > 0,
                                                      capacityCheckSymbol,
                                                      unionTagValidateSymbol,
                                                      unionTagSerializeHelper,
                                                      unionTagDeserializeHelper))
        {
            return plan.emitOpError("cannot be built as operations: " + *reason);
        }

        mlir::OpBuilder builder(&getContext());
        if (mlir::failed(buildTypedSerializeBody(builder,
                                                 module,
                                                 plan.getLoc(),
                                                 fnStem + "__serialize_ir_",
                                                 identity,
                                                 steps,
                                                 capacityCheckSymbol,
                                                 isUnion,
                                                 unionTagBits,
                                                 unionTagValidateSymbol,
                                                 unionTagSerializeHelper)))
        {
            return plan.emitOpError("serialize body could not be built");
        }
        if (mlir::failed(buildTypedDeserializeBody(builder,
                                                   module,
                                                   plan.getLoc(),
                                                   fnStem + "__deserialize_ir_",
                                                   identity,
                                                   steps,
                                                   isUnion,
                                                   unionTagBits,
                                                   unionTagValidateSymbol,
                                                   unionTagDeserializeHelper)))
        {
            return plan.emitOpError("deserialize body could not be built");
        }
        if (mlir::failed(buildTypedInitializeBody(builder,
                                                  module,
                                                  plan.getLoc(),
                                                  fnStem + "__initialize_ir_",
                                                  identity,
                                                  steps,
                                                  isUnion)))
        {
            return plan.emitOpError("initialize body could not be built");
        }
        for (const auto& [name, direction] : {std::pair{fnStem + "__serialize_ir_", "serialize"},
                                              std::pair{fnStem + "__deserialize_ir_", "deserialize"},
                                              std::pair{fnStem + "__initialize_ir_", "initialize"}})
        {
            auto fn = module.lookupSymbol<mlir::func::FuncOp>(name);
            if (!fn)
            {
                return plan.emitOpError("body '" + name + "' was not defined");
            }
            // What a translator needs to find a body and place it: the schema it serialises,
            // its direction, and the section of a service it belongs to.
            fn->setAttr("llvmdsdl.schema_sym", schema.getSymNameAttr());
            fn->setAttr("llvmdsdl.plan_body", builder.getStringAttr(direction));
            if (!section.empty())
            {
                fn->setAttr("llvmdsdl.section", builder.getStringAttr(section));
            }
            built.push_back(fn);
        }

        // A wire-flat section's fields sit at offsets the schema fixes, so each scalar among them,
        // and each element of a fixed array of scalars, gets a getter and a setter beside the
        // bodies: one read or one write at that offset. A nested composite gets a getter that
        // answers the buffer from its offset, for the nested type's own accessors.
        if (plan.getWireFlat() && !isUnion)
        {
            const auto fields = fixedFieldOffsets(steps);
            if (!fields)
            {
                return plan.emitOpError("is wire-flat but holds a step of no fixed width");
            }
            for (const FixedField& field : *fields)
            {
                if (stepIsComposite(*field.step))
                {
                    if (mlir::failed(buildCompositeAccessor(builder,
                                                            module,
                                                            plan.getLoc(),
                                                            fnStem,
                                                            schema,
                                                            section,
                                                            *field.step,
                                                            field.bitOffset,
                                                            built)))
                    {
                        return plan.emitOpError("accessor body could not be built for '" + field.step->name + "'");
                    }
                    continue;
                }
                if (mlir::failed(buildFieldAccessors(builder,
                                                     module,
                                                     plan.getLoc(),
                                                     fnStem,
                                                     schema,
                                                     section,
                                                     *field.step,
                                                     field.bitOffset,
                                                     built)))
                {
                    return plan.emitOpError("accessor bodies could not be built for '" + field.step->name + "'");
                }
            }
        }
        // A union whose options are all flat and of one length has its tag at offset nought and
        // every option at the offset after it, so the tag gets a getter and a setter as a field
        // would, and each option its accessors at that one offset. A setter writes the option's
        // value and not the tag: selecting is the tag setter's, with the option's tag constant.
        else if (isUnion && unionIsFlat(plan))
        {
            const PlanStep     tag      = unionTagAsStep(plan);
            const std::int64_t afterTag = tag.bitLength;
            if (mlir::failed(
                    buildFieldAccessors(builder, module, plan.getLoc(), fnStem, schema, section, tag, 0, built)))
            {
                return plan.emitOpError("accessor bodies could not be built for the union's tag");
            }
            for (const PlanStep* option : unionOptionsOf(steps))
            {
                const bool ok = stepIsComposite(*option) ? mlir::succeeded(buildCompositeAccessor(builder,
                                                                                                  module,
                                                                                                  plan.getLoc(),
                                                                                                  fnStem,
                                                                                                  schema,
                                                                                                  section,
                                                                                                  *option,
                                                                                                  afterTag,
                                                                                                  built))
                                                         : mlir::succeeded(buildFieldAccessors(builder,
                                                                                               module,
                                                                                               plan.getLoc(),
                                                                                               fnStem,
                                                                                               schema,
                                                                                               section,
                                                                                               *option,
                                                                                               afterTag,
                                                                                               built));
                if (!ok)
                {
                    return plan.emitOpError("accessor bodies could not be built for '" + option->name + "'");
                }
            }
        }
        return mlir::success();
    }

    // NOLINTNEXTLINE(misc-override-with-different-visibility) -- MLIR declares passes this way.
    void runOnOperation() override
    {
        auto module = getOperation();

        mlir::SmallVector<mlir::func::FuncOp, 16> built;
        for (mlir::dsdl::SchemaOp schema : module.getBodyRegion().front().getOps<mlir::dsdl::SchemaOp>())
        {
            // A schema is a type, and a type has a plan. The header a backend publishes
            // declares entry points for it, so a schema that carries none is malformed input
            // rather than something to pass over.
            std::size_t plans = 0;
            if (!schema.getBody().empty())
            {
                for (const mlir::dsdl::SerializationPlanOp plan :
                     schema.getBody().front().getOps<mlir::dsdl::SerializationPlanOp>())
                {
                    ++plans;
                    if (mlir::failed(buildPlan(module, schema, plan, built)))
                    {
                        signalPassFailure();
                        return;
                    }
                }
            }
            if (plans == 0)
            {
                schema.emitOpError("carries no serialisation plan");
                signalPassFailure();
                return;
            }
        }

        // Canonicalise what was built, here rather than downstream. A loop carries values its
        // body may not read and a branch may prove constant; those are dead results while they
        // are still scf, and become variables a target cannot remove once they are not.
        //
        // Applied to the built functions rather than the module. A serialisation plan holds a
        // region, has no results and declares no memory effects, so it is trivially dead to a
        // module-wide sweep -- which would delete the plans the next pass still has to read.
        mlir::RewritePatternSet cleanup(&getContext());
        for (const mlir::RegisteredOperationName name : getContext().getRegisteredOperations())
        {
            name.getCanonicalizationPatterns(cleanup, &getContext());
        }
        const mlir::FrozenRewritePatternSet frozen(std::move(cleanup));
        for (mlir::func::FuncOp fn : built)
        {
            if (mlir::failed(mlir::applyPatternsGreedily(fn, frozen)))
            {
                fn.emitError("failed to canonicalise built plan body");
                signalPassFailure();
                return;
            }
        }
    }
};

}  // namespace

std::unique_ptr<mlir::Pass> createBuildDSDLPlanBodiesPass()
{
    return std::make_unique<BuildDSDLPlanBodiesPass>();
}

void registerBuildDSDLPlanBodiesPass()
{
    static bool once = false;
    if (once)
    {
        return;
    }
    once = true;
    static mlir::PassRegistration<BuildDSDLPlanBodiesPass> const reg;
}

}  // namespace llvmdsdl
