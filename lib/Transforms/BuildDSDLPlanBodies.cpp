//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Builds serialization plan bodies as dialect operations.
///
/// `build-dsdl-plan-bodies` turns every `dsdl.serialization_plan` into a serialize and a
/// deserialize `func.func` over the plan operations, for whichever target converts them next. A
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

#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/ADT/StringRef.h>
#include <mlir/Dialect/Arith/IR/Arith.h>
#include <mlir/Dialect/Func/IR/FuncOps.h>
#include <mlir/Dialect/SCF/IR/SCF.h>
#include <mlir/IR/Builders.h>
#include <mlir/IR/BuiltinAttributes.h>
#include <mlir/IR/BuiltinOps.h>
#include <mlir/IR/BuiltinTypes.h>
#include <mlir/IR/DialectRegistry.h>
#include <mlir/IR/Location.h>
#include <mlir/IR/MLIRContext.h>
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

constexpr std::int64_t kRuntimeErrorInvalidArgument = 2;
constexpr std::int64_t kDelimiterHeaderBits         = 32;

/// @brief What a step carries forward: how far into the wire it got, and what went wrong.
///
/// Both travel as values rather than as constants folded at build time. A variable-length
/// array makes the offset depend on a count read from the object, so no offset after the
/// first array is known here.
struct PlanCursor final
{
    mlir::Value bitOffset;
    mlir::Value error;
};

