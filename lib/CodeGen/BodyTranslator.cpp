//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// The structure of a plan-body translation, shared by every language.
///
/// Each value gets a name when it is defined and is read by that name afterwards; a constant
/// and an address the spelling forms inline are read as their expression instead. A structured
/// operation's results are variables declared ahead of it and assigned by its arms, which is
/// what `scf.yield` becomes. A pure operation nothing reads is not spelled at all.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/CodeGen/BodyTranslator.h"

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/STLExtras.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/ADT/TypeSwitch.h>
#include <llvm/Support/Error.h>
#include <mlir/Dialect/Arith/IR/Arith.h>
#include <mlir/Dialect/Func/IR/FuncOps.h>
#include <mlir/Dialect/SCF/IR/SCF.h>
#include <mlir/IR/Attributes.h>
#include <mlir/IR/BuiltinAttributeInterfaces.h>
#include <mlir/IR/Block.h>
#include <mlir/IR/BuiltinAttributes.h>
#include <mlir/IR/BuiltinOps.h>
#include <mlir/IR/Operation.h>
#include <mlir/IR/Region.h>
#include <mlir/IR/Value.h>
#include <mlir/IR/ValueRange.h>
#include <mlir/Support/LLVM.h>

#include "llvmdsdl/CodeGen/SourceWriter.h"
#include "llvmdsdl/IR/DSDLOps.h"

namespace llvmdsdl
{
namespace
{

Comparison comparisonOf(const mlir::arith::CmpIPredicate predicate)
{
    switch (predicate)
    {
    case mlir::arith::CmpIPredicate::eq:
        return Comparison::Eq;
    case mlir::arith::CmpIPredicate::ne:
        return Comparison::Ne;
    case mlir::arith::CmpIPredicate::slt:
        return Comparison::LtS;
    case mlir::arith::CmpIPredicate::sle:
        return Comparison::LeS;
    case mlir::arith::CmpIPredicate::sgt:
        return Comparison::GtS;
    case mlir::arith::CmpIPredicate::sge:
        return Comparison::GeS;
    case mlir::arith::CmpIPredicate::ult:
        return Comparison::LtU;
    case mlir::arith::CmpIPredicate::ule:
        return Comparison::LeU;
    case mlir::arith::CmpIPredicate::ugt:
        return Comparison::GtU;
    case mlir::arith::CmpIPredicate::uge:
        return Comparison::GeU;
    }
    return Comparison::Eq;
}

/// @brief One function's translation: the names of its values and the walk over its blocks.
class Translator final : public ValueNames
{
public:
    Translator(const BodySpelling& spelling, SourceWriter& w)
        : spelling_(spelling)
        , w_(w)
    {
    }

    std::string operator()(const mlir::Value value) const override
    {
        const auto found = names_.find(value);
        return (found == names_.end()) ? std::string{"<unnamed>"} : found->second;
    }

    llvm::Error run(mlir::func::FuncOp fn)
    {
        const std::vector<std::string> parameters = spelling_.openFunction(w_, fn);
        if (parameters.size() != fn.getNumArguments())
        {
            return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                           "spelling named %zu parameters for %u arguments of %s",
                                           parameters.size(),
                                           fn.getNumArguments(),
                                           fn.getSymName().str().c_str());
        }
        for (const auto& [argument, name] : llvm::zip(fn.getArguments(), parameters))
        {
            names_[argument] = name;
        }
        if (auto err = block(fn.getBody().front(), {}, {}))
        {
            return err;
        }
        spelling_.closeFunction(w_, fn);
        return llvm::Error::success();
    }

private:
    std::string fresh()
    {
        return "v" + std::to_string(counter_++);
    }

    /// @brief Declares a variable for @p value that a structured operation's arms assign.
    std::string variable(const mlir::Value value, const bool reassigned)
    {
        const std::string name = fresh();
        spelling_.declareVariable(w_, value.getType(), name, reassigned);
        names_[value] = name;
        return name;
    }