/// @brief Whether a plan's steps can be built as operations yet.
///
/// Scalars and arrays of scalars. Composites and unions each need their own shape and are
/// still rendered as text, so a plan containing one falls back whole rather than in part: a
/// function is one body, and it is either operations or a string.
/// @brief Whether one field step can be built as operations.
///
/// Shared by the two shapes a plan comes in. A union is one field per option and a struct is
/// a sequence of them, but what a single field needs is the same either way, and having the
/// two disagree is how an option gets accepted that the arm builder cannot emit.
/// @brief Why one field cannot be built as operations, or nothing when it can.
std::optional<std::string> unsupportedFieldReason(const PlanStep& step)
{
    const std::string field = "field '" + (step.name.empty() ? step.cName : step.name) + "'";
    if (step.cName.empty())
    {
        return field + " has no c_name; bodies are built from a backend's final names";
    }
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
        if (step.compositeCTypeName.empty())
        {
            return field + " names no composite type";
        }
        if (!step.compositeSealed && step.delimiterValidateHelper.empty())
        {
            return field + " carries no delimiter-header validation helper; run lower-dsdl-exec first";
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
                                                 llvm::StringRef              capacityCheckSymbol,
                                                 llvm::StringRef              unionTagValidateSymbol,
                                                 llvm::StringRef              unionTagSerializeHelper,
                                                 llvm::StringRef              unionTagDeserializeHelper)
{
    if (capacityCheckSymbol.empty())
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

bool stepIsComposite(const PlanStep& step)
{
    return !step.compositeCTypeName.empty();
}

bool stepIsBitpackedArray(const PlanStep& step);

/// A bool array moves as one run of bits, and an array of composites element by element; both
/// are defined below, beside the other nesting builders, and reached from the array builders
/// above them.
PlanCursor buildBitpackedArray(mlir::OpBuilder& b,
                               mlir::Location   loc,
                               const PlanStep&  step,
                               std::int64_t     memberIndex,
                               mlir::Value      object,
                               mlir::Value      buffer,
                               mlir::Value      capacityBytes,
                               PlanCursor       cursor,
                               mlir::Value      count,
                               bool             writing);

PlanCursor buildCompositeElementLoop(mlir::OpBuilder& b,
                                     mlir::Location   loc,
                                     const PlanStep&  step,
                                     std::int64_t     memberIndex,
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

/// @brief The member path reaching a step's storage.
///
/// A scalar field is one name. An array's count and elements are two, because the generated
/// struct holds them in a nested member of its own.
mlir::ArrayAttr stepPath(mlir::OpBuilder& b, const PlanStep& step, llvm::StringRef leaf)
{
    if (leaf.empty())
    {
        return b.getStrArrayAttr({step.cName});
    }
    return b.getStrArrayAttr({step.cName, leaf});
}

/// @brief The C spelling of one array element's storage.
std::string elementTypeName(const PlanStep& step)
{
    if (step.scalarCategory == "float")
    {
        return (step.bitLength <= 32) ? "float" : "double";
    }
    const unsigned holder = holderWidthFor(step.bitLength);
    return std::string(step.scalarCategory == "signed" ? "int" : "uint") + std::to_string(holder) + "_t";
}

bool stepIsFixedArray(const PlanStep& step)
{
    return stepIsArray(step) && !isVariableArrayKind(step.arrayKind);
}

/// @brief The path to a step's element storage.
///
/// A variable-length array is a member holding a count and the elements beside it. A fixed one
/// is the elements: it has no count to hold, its length being in its declaration.
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

mlir::ArrayAttr elementPath(mlir::OpBuilder& b, const PlanStep& step)
{
    return stepIsFixedArray(step) ? b.getStrArrayAttr({step.cName}) : b.getStrArrayAttr({step.cName, "elements"});
}

mlir::DenseI64ArrayAttr elementIndices(mlir::OpBuilder& b, const PlanStep& step, const std::int64_t memberIndex)
{
    // A fixed array is the member; a variable one holds its elements in the first position of
    // the pair the member is, its count in the second.
    return stepIsFixedArray(step) ? b.getDenseI64ArrayAttr({memberIndex}) : b.getDenseI64ArrayAttr({memberIndex, 0});
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
/// The helper is not optional. The builder pass establishes that every helper a plan names is
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
    return PlanCursor{mlir::arith::AddIOp::create(b, loc, cursor.bitOffset, constantI64(b, loc, width)),
                      write.getError()};
}

/// @brief Runs @p body only while nothing has failed, threading the cursor either way.
template <typename BodyFn>
PlanCursor guarded(mlir::OpBuilder& b, mlir::Location loc, PlanCursor cursor, BodyFn body)
{
    const mlir::SmallVector<mlir::Type, 2> types{b.getIntegerType(64), b.getIntegerType(8)};
    auto guard = mlir::scf::IfOp::create(b, loc, types, isHealthy(b, loc, cursor.error), true);
    {
        mlir::OpBuilder::InsertionGuard const g(b);
        b.setInsertionPointToStart(guard.thenBlock());
        const PlanCursor next = body(cursor);
        mlir::scf::YieldOp::create(b, loc, mlir::ValueRange{next.bitOffset, next.error});
    }
    {
        mlir::OpBuilder::InsertionGuard const g(b);
        b.setInsertionPointToStart(guard.elseBlock());
        mlir::scf::YieldOp::create(b, loc, mlir::ValueRange{cursor.bitOffset, cursor.error});
    }
    return PlanCursor{guard.getResult(0), guard.getResult(1)};
}

/// @brief Serializes one scalar field.
PlanCursor buildScalarWrite(mlir::OpBuilder&   b,
                            mlir::Location     loc,
                            const PlanStep&    step,
                            const std::int64_t memberIndex,
                            mlir::Value        object,
                            mlir::Value        buffer,
                            mlir::Value        capacityBytes,
                            PlanCursor         cursor)
{
    return guarded(b, loc, cursor, [&](PlanCursor inner) {
        const mlir::Type valueType = stepValueType(b, step);
        mlir::Value      member =
            mlir::dsdl::LoadMemberOp::create(b,
                                             loc,
                                             valueType,
                                             object,
                                             stepPath(b, step, {}),
                                             b.getDenseI64ArrayAttr(llvm::ArrayRef<std::int64_t>{memberIndex}));
        markSigned(member.getDefiningOp(), step);
        member = normaliseScalar(b, loc, step, member, true);
        return emitWrite(b, loc, buffer, capacityBytes, inner, member, step.bitLength, step.scalarCategory == "signed");
    });
}

/// @brief Serializes one variable-length array: a validated count, its prefix, then elements.
PlanCursor buildArrayWrite(mlir::OpBuilder&   b,
                           mlir::Location     loc,
                           const PlanStep&    step,
                           const std::int64_t memberIndex,
                           mlir::Value        object,
                           mlir::Value        buffer,
                           mlir::Value        capacityBytes,
                           PlanCursor         cursor)
{
    return guarded(b, loc, cursor, [&](PlanCursor inner) {
        auto       i64Ty = b.getIntegerType(64);
        const bool fixed = stepIsFixedArray(step);

        // A fixed array's length is in its declaration, so there is nothing to read from the
        // object, nothing that could be out of range, and nothing to announce on the wire.
        mlir::Value count = constantI64(b, loc, step.arrayCapacity);
        if (!fixed)
        {
            count =
                mlir::dsdl::LoadMemberOp::create(b,
                                                 loc,
                                                 i64Ty,
                                                 object,
                                                 stepPath(b, step, "count"),
                                                 b.getDenseI64ArrayAttr(llvm::ArrayRef<std::int64_t>{memberIndex, 1}));

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
                return buildCompositeElementLoop(b,
                                                 loc,
                                                 step,
                                                 memberIndex,
                                                 object,
                                                 buffer,
                                                 capacityBytes,
                                                 afterPrefix,
                                                 count,
                                                 true);
            }
            if (stepIsBitpackedArray(step))
            {
                return buildBitpackedArray(b,
                                           loc,
                                           step,
                                           memberIndex,
                                           object,
                                           buffer,
                                           capacityBytes,
                                           afterPrefix,
                                           count,
                                           true);
            }

            // Driven by the offset rather than by a separate index. An scf.while's results
            // are exactly the values its condition forwards, so a carried index would also
            // be a result, and nothing after the loop reads it -- which the emitted C
            // declares and never uses. The index is recoverable from the offset, the
            // element width being fixed.
            const mlir::Value start = afterPrefix.bitOffset;
            const mlir::Value width = constantI64(b, loc, step.bitLength);
            const mlir::Value end =
                mlir::arith::AddIOp::create(b, loc, start, mlir::arith::MulIOp::create(b, loc, count, width));

            const mlir::SmallVector<mlir::Type, 2> loopTypes{i64Ty, b.getIntegerType(8)};
            auto loop = mlir::scf::WhileOp::create(b, loc, loopTypes, mlir::ValueRange{start, afterPrefix.error});

            {
                mlir::OpBuilder::InsertionGuard const g(b);
                mlir::Block* before = b.createBlock(&loop.getBefore(), {}, {i64Ty, b.getIntegerType(8)}, {loc, loc});
                b.setInsertionPointToStart(before);
                const mlir::Value more =
                    mlir::arith::CmpIOp::create(b, loc, mlir::arith::CmpIPredicate::ult, before->getArgument(0), end);
                const mlir::Value keep =
                    mlir::arith::AndIOp::create(b, loc, more, isHealthy(b, loc, before->getArgument(1)));
                mlir::scf::ConditionOp::create(b, loc, keep, before->getArguments());
            }
            {
                mlir::OpBuilder::InsertionGuard const g(b);
                mlir::Block* after = b.createBlock(&loop.getAfter(), {}, {i64Ty, b.getIntegerType(8)}, {loc, loc});
                b.setInsertionPointToStart(after);
                const mlir::Value offset   = after->getArgument(0);
                const mlir::Value incoming = after->getArgument(1);
                const mlir::Value index =
                    mlir::arith::DivUIOp::create(b, loc, mlir::arith::SubIOp::create(b, loc, offset, start), width);

                const mlir::Type valueType = stepValueType(b, step);
                mlir::Value      element =
                    mlir::dsdl::LoadElementOp::create(b,
                                                      loc,
                                                      valueType,
                                                      object,
                                                      elementPath(b, step),
                                                      elementIndices(b, step, memberIndex),
                                                      index,
                                                      b.getStringAttr("const " + elementTypeName(step)));
                markSigned(element.getDefiningOp(), step);
                element = normaliseScalar(b, loc, step, element, true);

                const PlanCursor  written = emitWrite(b,
                                                      loc,
                                                      buffer,
                                                      capacityBytes,
                                                      PlanCursor{offset, incoming},
                                                      element,
                                                      step.bitLength,
                                                      step.scalarCategory == "signed");
                const mlir::Value carried =
                    mlir::arith::SelectOp::create(b, loc, isHealthy(b, loc, incoming), written.error, incoming);
                mlir::scf::YieldOp::create(b, loc, mlir::ValueRange{written.bitOffset, carried});
            }
            return PlanCursor{loop.getResult(0), loop.getResult(1)};
        });
    });
}

/// @brief Pads to the next byte boundary, one zero bit at a time.
///
/// The count is not known here once an array is involved, so this is the loop the text form
/// writes rather than the unrolled writes a fixed layout would allow.
/// @brief Writes zero bits from the cursor up to @p end.
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
        const PlanCursor  written =
            emitWrite(b, loc, buffer, capacityBytes, PlanCursor{after->getArgument(0), incoming}, zeroBit, 1, false);
        // Every value a loop carries has to be read in its body. The condition already
        // guarantees this one is clear, so the select always takes the write's own result --
        // but binding it and dropping it leaves a declaration the emitted C never uses.
        const mlir::Value carried =
            mlir::arith::SelectOp::create(b, loc, isHealthy(b, loc, incoming), written.error, incoming);
        mlir::scf::YieldOp::create(b, loc, mlir::ValueRange{written.bitOffset, carried});
    }
    return PlanCursor{loop.getResult(0), loop.getResult(1)};
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
    const mlir::Value end = mlir::arith::AddIOp::create(b, loc, cursor.bitOffset, constantI64(b, loc, bits));
    if (!writing)
    {
        return PlanCursor{end, cursor.error};
    }
    return guarded(b, loc, cursor, [&](PlanCursor inner) {
        return buildZeroBitsTo(b, loc, buffer, capacityBytes, inner, end);
    });
}

PlanCursor buildFinalPadding(mlir::OpBuilder&            b,
                             mlir::Location              loc,
                             mlir::Value                 buffer,
                             mlir::Value                 capacityBytes,
                             PlanCursor                  cursor,
                             std::optional<std::int64_t> staticBitOffset)
{
    // A plan of fixed-width scalars ends at a known offset, so the padding is a known number
    // of writes and needs no loop. Only a variable-length array makes the end unknown.
    if (staticBitOffset.has_value())
    {
        const std::int64_t aligned = ((*staticBitOffset + 7) / 8) * 8;
        if (aligned == *staticBitOffset)
        {
            // Already on a boundary, so there is nothing to pad and the running offset
            // stands as it is.
            return cursor;
        }
        for (std::int64_t bit = *staticBitOffset; bit < aligned; ++bit)
        {
            cursor = guarded(b, loc, cursor, [&](PlanCursor inner) {
                const mlir::Value zeroBit = mlir::arith::ConstantOp::create(b, loc, b.getBoolAttr(false));
                return emitWrite(b, loc, buffer, capacityBytes, inner, zeroBit, 1, false);
            });
        }
        return PlanCursor{constantI64(b, loc, aligned), cursor.error};
    }

    const mlir::Value seven   = constantI64(b, loc, 7);
    const mlir::Value eight   = constantI64(b, loc, 8);
    const mlir::Value rounded = mlir::arith::AddIOp::create(b, loc, cursor.bitOffset, seven);
    const mlir::Value bytes   = mlir::arith::DivUIOp::create(b, loc, rounded, eight);
    const mlir::Value aligned = mlir::arith::MulIOp::create(b, loc, bytes, eight);
    return buildZeroBitsTo(b, loc, buffer, capacityBytes, cursor, aligned);
}

/// @brief Whether a step leaves the plan on a byte boundary, given it began on one.
///
/// A composite does: it consumes whole bytes, whatever it contains. That is what makes the
/// alignment before the next one a no-op, and it holds without knowing any offset.
bool stepPreservesByteAlignment(const PlanStep& step)
{
    if ((step.kind == PlanStepKind::Align) || stepIsComposite(step))
    {
        return true;
    }
    if (step.kind == PlanStepKind::Padding)
    {
        return (step.bits % 8) == 0;
    }
    if ((step.bitLength % 8) != 0)
    {
        return false;
    }
    return !stepIsArray(step) || ((step.arrayLengthPrefixBits % 8) == 0);
}

/// @brief Rounds the running offset up to a byte boundary.
///
/// Serializing has to write the bits it skips, because the buffer is the output. Reading does
/// not: the offset simply moves.
PlanCursor buildAlignment(mlir::OpBuilder& b,
                          mlir::Location   loc,
                          mlir::Value      buffer,
                          mlir::Value      capacityBytes,
                          PlanCursor       cursor,
                          const bool       writing,
                          const bool       alreadyAligned)
{
    if (alreadyAligned)
    {
        // Nothing to skip to. Worth knowing statically rather than emitting a loop that
        // never runs: every field before this one occupied whole bytes, or a nested
        // composite left the plan on a boundary by construction.
        return cursor;
    }
    const mlir::Value seven   = constantI64(b, loc, 7);
    const mlir::Value eight   = constantI64(b, loc, 8);
    const mlir::Value rounded = mlir::arith::AddIOp::create(b, loc, cursor.bitOffset, seven);
    const mlir::Value bytes   = mlir::arith::DivUIOp::create(b, loc, rounded, eight);
    const mlir::Value aligned = mlir::arith::MulIOp::create(b, loc, bytes, eight);
    if (!writing)
    {
        return PlanCursor{aligned, cursor.error};
    }
    return buildFinalPadding(b, loc, buffer, capacityBytes, cursor, std::nullopt);
}

/// @brief The space the wire buffer has left at the plan's current position.
mlir::Value remainingBytes(mlir::OpBuilder& b, mlir::Location loc, mlir::Value capacityBytes, mlir::Value bitOffset)
{
    const mlir::Value used  = mlir::arith::DivUIOp::create(b, loc, bitOffset, constantI64(b, loc, 8));
    const mlir::Value fits  = mlir::arith::CmpIOp::create(b, loc, mlir::arith::CmpIPredicate::ult, used, capacityBytes);
    const mlir::Value taken = mlir::arith::SelectOp::create(b, loc, fits, used, capacityBytes);
    return mlir::arith::SubIOp::create(b, loc, capacityBytes, taken);
}

/// @brief The dialect's pointer to @p step's nested type, qualified for the direction.
mlir::dsdl::PtrType nestedPointerType(mlir::MLIRContext* ctx, const PlanStep& step, const bool writing)
{
    return mlir::dsdl::PtrType::get(ctx,
                                    mlir::dsdl::OpaqueType::get(ctx,
                                                                (writing ? "const " : "") + step.compositeCTypeName));
}

/// @brief The dialect's pointer into the wire buffer, qualified for the direction.
mlir::dsdl::PtrType wirePointerType(mlir::MLIRContext* ctx, const bool writing)
{
    return mlir::dsdl::PtrType::get(ctx, mlir::dsdl::OpaqueType::get(ctx, writing ? "uint8_t" : "const uint8_t"));
}