    /// @brief Defines @p result as @p expr: declared under a name, or read as the expression.
    ///
    /// A pure result nothing reads is dropped; an effectful one is evaluated and discarded.
    void define(const mlir::Value result, std::string expr, const bool pure)
    {
        if (spelling_.spellsInline(result.getDefiningOp()))
        {
            names_[result] = std::move(expr);
            return;
        }
        if (result.use_empty())
        {
            if (!pure)
            {
                spelling_.discard(w_, expr);
            }
            return;
        }
        const std::string name = fresh();
        spelling_.declare(w_, result.getType(), name, expr);
        names_[result] = name;
    }

    std::vector<std::string> operands(mlir::Operation* op) const
    {
        std::vector<std::string> out;
        out.reserve(op->getNumOperands());
        for (const mlir::Value operand : op->getOperands())
        {
            out.push_back((*this)(operand));
        }
        return out;
    }

    /// @brief Translates @p b. Its `scf.yield` assigns @p yieldTargets; an `scf.condition`
    ///        assigns @p conditionTargets and leaves the loop when its condition fails.
    llvm::Error block(mlir::Block&                b,
                      llvm::ArrayRef<std::string> yieldTargets,
                      llvm::ArrayRef<std::string> conditionTargets)
    {
        for (mlir::Operation& op : b)
        {
            if (auto err = translate(&op, yieldTargets, conditionTargets))
            {
                return err;
            }
        }
        return llvm::Error::success();
    }

    llvm::Error structured(mlir::scf::IfOp op)
    {
        std::vector<std::string> results;
        results.reserve(op.getNumResults());
        for (const mlir::Value result : op.getResults())
        {
            results.push_back(variable(result, false));
        }
        spelling_.openIf(w_, (*this)(op.getCondition()));
        if (auto err = block(op.getThenRegion().front(), results, {}))
        {
            return err;
        }
        if (!op.getElseRegion().empty())
        {
            spelling_.openElse(w_);
            if (auto err = block(op.getElseRegion().front(), results, {}))
            {
                return err;
            }
        }
        spelling_.closeBlock(w_);
        discardUnused(op.getResults());
        return llvm::Error::success();
    }

    llvm::Error structured(mlir::scf::WhileOp op)
    {
        // The loop-carried values are variables the after region reassigns; the results are
        // variables the condition assigns, which the after region then reads as its arguments.
        std::vector<std::string> carried;
        for (const auto& [argument, init] : llvm::zip(op.getBeforeArguments(), op.getInits()))
        {
            carried.push_back(variable(argument, true));
            spelling_.assign(w_, carried.back(), (*this)(init));
        }
        std::vector<std::string> results;
        for (const auto& [result, argument] : llvm::zip(op.getResults(), op.getAfterArguments()))
        {
            results.push_back(variable(result, true));
            names_[argument] = results.back();
        }
        spelling_.openLoop(w_);
        if (auto err = block(op.getBefore().front(), {}, results))
        {
            return err;
        }
        if (auto err = block(op.getAfter().front(), carried, {}))
        {
            return err;
        }
        spelling_.closeBlock(w_);
        discardUnused(op.getResults());
        return llvm::Error::success();
    }

    llvm::Error structured(mlir::scf::ForOp op)
    {
        // A result is the final value of the variable its iteration argument was carried in.
        std::vector<std::string> carried;
        for (const auto& [argument, init, result] :
             llvm::zip(op.getRegionIterArgs(), op.getInitArgs(), op.getResults()))
        {
            carried.push_back(variable(argument, true));
            spelling_.assign(w_, carried.back(), (*this)(init));
            names_[result] = carried.back();
        }
        const std::string induction  = fresh();
        names_[op.getInductionVar()] = induction;
        spelling_.openFor(w_,
                          induction,
                          (*this)(op.getLowerBound()),
                          (*this)(op.getUpperBound()),
                          (*this)(op.getStep()));
        if (auto err = block(*op.getBody(), carried, {}))
        {
            return err;
        }
        spelling_.closeBlock(w_);
        discardUnused(op.getResults());
        return llvm::Error::success();
    }