/// @brief Encodes or decodes one sealed nested composite at @p target through its own entry point.
///
/// The nested type is handed its storage, the point the container reached, and the space left;
/// it answers with what it used, and the container advances by that. The container does not
/// know the nested layout and does not need to.
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
    auto  sizePtr = mlir::dsdl::PtrType::get(ctx, mlir::dsdl::OpaqueType::get(ctx, "size_t"));

    const mlir::Value available  = remainingBytes(b, loc, capacityBytes, inner.bitOffset);
    const mlir::Value sizeSlot   = mlir::dsdl::LocalOp::create(b, loc, sizePtr, available);
    const mlir::Value byteOffset = mlir::arith::DivUIOp::create(b, loc, inner.bitOffset, constantI64(b, loc, 8));
    const mlir::Value at = mlir::dsdl::BufferAtOp::create(b, loc, wirePointerType(ctx, writing), buffer, byteOffset);

    const std::string callee = step.compositeCTypeName + (writing ? "__serialize_" : "__deserialize_");
    auto              call =
        mlir::dsdl::CallSerdesOp::create(b, loc, b.getIntegerType(8), b.getStringAttr(callee), target, at, sizeSlot);

    // Read back before branching on the error: the nested call reports what it used in the
    // same place either way, and the select below decides whether it counts.
    const mlir::Value used = mlir::dsdl::LoadScalarOp::create(b, loc, i64Ty, sizeSlot);
    const mlir::Value advanced =
        mlir::arith::AddIOp::create(b,
                                    loc,
                                    inner.bitOffset,
                                    mlir::arith::MulIOp::create(b, loc, used, constantI64(b, loc, 8)));
    const mlir::Value ok      = isHealthy(b, loc, call.getError());
    const mlir::Value nextOff = mlir::arith::SelectOp::create(b, loc, ok, advanced, inner.bitOffset);
    return PlanCursor{nextOff, call.getError()};
}

/// @brief Encodes or decodes one delimited nested composite at @p target.
///
/// A delimited nested type is preceded by its own length in bytes, so that a reader which
/// does not know the type can step over it. That is the whole point of the header, and it is
/// why the decoder advances by the length it was told rather than by what the nested decode
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
    auto  sizePtr = mlir::dsdl::PtrType::get(ctx, mlir::dsdl::OpaqueType::get(ctx, "size_t"));

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
    const mlir::Value afterHeader =
        mlir::arith::AddIOp::create(b, loc, headerOffset, constantI64(b, loc, kDelimiterHeaderBits));
    const mlir::Value remaining = remainingBytes(b, loc, capacityBytes, afterHeader);

    // Serializing does not know the length until the nested type reports it, so the header
    // is reserved here and written once the encoding below has run.
    const mlir::Value sizeInit = writing ? remaining : declared;
    const mlir::Value sizeSlot = mlir::dsdl::LocalOp::create(b, loc, sizePtr, sizeInit);

    mlir::Value error = inner.error;
    if (!writing)
    {
        error = foldError(b,
                          loc,
                          error,
                          callErrorHelper(b, loc, step.delimiterValidateHelper, mlir::ValueRange{declared, remaining}));
    }

    return guarded(b, loc, PlanCursor{afterHeader, error}, [&](PlanCursor ready) {
        const mlir::Value at =
            mlir::dsdl::BufferAtOp::create(b,
                                           loc,
                                           wirePointerType(ctx, writing),
                                           buffer,
                                           mlir::arith::DivUIOp::create(b, loc, ready.bitOffset, eight));

        const std::string callee = step.compositeCTypeName + (writing ? "__serialize_" : "__deserialize_");
        auto              call   = mlir::dsdl::CallSerdesOp::create(b,
                                                                    loc,
                                                                    b.getIntegerType(8),
                                                                    b.getStringAttr(callee),
                                                                    target,
                                                                    at,
                                                                    sizeSlot);

        mlir::Value err = call.getError();

        // The length the reader will be told. Serializing learns it from the nested type,
        // reading it back out of the slot the callee wrote. Decoding was told it up front
        // and steps that far regardless of what the nested decode consumed, so it never
        // reads the slot back at all.
        const mlir::Value span = writing ? mlir::dsdl::LoadScalarOp::create(b, loc, i64Ty, sizeSlot) : declared;

        if (writing)
        {
            err = foldError(b,
                            loc,
                            err,
                            callErrorHelper(b, loc, step.delimiterValidateHelper, mlir::ValueRange{span, remaining}));
        }

        return guarded(b, loc, PlanCursor{ready.bitOffset, err}, [&](PlanCursor done) {
            const mlir::Value after =
                mlir::arith::AddIOp::create(b, loc, done.bitOffset, mlir::arith::MulIOp::create(b, loc, span, eight));
            mlir::Value outcome = done.error;
            if (writing)
            {
                auto header = mlir::dsdl::WriteBitsOp::create(b,
                                                              loc,
                                                              b.getIntegerType(8),
                                                              buffer,
                                                              capacityBytes,
                                                              headerOffset,
                                                              span,
                                                              b.getI64IntegerAttr(kDelimiterHeaderBits),
                                                              nullptr);
                outcome     = foldError(b, loc, outcome, header.getError());
            }
            return PlanCursor{after, outcome};
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

/// @brief Encodes or decodes one nested composite member.
PlanCursor buildCompositeStep(mlir::OpBuilder&   b,
                              mlir::Location     loc,
                              const PlanStep&    step,
                              const std::int64_t memberIndex,
                              mlir::Value        object,
                              mlir::Value        buffer,
                              mlir::Value        capacityBytes,
                              PlanCursor         cursor,
                              const bool         writing)
{
    return guarded(b, loc, cursor, [&](PlanCursor inner) {
        const mlir::Value target = mlir::dsdl::MemberAddrOp::create(b,
                                                                    loc,
                                                                    nestedPointerType(b.getContext(), step, writing),
                                                                    object,
                                                                    b.getStrArrayAttr({step.cName}),
                                                                    b.getDenseI64ArrayAttr({memberIndex}));
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

/// @brief Runs one option's steps when the tag selects it, and passes the cursor through
///        untouched otherwise.
///
/// A union encodes exactly one of its options, so the chain of tests is the plan: whichever
/// arm the tag selects contributes, and the rest contribute nothing. An option that the tag
/// did not select must not advance the offset, which is why each arm yields the cursor it
/// was given rather than a merged one.
template <typename StepFn>
PlanCursor buildUnionOption(mlir::OpBuilder& b,
                            mlir::Location   loc,
                            mlir::Value      tag,
                            const PlanStep&  option,
                            PlanCursor       cursor,
                            StepFn           emitStep)
{
    const mlir::Value selected = mlir::arith::CmpIOp::create(b,
                                                             loc,
                                                             mlir::arith::CmpIPredicate::eq,
                                                             tag,
                                                             constantI64(b, loc, option.unionOptionIndex));

    const mlir::SmallVector<mlir::Type, 2> types{b.getIntegerType(64), b.getIntegerType(8)};
    auto                                   arm = mlir::scf::IfOp::create(b, loc, types, selected, true);
    {
        mlir::OpBuilder::InsertionGuard const g(b);
        b.setInsertionPointToStart(arm.thenBlock());
        const PlanCursor next = emitStep(cursor);
        mlir::scf::YieldOp::create(b, loc, mlir::ValueRange{next.bitOffset, next.error});
    }
    {
        mlir::OpBuilder::InsertionGuard const g(b);
        b.setInsertionPointToStart(arm.elseBlock());
        mlir::scf::YieldOp::create(b, loc, mlir::ValueRange{cursor.bitOffset, cursor.error});
    }
    return PlanCursor{arm.getResult(0), arm.getResult(1)};
}

bool stepIsBitpackedArray(const PlanStep& step)
{
    return stepIsArray(step) && (step.scalarCategory == "bool");
}

/// @brief Moves a bool array, which is stored bitpacked rather than as elements.
///
/// One run of bits rather than a loop: the storage already has the layout the wire wants, so
/// the whole array travels in a single copy whose length is the array's count.
PlanCursor buildBitpackedArray(mlir::OpBuilder&   b,
                               mlir::Location     loc,
                               const PlanStep&    step,
                               const std::int64_t memberIndex,
                               mlir::Value        object,
                               mlir::Value        buffer,
                               mlir::Value        capacityBytes,
                               PlanCursor         cursor,
                               mlir::Value        count,
                               const bool         writing)
{
    auto*      ctx       = b.getContext();
    const auto qualifier = writing ? std::string("const ") : std::string();
    auto       bytePtr   = mlir::dsdl::PtrType::get(ctx, mlir::dsdl::OpaqueType::get(ctx, qualifier + "uint8_t"));

    // A variable-length bool array keeps its bits in a `bitpacked` member beside the count; a
    // fixed one has no count, so the member is the storage.
    const bool        fixed  = stepIsFixedArray(step);
    const mlir::Value packed = mlir::dsdl::ElementAddrOp::create(b,
                                                                 loc,
                                                                 bytePtr,
                                                                 object,
                                                                 fixed ? b.getStrArrayAttr({step.cName})
                                                                       : b.getStrArrayAttr({step.cName, "bitpacked"}),
                                                                 fixed ? b.getDenseI64ArrayAttr({memberIndex})
                                                                       : b.getDenseI64ArrayAttr({memberIndex, 0}),
                                                                 constantI64(b, loc, 0),
                                                                 b.getStringAttr(qualifier + "uint8_t"));
    if (writing)
    {
        mlir::dsdl::BitWriteOp::create(b, loc, buffer, cursor.bitOffset, count, packed, constantI64(b, loc, 0));
    }
    else
    {
        mlir::dsdl::BitReadOp::create(b, loc, packed, buffer, capacityBytes, cursor.bitOffset, count);
    }
    return PlanCursor{mlir::arith::AddIOp::create(b, loc, cursor.bitOffset, count), cursor.error};
}

/// @brief Encodes or decodes an array of nested composites, one element at a time.
///
/// Each element goes through the nested type's own entry point and reports its own length, so
/// unlike an array of scalars the stride is not known and the loop cannot be driven by the
/// offset. It is counted instead, with `scf.for`, whose induction variable is not among its
/// results -- an `scf.while` would make the index a result nothing reads, and that reaches
/// the emitted C as a variable nothing uses.
PlanCursor buildCompositeElementLoop(mlir::OpBuilder&   b,
                                     mlir::Location     loc,
                                     const PlanStep&    step,
                                     const std::int64_t memberIndex,
                                     mlir::Value        object,
                                     mlir::Value        buffer,
                                     mlir::Value        capacityBytes,
                                     PlanCursor         cursor,
                                     mlir::Value        count,
                                     const bool         writing)
{
    auto*      ctx       = b.getContext();
    auto       i64Ty     = b.getIntegerType(64);
    auto       indexTy   = b.getIndexType();
    const auto qualifier = writing ? std::string("const ") : std::string();

    const mlir::Value zero  = mlir::arith::ConstantIndexOp::create(b, loc, 0);
    const mlir::Value one   = mlir::arith::ConstantIndexOp::create(b, loc, 1);
    const mlir::Value bound = mlir::arith::IndexCastOp::create(b, loc, indexTy, count);

    auto loop = mlir::scf::ForOp::create(b, loc, zero, bound, one, mlir::ValueRange{cursor.bitOffset, cursor.error});
    {
        mlir::OpBuilder::InsertionGuard const g(b);
        b.setInsertionPointToStart(loop.getBody());
        const mlir::Value index = mlir::arith::IndexCastOp::create(b, loc, i64Ty, loop.getInductionVar());
        const PlanCursor  carried{loop.getRegionIterArg(0), loop.getRegionIterArg(1)};

        const PlanCursor next = guarded(b, loc, carried, [&](PlanCursor inner) {
            const mlir::Value target =
                mlir::dsdl::ElementAddrOp::create(b,
                                                  loc,
                                                  nestedPointerType(ctx, step, writing),
                                                  object,
                                                  elementPath(b, step),
                                                  elementIndices(b, step, memberIndex),
                                                  index,
                                                  b.getStringAttr(qualifier + step.compositeCTypeName));
            return buildNested(b, loc, step, target, buffer, capacityBytes, inner, writing);
        });
        mlir::scf::YieldOp::create(b, loc, mlir::ValueRange{next.bitOffset, next.error});
    }
    return PlanCursor{loop.getResult(0), loop.getResult(1)};
}

/// @brief Where a step's field sits among the generated struct's members.
///
/// The struct is the non-padding fields in declaration order: a `void` field reserves wire
/// bits and has nothing to hold, so it takes no member and no position. A union lists its
/// options and then `_tag_`, which is why the tag's index is the option count.
///
/// The C path never reads this -- it has the member's name -- and object emission has nothing
/// else to go on, so it is the one place the two targets are told apart by more than spelling.
std::vector<std::int64_t> memberIndicesFor(const std::vector<PlanStep>& steps)
{
    std::vector<std::int64_t> indices(steps.size(), -1);
    std::int64_t              next = 0;
    for (std::size_t i = 0; i < steps.size(); ++i)
    {
        if (steps[i].kind == PlanStepKind::Field)
        {
            indices[i] = next++;
        }
    }
    return indices;
}

/// @brief The index of a union's `_tag_`, which the struct places after the options.
std::int64_t unionTagMemberIndex(const std::vector<PlanStep>& steps)
{
    return static_cast<std::int64_t>(unionOptionsOf(steps).size());
}

/// @brief Builds a typed serialize body as operations.
///
/// The published header declares this symbol, so the parameter spellings are the header's.
/// Control flow is structured, because the C path has no branch-graph conversion, so what the
/// hand-written text says with an early return this says by carrying an error through the
/// cursor.
mlir::LogicalResult buildTypedSerializeBody(mlir::OpBuilder&             builder,
                                            mlir::ModuleOp               module,
                                            mlir::Location               loc,
                                            llvm::StringRef              functionName,
                                            llvm::StringRef              cTypeName,
                                            const std::vector<PlanStep>& steps,
                                            llvm::StringRef              capacityCheckSymbol,
                                            const bool                   isUnion,
                                            const std::int64_t           unionTagBits,
                                            llvm::StringRef              unionTagValidateSymbol,
                                            llvm::StringRef              unionTagHelper)
{
    if (capacityCheckSymbol.empty() || !module.lookupSymbol<mlir::func::FuncOp>(capacityCheckSymbol))
    {
        return mlir::failure();
    }

    mlir::OpBuilder::InsertionGuard const outer(builder);
    builder.setInsertionPointToEnd(&module.getBodyRegion().front());

    auto* ctx    = builder.getContext();
    auto  objTy  = mlir::dsdl::PtrType::get(ctx, mlir::dsdl::OpaqueType::get(ctx, ("const " + cTypeName).str()));
    auto  bufTy  = mlir::dsdl::PtrType::get(ctx, mlir::dsdl::OpaqueType::get(ctx, "uint8_t"));
    auto  sizeTy = mlir::dsdl::PtrType::get(ctx, mlir::dsdl::OpaqueType::get(ctx, "size_t"));
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
            callErrorHelper(builder, loc, capacityCheckSymbol, mlir::ValueRange{capacityBits});

        const std::vector<std::int64_t> members     = memberIndicesFor(steps);
        bool                            byteAligned = true;
        PlanCursor                      cursor{constantI64(builder, loc, 0), capacityError};

        if (isUnion)
        {
            // The tag comes off the object, is normalised, and is validated before anything
            // is written: a tag naming no option selects nothing, and the plan stops there.
            const mlir::Value rawTag =
                mlir::dsdl::LoadMemberOp::create(builder,
                                                 loc,
                                                 i64Ty,
                                                 object,
                                                 builder.getStrArrayAttr({"_tag_"}),
                                                 builder.getDenseI64ArrayAttr(
                                                     llvm::ArrayRef<std::int64_t>{unionTagMemberIndex(steps)}));
            const mlir::Value tagValue = applyHelper(builder, loc, unionTagHelper, rawTag);
            cursor.error = foldError(builder,
                                     loc,
                                     cursor.error,
                                     callErrorHelper(builder, loc, unionTagValidateSymbol, mlir::ValueRange{tagValue}));
            cursor       = guarded(builder, loc, cursor, [&](PlanCursor inner) {
                return emitWrite(builder, loc, buffer, capacityBytes, inner, tagValue, unionTagBits, false);
            });

            for (const PlanStep* option : unionOptionsOf(steps))
            {
                cursor = buildUnionOption(builder, loc, rawTag, *option, cursor, [&](PlanCursor arm) {
                    if (option->alignmentBits > 1)
                    {
                        arm = buildAlignment(builder, loc, buffer, capacityBytes, arm, true, false);
                    }
                    if (stepIsArray(*option))
                    {
                        return buildArrayWrite(builder,
                                               loc,
                                               *option,
                                               option->unionOptionIndex,
                                               object,
                                               buffer,
                                               capacityBytes,
                                               arm);
                    }
                    if (stepIsComposite(*option))
                    {
                        return buildCompositeStep(builder,
                                                  loc,
                                                  *option,
                                                  option->unionOptionIndex,
                                                  object,
                                                  buffer,
                                                  capacityBytes,
                                                  arm,
                                                  true);
                    }
                    return buildScalarWrite(builder,
                                            loc,
                                            *option,
                                            option->unionOptionIndex,
                                            object,
                                            buffer,
                                            capacityBytes,
                                            arm);
                });
            }
            byteAligned = false;
        }
        for (std::size_t index = 0; isUnion ? false : (index < steps.size()); ++index)
        {
            const PlanStep&    step        = steps[index];
            const std::int64_t memberIndex = members[index];
            const bool         aligned     = byteAligned;
            byteAligned                    = byteAligned && stepPreservesByteAlignment(step);
            if (step.kind == PlanStepKind::Align)
            {
                cursor = buildAlignment(builder, loc, buffer, capacityBytes, cursor, true, aligned);
            }
            else if (step.kind == PlanStepKind::Padding)
            {
                cursor = buildPaddingStep(builder, loc, buffer, capacityBytes, cursor, step.bits, true);
            }
            else if (stepIsArray(step))
            {
                cursor = buildArrayWrite(builder, loc, step, memberIndex, object, buffer, capacityBytes, cursor);
            }
            else if (stepIsComposite(step))
            {
                cursor =
                    buildCompositeStep(builder, loc, step, memberIndex, object, buffer, capacityBytes, cursor, true);
            }
            else
            {
                cursor = buildScalarWrite(builder, loc, step, memberIndex, object, buffer, capacityBytes, cursor);
            }
        }
        // Where the plan ends is known when nothing varies, and known to be byte-aligned
        // whenever every width is a whole number of bytes -- an array of them lands on a
        // boundary whatever its count. Either way the trailing padding is not a loop.
        std::optional<std::int64_t> staticEnd;
        // A nested composite's length is its own to decide, so anything after one is as
        // unknown as anything after an array.
        const bool anyVariable = std::ranges::any_of(steps, [](const PlanStep& step) {
            return stepIsArray(step) || stepIsComposite(step) || (step.kind == PlanStepKind::Align);
        });
        if (!anyVariable)
        {
            std::int64_t total = 0;
            for (const auto& step : steps)
            {
                total += step.bitLength;
            }
            staticEnd = total;
        }
        else
        {
            const bool wholeBytes = std::ranges::all_of(steps, [](const PlanStep& step) {
                if ((step.kind == PlanStepKind::Align) || stepIsComposite(step))
                {
                    // Both land the plan on a byte boundary by construction.
                    return true;
                }
                const bool widthWhole  = (step.bitLength % 8) == 0;
                const bool prefixWhole = !stepIsArray(step) || ((step.arrayLengthPrefixBits % 8) == 0);
                return widthWhole && prefixWhole;
            });
            if (wholeBytes || byteAligned)
            {
                staticEnd = 0;
            }
        }
        cursor = buildFinalPadding(builder, loc, buffer, capacityBytes, cursor, staticEnd);

        auto epilogue =
            mlir::scf::IfOp::create(builder, loc, mlir::TypeRange{i8Ty}, isHealthy(builder, loc, cursor.error), true);
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

/// @brief Deserializes one scalar field, advancing the offset past it.
///
/// No guard, unlike the serialize side. A read cannot fail: the runtime answers a short
/// buffer by zero-extending, which is the tolerance a deserializer is required to have.
PlanCursor buildScalarRead(mlir::OpBuilder&   b,
                           mlir::Location     loc,
                           const PlanStep&    step,
                           const std::int64_t memberIndex,
                           mlir::Value        object,
                           mlir::Value        buffer,
                           mlir::Value        capacityBytes,
                           PlanCursor         cursor)
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
    markSigned(mlir::dsdl::StoreMemberOp::create(b,
                                                 loc,
                                                 object,
                                                 stepPath(b, step, {}),
                                                 b.getDenseI64ArrayAttr(llvm::ArrayRef<std::int64_t>{memberIndex}),
                                                 raw),
               step);
    return PlanCursor{mlir::arith::AddIOp::create(b, loc, bitOffset, constantI64(b, loc, step.bitLength)),
                      cursor.error};
}

/// @brief Deserializes one variable-length array.
///
/// The count comes off the wire and is clamped to the declared capacity before it is used to
/// bound the loop: a length prefix is attacker-controlled, and a decoder that trusted it would
/// write past the elements it has.
/// @brief Reads @p count elements into the object, advancing past them.
PlanCursor buildArrayElementReads(mlir::OpBuilder&   b,
                                  mlir::Location     loc,
                                  const PlanStep&    step,
                                  const std::int64_t memberIndex,
                                  mlir::Value        object,
                                  mlir::Value        buffer,
                                  mlir::Value        capacityBytes,
                                  PlanCursor         cursor,
                                  mlir::Value        count)
{
    if (stepIsComposite(step))
    {
        return buildCompositeElementLoop(b,
                                         loc,
                                         step,
                                         memberIndex,
                                         object,
                                         buffer,
                                         capacityBytes,
                                         cursor,
                                         count,
                                         false);
    }
    if (stepIsBitpackedArray(step))
    {
        return buildBitpackedArray(b, loc, step, memberIndex, object, buffer, capacityBytes, cursor, count, false);
    }
    auto              i64Ty  = b.getIntegerType(64);
    const mlir::Value offset = cursor.bitOffset;
    // Driven by the offset alone, for the same reason as the serialize side: a carried index
    // would also be a loop result, and nothing after the loop reads it.
    const mlir::Value start = offset;
    const mlir::Value width = constantI64(b, loc, step.bitLength);
    const mlir::Value end =
        mlir::arith::AddIOp::create(b, loc, start, mlir::arith::MulIOp::create(b, loc, count, width));

    auto loop = mlir::scf::WhileOp::create(b, loc, mlir::TypeRange{i64Ty}, mlir::ValueRange{start});
    {
        mlir::OpBuilder::InsertionGuard const g(b);
        mlir::Block*                          before = b.createBlock(&loop.getBefore(), {}, {i64Ty}, {loc});
        b.setInsertionPointToStart(before);
        const mlir::Value more =
            mlir::arith::CmpIOp::create(b, loc, mlir::arith::CmpIPredicate::ult, before->getArgument(0), end);
        mlir::scf::ConditionOp::create(b, loc, more, before->getArguments());
    }
    {
        mlir::OpBuilder::InsertionGuard const g(b);
        mlir::Block*                          after = b.createBlock(&loop.getAfter(), {}, {i64Ty}, {loc});
        b.setInsertionPointToStart(after);
        const mlir::Value at = after->getArgument(0);
        const mlir::Value index =
            mlir::arith::DivUIOp::create(b, loc, mlir::arith::SubIOp::create(b, loc, at, start), width);

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
                                                      elementPath(b, step),
                                                      elementIndices(b, step, memberIndex),
                                                      index,
                                                      element,
                                                      b.getStringAttr(elementTypeName(step))),
                   step);
        mlir::scf::YieldOp::create(b, loc, mlir::ValueRange{mlir::arith::AddIOp::create(b, loc, at, width)});
    }
    return PlanCursor{loop.getResult(0), cursor.error};
}

PlanCursor buildArrayRead(mlir::OpBuilder&   b,
                          mlir::Location     loc,
                          const PlanStep&    step,
                          const std::int64_t memberIndex,
                          mlir::Value        object,
                          mlir::Value        buffer,
                          mlir::Value        capacityBytes,
                          PlanCursor         cursor)
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
                return buildArrayElementReads(b, loc, step, memberIndex, object, buffer, capacityBytes, inner, count);
            });
        }

        mlir::Value wireLength = mlir::dsdl::ReadBitsOp::create(b,
                                                                loc,
                                                                i64Ty,
                                                                buffer,
                                                                capacityBytes,
                                                                bitOffset,
                                                                b.getI64IntegerAttr(step.arrayLengthPrefixBits),
                                                                nullptr);
        wireLength             = applyHelper(b, loc, step.deserArrayLengthPrefixHelper, wireLength);
        const mlir::Value offset =
            mlir::arith::AddIOp::create(b, loc, bitOffset, constantI64(b, loc, step.arrayLengthPrefixBits));

        // The length off the wire is stored as it was read and then validated, not clamped to
        // the declared capacity. A prefix longer than the array can hold is malformed input, and
        // a decoder that quietly truncated it would accept a message the sender did not send.
        mlir::dsdl::StoreMemberOp::create(b,
                                          loc,
                                          object,
                                          stepPath(b, step, "count"),
                                          b.getDenseI64ArrayAttr(llvm::ArrayRef<std::int64_t>{memberIndex, 1}),
                                          wireLength);
        const mlir::Value count = wireLength;
        const mlir::Value error = callErrorHelper(b, loc, step.arrayLengthValidateHelper, mlir::ValueRange{count});

        return guarded(b, loc, PlanCursor{offset, error}, [&](PlanCursor inner) {
            return buildArrayElementReads(b, loc, step, memberIndex, object, buffer, capacityBytes, inner, count);
        });
    });
}