    /// @brief Marks the variables of @p results the function never reads.
    void discardUnused(mlir::ResultRange results)
    {
        for (const mlir::Value result : results)
        {
            if (result.use_empty())
            {
                spelling_.discard(w_, (*this)(result));
            }
        }
    }

    llvm::Error binary(mlir::Operation* op, const BinaryOperator kind)
    {
        const mlir::Value result = op->getResult(0);
        define(result,
               spelling_.binary(kind, (*this)(op->getOperand(0)), (*this)(op->getOperand(1)), result.getType()),
               true);
        return llvm::Error::success();
    }

    llvm::Error conversion(mlir::Operation* op, const Conversion kind)
    {
        const mlir::Value result = op->getResult(0);
        const mlir::Value source = op->getOperand(0);
        define(result, spelling_.convert(kind, (*this)(source), source.getType(), result.getType()), true);
        return llvm::Error::success();
    }

    llvm::Error translate(mlir::Operation*            op,
                          llvm::ArrayRef<std::string> yieldTargets,
                          llvm::ArrayRef<std::string> conditionTargets)
    {
        return llvm::TypeSwitch<mlir::Operation*, llvm::Error>(op)
            // Values the plan computes.
            .Case<mlir::arith::ConstantOp>([&](mlir::arith::ConstantOp constant) {
                names_[constant.getResult()] = spelling_.constant(mlir::cast<mlir::TypedAttr>(constant.getValue()));
                return llvm::Error::success();
            })
            .Case<mlir::arith::AddIOp>([&](auto) { return binary(op, BinaryOperator::Add); })
            .Case<mlir::arith::SubIOp>([&](auto) { return binary(op, BinaryOperator::Sub); })
            .Case<mlir::arith::MulIOp>([&](auto) { return binary(op, BinaryOperator::Mul); })
            .Case<mlir::arith::DivUIOp>([&](auto) { return binary(op, BinaryOperator::DivU); })
            .Case<mlir::arith::DivSIOp>([&](auto) { return binary(op, BinaryOperator::DivS); })
            .Case<mlir::arith::RemUIOp>([&](auto) { return binary(op, BinaryOperator::RemU); })
            .Case<mlir::arith::RemSIOp>([&](auto) { return binary(op, BinaryOperator::RemS); })
            .Case<mlir::arith::AndIOp>([&](auto) { return binary(op, BinaryOperator::And); })
            .Case<mlir::arith::OrIOp>([&](auto) { return binary(op, BinaryOperator::Or); })
            .Case<mlir::arith::XOrIOp>([&](auto) { return binary(op, BinaryOperator::Xor); })
            .Case<mlir::arith::ShLIOp>([&](auto) { return binary(op, BinaryOperator::ShiftLeft); })
            .Case<mlir::arith::ShRUIOp>([&](auto) { return binary(op, BinaryOperator::ShiftRightU); })
            .Case<mlir::arith::ShRSIOp>([&](auto) { return binary(op, BinaryOperator::ShiftRightS); })
            .Case<mlir::arith::CmpIOp>([&](mlir::arith::CmpIOp compare) {
                define(compare.getResult(),
                       spelling_.compare(comparisonOf(compare.getPredicate()),
                                         (*this)(compare.getLhs()),
                                         (*this)(compare.getRhs()),
                                         compare.getLhs().getType()),
                       true);
                return llvm::Error::success();
            })
            .Case<mlir::arith::SelectOp>([&](mlir::arith::SelectOp select) {
                define(select.getResult(),
                       spelling_.select((*this)(select.getCondition()),
                                        (*this)(select.getTrueValue()),
                                        (*this)(select.getFalseValue()),
                                        select.getType()),
                       true);
                return llvm::Error::success();
            })
            .Case<mlir::arith::ExtUIOp>([&](auto) { return conversion(op, Conversion::ZeroExtend); })
            .Case<mlir::arith::ExtSIOp>([&](auto) { return conversion(op, Conversion::SignExtend); })
            .Case<mlir::arith::TruncIOp>([&](auto) { return conversion(op, Conversion::Truncate); })
            .Case<mlir::arith::IndexCastOp>([&](auto) { return conversion(op, Conversion::IndexCast); })
            // Calls and returns.
            .Case<mlir::func::CallOp>([&](mlir::func::CallOp call) -> llvm::Error {
                if (call.getNumResults() != 1)
                {
                    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                                   "call to %s has %u results; the translator spells one",
                                                   call.getCallee().str().c_str(),
                                                   call.getNumResults());
                }
                define(call.getResult(0),
                       spelling_.call(spelling_.functionName(call.getCallee()), operands(op)),
                       false);
                return llvm::Error::success();
            })
            .Case<mlir::func::ReturnOp>([&](mlir::func::ReturnOp ret) -> llvm::Error {
                if (ret.getNumOperands() != 1)
                {
                    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                                   "return with %u operands; the translator spells one",
                                                   ret.getNumOperands());
                }
                spelling_.returnValue(w_, (*this)(ret.getOperand(0)));
                return llvm::Error::success();
            })
            // Structure.
            .Case<mlir::scf::IfOp>([&](mlir::scf::IfOp ifOp) { return structured(ifOp); })
            .Case<mlir::scf::WhileOp>([&](mlir::scf::WhileOp whileOp) { return structured(whileOp); })
            .Case<mlir::scf::ForOp>([&](mlir::scf::ForOp forOp) { return structured(forOp); })
            .Case<mlir::scf::YieldOp>([&](mlir::scf::YieldOp yield) {
                for (const auto& [target, value] : llvm::zip(yieldTargets, yield.getOperands()))
                {
                    spelling_.assign(w_, target, (*this)(value));
                }
                return llvm::Error::success();
            })
            .Case<mlir::scf::ConditionOp>([&](mlir::scf::ConditionOp condition) {
                for (const auto& [target, value] : llvm::zip(conditionTargets, condition.getArgs()))
                {
                    spelling_.assign(w_, target, (*this)(value));
                }
                spelling_.breakUnless(w_, (*this)(condition.getCondition()));
                return llvm::Error::success();
            })
            // The dialect: reads.
            .Case<mlir::dsdl::IsNullOp>([&](mlir::dsdl::IsNullOp read) {
                define(read.getResult(), spelling_.isNull(read, *this), true);
                return llvm::Error::success();
            })
            .Case<mlir::dsdl::BufferOrEmptyOp>([&](mlir::dsdl::BufferOrEmptyOp read) {
                define(read.getResult(), spelling_.bufferOrEmpty(read, *this), true);
                return llvm::Error::success();
            })
            .Case<mlir::dsdl::BufferAtOp>([&](mlir::dsdl::BufferAtOp read) {
                define(read.getResult(), spelling_.bufferAt(read, *this), true);
                return llvm::Error::success();
            })
            .Case<mlir::dsdl::LoadScalarOp>([&](mlir::dsdl::LoadScalarOp read) {
                define(read.getResult(), spelling_.loadScalar(read, *this), true);
                return llvm::Error::success();
            })
            .Case<mlir::dsdl::LoadMemberOp>([&](mlir::dsdl::LoadMemberOp read) {
                define(read.getResult(), spelling_.loadMember(read, *this), true);
                return llvm::Error::success();
            })
            .Case<mlir::dsdl::LoadElementOp>([&](mlir::dsdl::LoadElementOp read) {
                define(read.getResult(), spelling_.loadElement(read, *this), true);
                return llvm::Error::success();
            })
            .Case<mlir::dsdl::MemberAddrOp>([&](mlir::dsdl::MemberAddrOp read) {
                define(read.getResult(), spelling_.memberAddr(read, *this), true);
                return llvm::Error::success();
            })
            .Case<mlir::dsdl::ElementAddrOp>([&](mlir::dsdl::ElementAddrOp read) {
                define(read.getResult(), spelling_.elementAddr(read, *this), true);
                return llvm::Error::success();
            })
            .Case<mlir::dsdl::ArrayLengthOp>([&](mlir::dsdl::ArrayLengthOp read) {
                define(read.getResult(), spelling_.arrayLength(read, *this), true);
                return llvm::Error::success();
            })
            .Case<mlir::dsdl::UnionTagOp>([&](mlir::dsdl::UnionTagOp read) {
                define(read.getResult(), spelling_.unionTag(read, *this), true);
                return llvm::Error::success();
            })
            .Case<mlir::dsdl::ReadBitsOp>([&](mlir::dsdl::ReadBitsOp read) {
                define(read.getResult(), spelling_.readBits(read, *this), true);
                return llvm::Error::success();
            })
            .Case<mlir::dsdl::LocalOp>([&](mlir::dsdl::LocalOp local) {
                names_[local.getResult()] = spelling_.local(w_, local, fresh(), *this);
                return llvm::Error::success();
            })
            // The dialect: writes and calls.
            .Case<mlir::dsdl::WriteBitsOp>([&](mlir::dsdl::WriteBitsOp write) {
                define(write.getResult(), spelling_.writeBits(write, *this), false);
                return llvm::Error::success();
            })
            .Case<mlir::dsdl::CallSerdesOp>([&](mlir::dsdl::CallSerdesOp call) {
                define(call.getResult(), spelling_.callSerdes(call, *this), false);
                return llvm::Error::success();
            })
            .Case<mlir::dsdl::StoreScalarOp>([&](mlir::dsdl::StoreScalarOp write) {
                spelling_.storeScalar(w_, write, *this);
                return llvm::Error::success();
            })
            .Case<mlir::dsdl::StoreMemberOp>([&](mlir::dsdl::StoreMemberOp write) {
                spelling_.storeMember(w_, write, *this);
                return llvm::Error::success();
            })
            .Case<mlir::dsdl::StoreElementOp>([&](mlir::dsdl::StoreElementOp write) {
                spelling_.storeElement(w_, write, *this);
                return llvm::Error::success();
            })
            .Case<mlir::dsdl::SetArrayLengthOp>([&](mlir::dsdl::SetArrayLengthOp write) {
                spelling_.setArrayLength(w_, write, *this);
                return llvm::Error::success();
            })
            .Case<mlir::dsdl::SetUnionTagOp>([&](mlir::dsdl::SetUnionTagOp write) {
                spelling_.setUnionTag(w_, write, *this);
                return llvm::Error::success();
            })
            .Case<mlir::dsdl::BitWriteOp>([&](mlir::dsdl::BitWriteOp write) {
                spelling_.bitWrite(w_, write, *this);
                return llvm::Error::success();
            })
            .Case<mlir::dsdl::BitReadOp>([&](mlir::dsdl::BitReadOp read) {
                spelling_.bitRead(w_, read, *this);
                return llvm::Error::success();
            })
            .Default([&](mlir::Operation* other) {
                return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                               "no spelling for '%s' in a plan body",
                                               other->getName().getStringRef().str().c_str());
            });
    }

    const BodySpelling&                      spelling_;
    SourceWriter&                            w_;
    llvm::DenseMap<mlir::Value, std::string> names_;
    std::size_t                              counter_{0};
};

}  // namespace

llvm::Error translateFunction(mlir::func::FuncOp fn, const BodySpelling& spelling, SourceWriter& w)
{
    Translator translator(spelling, w);
    return translator.run(fn);
}

std::vector<mlir::func::FuncOp> schemaFunctions(mlir::ModuleOp module, const llvm::StringRef schemaSym)
{
    std::vector<mlir::func::FuncOp> out;
    for (const mlir::func::FuncOp fn : module.getBodyRegion().front().getOps<mlir::func::FuncOp>())
    {
        const auto owner = fn->getAttrOfType<mlir::StringAttr>("llvmdsdl.schema_sym");
        if (owner && owner.getValue() == schemaSym)
        {
            out.push_back(fn);
        }
    }
    return out;
}

std::optional<llvm::StringRef> planBodyDirection(mlir::func::FuncOp fn)
{
    const auto direction = fn->getAttrOfType<mlir::StringAttr>("llvmdsdl.plan_body");
    if (!direction)
    {
        return std::nullopt;
    }
    return direction.getValue();
}

}  // namespace llvmdsdl