/// @brief Builds a typed deserialize body as operations.
///
/// The argument check is not the serialize one reversed, and its order is load-bearing. A null
/// buffer is legal when the declared size is zero, and C reaches that clause by short-circuit,
/// having already established the size pointer is non-null. Reading the size eagerly would
/// dereference null on exactly the call the check exists to reject.
mlir::LogicalResult buildTypedDeserializeBody(mlir::OpBuilder&             builder,
                                              mlir::ModuleOp               module,
                                              mlir::Location               loc,
                                              llvm::StringRef              functionName,
                                              llvm::StringRef              cTypeName,
                                              const std::vector<PlanStep>& steps,
                                              const bool                   isUnion,
                                              const std::int64_t           unionTagBits,
                                              llvm::StringRef              unionTagValidateSymbol,
                                              llvm::StringRef              unionTagHelper)
{
    mlir::OpBuilder::InsertionGuard const outer(builder);
    builder.setInsertionPointToEnd(&module.getBodyRegion().front());

    auto* ctx    = builder.getContext();
    auto  objTy  = mlir::dsdl::PtrType::get(ctx, mlir::dsdl::OpaqueType::get(ctx, cTypeName));
    auto  bufTy  = mlir::dsdl::PtrType::get(ctx, mlir::dsdl::OpaqueType::get(ctx, "const uint8_t"));
    auto  sizeTy = mlir::dsdl::PtrType::get(ctx, mlir::dsdl::OpaqueType::get(ctx, "size_t"));
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

        const std::vector<std::int64_t> members     = memberIndicesFor(steps);
        bool                            byteAligned = true;
        PlanCursor                      cursor{constantI64(builder, loc, 0), constantI8(builder, loc, 0)};

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
                mlir::dsdl::StoreMemberOp::create(builder,
                                                  loc,
                                                  object,
                                                  builder.getStrArrayAttr({"_tag_"}),
                                                  builder.getDenseI64ArrayAttr(
                                                      llvm::ArrayRef<std::int64_t>{unionTagMemberIndex(steps)}),
                                                  tagValue);
                return PlanCursor{mlir::arith::AddIOp::create(builder,
                                                              loc,
                                                              inner.bitOffset,
                                                              constantI64(builder, loc, unionTagBits)),
                                  inner.error};
            });

            for (const PlanStep* option : unionOptionsOf(steps))
            {
                cursor = buildUnionOption(builder, loc, tagValue, *option, cursor, [&](PlanCursor arm) {
                    if (option->alignmentBits > 1)
                    {
                        arm = buildAlignment(builder, loc, readable, capacityBytes, arm, false, false);
                    }
                    if (stepIsArray(*option))
                    {
                        return buildArrayRead(builder,
                                              loc,
                                              *option,
                                              option->unionOptionIndex,
                                              object,
                                              readable,
                                              capacityBytes,
                                              arm);
                    }
                    if (stepIsComposite(*option))
                    {
                        return buildCompositeStep(builder,
                                                  loc,
                                                  *option,
                                                  option->unionOptionIndex,
                                                  object,
                                                  readable,
                                                  capacityBytes,
                                                  arm,
                                                  false);
                    }
                    return buildScalarRead(builder,
                                           loc,
                                           *option,
                                           option->unionOptionIndex,
                                           object,
                                           readable,
                                           capacityBytes,
                                           arm);
                });
            }
            byteAligned = false;
        }
        for (std::size_t index = 0; isUnion ? false : (index < steps.size()); ++index)
        {
            const PlanStep&    step        = steps[index];
            const std::int64_t memberIndex = members[index];
            const bool         aligned     = byteAligned;
            byteAligned                    = byteAligned && stepPreservesByteAlignment(step);
            if (step.kind == PlanStepKind::Align)
            {
                cursor = buildAlignment(builder, loc, readable, capacityBytes, cursor, false, aligned);
            }
            else if (step.kind == PlanStepKind::Padding)
            {
                cursor = buildPaddingStep(builder, loc, readable, capacityBytes, cursor, step.bits, false);
            }
            else if (stepIsArray(step))
            {
                cursor = buildArrayRead(builder, loc, step, memberIndex, object, readable, capacityBytes, cursor);
            }
            else if (stepIsComposite(step))
            {
                cursor =
                    buildCompositeStep(builder, loc, step, memberIndex, object, readable, capacityBytes, cursor, false);
            }
            else
            {
                cursor = buildScalarRead(builder, loc, step, memberIndex, object, readable, capacityBytes, cursor);
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

/// @brief Builds every serialization plan's bodies as operations, before a target is chosen.
///
/// A plan becomes a serialize and a deserialize `func.func` over the plan operations, in the
/// dialect's own vocabulary. Which target converts them next is not this pass's concern: the C
/// conversion and the LLVM conversion read the same bodies, and neither renders a body of its
/// own. A plan this pass cannot express fails it, naming the step and the reason.
///
/// Whether a body was built is asked of the module by symbol rather than recorded in state, so
/// a later pass cannot come to disagree with this one about it.
struct BuildDSDLPlanBodiesPass : public mlir::PassWrapper<BuildDSDLPlanBodiesPass, mlir::OperationPass<mlir::ModuleOp>>
{
    llvm::StringRef getArgument() const final
    {
        return "build-dsdl-plan-bodies";
    }
    llvm::StringRef getDescription() const final
    {
        return "Build DSDL serialization plan bodies as dialect operations";
    }
    void getDependentDialects(mlir::DialectRegistry& registry) const override
    {
        registry.insert<mlir::dsdl::DSDLDialect,
                        mlir::func::FuncDialect,
                        mlir::arith::ArithDialect,
                        mlir::scf::SCFDialect>();
    }

    /// @brief Builds both bodies of @p plan, or says why it cannot.
    mlir::LogicalResult buildPlan(mlir::ModuleOp                             module,
                                  mlir::dsdl::SchemaOp                       schema,
                                  mlir::dsdl::SerializationPlanOp            plan,
                                  mlir::SmallVectorImpl<mlir::func::FuncOp>& built)
    {
        if (!module->hasAttr("llvmdsdl.names_final"))
        {
            // Lowering stamps the spelling it can guess at, and a body built over those would
            // name members and call symbols no backend emits.
            return plan.emitOpError("bodies are built from a backend's final C names, and this module carries "
                                    "none; stamp them and set 'llvmdsdl.names_final'");
        }
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

        const std::string cTypeName = plan.getCTypeName().str();
        if (cTypeName.empty())
        {
            return plan.emitOpError(
                "carries no 'c_type_name'; a body is built against the struct the backend declares");
        }

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
                                                 cTypeName,
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
                                                   cTypeName,
                                                   steps,
                                                   isUnion,
                                                   unionTagBits,
                                                   unionTagValidateSymbol,
                                                   unionTagDeserializeHelper)))
        {
            return plan.emitOpError("deserialize body could not be built");
        }
        for (const std::string& name : {fnStem + "__serialize_ir_", fnStem + "__deserialize_ir_"})
        {
            auto fn = module.lookupSymbol<mlir::func::FuncOp>(name);
            if (!fn)
            {
                return plan.emitOpError("body '" + name + "' was not defined");
            }
            built.push_back(fn);
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
            if (schema->hasAttr("llvmdsdl.layout_only"))
            {
                // Present so that a member of this type can be addressed. Its serialisation is
                // its own object's to define.
                continue;
            }
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
                schema.emitOpError("carries no serialization plan");
                signalPassFailure();
                return;
            }
        }

        // Canonicalise what was built, here rather than downstream. A loop carries values its
        // body may not read and a branch may prove constant; those are dead results while they
        // are still scf, and become variables a target cannot remove once they are not.
        //
        // Applied to the built functions rather than the module. A serialization plan holds a
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
                fn.emitError("failed to canonicalize built plan body");
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
