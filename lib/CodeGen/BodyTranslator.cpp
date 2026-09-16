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

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/STLExtras.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/ADT/StringSet.h>
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
#include <mlir/IR/SymbolTable.h>
#include <mlir/IR/Value.h>
#include <mlir/IR/ValueRange.h>
#include <mlir/Support/LLVM.h>

#include "llvmdsdl/CodeGen/SourceWriter.h"
#include "llvmdsdl/Support/NameCanonicalization.h"
#include "llvmdsdl/Support/NamingPolicy.h"
#include "llvmdsdl/IR/DSDLOps.h"
#include "llvmdsdl/IR/DSDLTypes.h"

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

/// @brief What an operation states about the value it defines.
struct Role final
{
    ValueRole       role{ValueRole::Anonymous};
    llvm::StringRef member;
};

/// @brief The member the nested calls reading @p value agree on, where they agree.
///
/// A size local and a buffer address are built for a single `dsdl.call_serdes` and carry no
/// member of their own; the call they are built for names it. Two calls may come to share one
/// address -- `dsdl.buffer_at` has no memory effect, so CSE may merge two addressing the same
/// offset -- and then no member describes it, since naming it after one would say the other does
/// not reach it. Taking whichever came first would also take whichever the use list happened to
/// present, and that order is a rewriter's to change.
llvm::StringRef memberOfNestedCaller(const mlir::Value value)
{
    std::optional<llvm::StringRef> shared;
    for (mlir::Operation* const user : value.getUsers())
    {
        auto call = mlir::dyn_cast<mlir::dsdl::CallSerdesOp>(user);
        if (!call)
        {
            continue;
        }
        if (!shared.has_value())
        {
            shared = call.getMember();
            continue;
        }
        if (*shared != call.getMember())
        {
            return {};
        }
    }
    return shared.value_or(llvm::StringRef{});
}

/// @brief Whether @p pointer addresses a size.
bool addressesSize(const mlir::Value pointer)
{
    const auto type = mlir::dyn_cast<mlir::dsdl::PtrType>(pointer.getType());
    return type && mlir::isa<mlir::dsdl::SizeType>(type.getPointee());
}

/// @brief The role of a helper's answer, from the marker lowering left on the helper.
Role roleOfCall(mlir::func::CallOp call)
{
    static constexpr std::pair<llvm::StringLiteral, ValueRole> Markers[] = {
        {"llvmdsdl.plan_capacity_check", ValueRole::Error},
        {"llvmdsdl.array_length_validate", ValueRole::Error},
        {"llvmdsdl.union_tag_validate", ValueRole::Error},
        {"llvmdsdl.delimiter_header_validate", ValueRole::Error},
        {"llvmdsdl.scalar_float_helper", ValueRole::Scalar},
        {"llvmdsdl.scalar_signed_helper", ValueRole::Scalar},
        {"llvmdsdl.scalar_unsigned_helper", ValueRole::Scalar},
        {"llvmdsdl.array_length_prefix_helper", ValueRole::Length},
        {"llvmdsdl.union_tag_helper", ValueRole::Tag},
    };
    auto* const callee = mlir::SymbolTable::lookupNearestSymbolFrom(call, call.getCalleeAttr());
    if (callee != nullptr)
    {
        for (const auto& [marker, role] : Markers)
        {
            if (callee->hasAttr(marker))
            {
                return {role, {}};
            }
        }
    }
    return {};
}

/// @brief The role `build-dsdl-plan-bodies` stamped on result @p index of @p op, if it did.
///
/// A plan's offset and the error beside it are results of its own shape, which the operations
/// that build them do not state; the pass that does know writes it down as it builds.
Role stampedRole(mlir::Operation* const op, const unsigned index)
{
    const auto roles = op->getAttrOfType<mlir::ArrayAttr>("llvmdsdl.result_roles");
    // Canonicalisation drops a result nothing reads, which moves every result after it. The
    // stamp is one name per result as it was built, so a count that no longer agrees is a stamp
    // that no longer says which result is which, and the yielded values are asked instead.
    if (!roles || (roles.size() != op->getNumResults()) || (index >= roles.size()))
    {
        return {};
    }
    const auto name = mlir::dyn_cast<mlir::StringAttr>(roles[index]);
    if (!name)
    {
        return {};
    }
    if (name.getValue() == "offset")
    {
        return {ValueRole::Offset, {}};
    }
    if (name.getValue() == "error")
    {
        return {ValueRole::Error, {}};
    }
    if (name.getValue() == "rejected")
    {
        return {ValueRole::Rejected, {}};
    }
    return {};
}

/// @brief The values a walk is already inside, so a carry that reaches itself ends it.
///
/// A loop's argument is reached from the value yielded back into it, which is reached from that
/// argument: what has to end is a cycle, not a depth. Stopping at a fixed depth would also stop a
/// chain of nested results that is merely long, and a plan nests as deeply as its type does.
///
/// The values, not the operations that hold them: a loop's result and the argument it forwards
/// belong to one operation and reach each other, and that is the walk doing its work.
/// @brief No value beneath this one reached back onto the path.
constexpr unsigned NoCarryBack = std::numeric_limits<unsigned>::max();

/// @brief One function's role walk: the path being followed, and the values already answered.
///
/// `path` holds the depth at which each value sits on the walk, so a value reached while it is
/// still being answered is a carry reaching itself. `settled` holds the values whose answer does
/// not depend on how they were reached, which is what makes it safe to keep.
struct RoleWalk final
{
    llvm::SmallDenseMap<mlir::Value, unsigned, 16> path;
    llvm::DenseMap<mlir::Value, Role>              settled;
    unsigned                                       depth{0};
};

/// @brief What a value is, and how far back up the walk anything beneath it reached.
///
/// `role` is nothing when the walk is already inside this value. A carry that reaches itself says
/// nothing about the value it returns unchanged, which is not the same as saying the value has no
/// role: the one is an absence of information and the other is information. Answering with
/// `Anonymous` for both would let a self-carry veto the role its initialiser states.
///
/// `carryBack` is the shallowest path depth reached back to from here or below. A value whose
/// subtree reached no further back than itself sits on no cycle: no value that can reach it is
/// reachable from it, so no ancestor of it can ever be skipped while answering it, and its answer
/// is the same however it was reached. Those are the ones worth keeping.
struct Reached final
{
    std::optional<Role> role;
    unsigned            carryBack{NoCarryBack};
};

Reached roleOf(mlir::Value value, RoleWalk& walk);

/// @brief @ref roleOf for a value the walk has just entered.
Reached roleOfReached(mlir::Value value, RoleWalk& walk);

/// @brief The role shared by the values yielded into one result, or nothing they share.
///
/// Two arms agreeing on the role but not on the member give the role alone: a failure that
/// reaches the same variable from two fields is an error either way, and naming it after one
/// of them would say the other cannot arrive there.
Reached roleOfYielded(mlir::ValueRange yielded, RoleWalk& walk)
{
    std::optional<Role> shared;
    unsigned            carryBack = NoCarryBack;
    bool                agreed    = true;
    for (const mlir::Value value : yielded)
    {
        const Reached reached = roleOf(value, walk);
        carryBack             = std::min(carryBack, reached.carryBack);
        if (!agreed)
        {
            continue;  // The answer is settled, but the rest still say how far back they reach.
        }
        if (!reached.role.has_value())
        {
            // The walk is already inside this one: it carries whatever the others say.
            continue;
        }
        const Role role = *reached.role;
        if (role.role == ValueRole::Anonymous)
        {
            // No operation says what this one is, which is not a statement that it has no role.
            // Letting it veto would take a role away from the arm that does state one.
            continue;
        }
        if (!shared.has_value())
        {
            shared = role;
            continue;
        }
        if (shared->role != role.role)
        {
            shared.reset();
            agreed = false;
            continue;
        }
        if (shared->member != role.member)
        {
            shared->member = {};
        }
    }
    return {shared.value_or(Role{}), carryBack};
}

/// @brief The values yielded into result @p index of @p op, over all of its arms.
///
/// The index is a result's, and the verifier ties every list read here to the result list: an
/// `scf.if`'s yield operands, an `scf.while`'s `scf.condition` arguments, an `scf.for`'s
/// initialisers and body yield. So each read is in range, and MLIR's own ranges assert when one
/// is not -- a guard here would answer "no role" for an index from the wrong list instead, which
/// is the defect it would be hiding. An `scf.if` with no else region is the one real option, and
/// that is what the emptiness test is for.
std::vector<mlir::Value> yieldedInto(mlir::Operation* const op, const unsigned index)
{
    std::vector<mlir::Value> out;
    if (auto ifOp = mlir::dyn_cast<mlir::scf::IfOp>(op))
    {
        for (mlir::Region* const region : {&ifOp.getThenRegion(), &ifOp.getElseRegion()})
        {
            if (!region->empty())
            {
                if (auto yield = mlir::dyn_cast<mlir::scf::YieldOp>(region->front().getTerminator()))
                {
                    out.push_back(yield.getOperand(index));
                }
            }
        }
        return out;
    }
    if (auto whileOp = mlir::dyn_cast<mlir::scf::WhileOp>(op))
    {
        if (auto condition = mlir::dyn_cast<mlir::scf::ConditionOp>(whileOp.getBefore().front().getTerminator()))
        {
            out.push_back(condition.getArgs()[index]);
        }
        return out;
    }
    if (auto forOp = mlir::dyn_cast<mlir::scf::ForOp>(op))
    {
        out.push_back(forOp.getInitArgs()[index]);
        if (auto yield = mlir::dyn_cast<mlir::scf::YieldOp>(forOp.getBody()->getTerminator()))
        {
            out.push_back(yield.getOperand(index));
        }
        return out;
    }
    return out;
}

/// @brief Whether @p loop forwards its before-region arguments in order and entire.
///
/// A stamp names results, and a loop's results are what its `scf.condition` forwards. The same
/// stamp describes a before-region argument only where the condition forwards the arguments
/// unchanged, so that position means the same thing in both lists. The plans are built that way;
/// the operation does not require it, and the two lists may differ in length and in order.
bool forwardsArgumentsInOrder(mlir::scf::WhileOp loop)
{
    auto condition = mlir::dyn_cast<mlir::scf::ConditionOp>(loop.getBefore().front().getTerminator());
    if (!condition)
    {
        return false;
    }
    const auto arguments = loop.getBefore().front().getArguments();
    const auto forwarded = condition.getArgs();
    return llvm::equal(arguments, forwarded);
}

/// @brief The values that reach before-region argument @p index of @p loop.
///
/// The initialiser at that position carries the first iteration, the after region's `scf.yield`
/// every one after it. Both are per-argument: asking what every initialiser shares would answer
/// for the offset and the error together, which are the two a loop carries.
///
/// The index is a before-region argument's, and the verifier ties that list to both of these: a
/// loop's initialisers match its before-region arguments, and its after region yields to them. So
/// each read is in range, on the same terms as @ref yieldedInto.
std::vector<mlir::Value> incomingTo(mlir::scf::WhileOp loop, const unsigned index)
{
    std::vector<mlir::Value> out;
    out.push_back(loop.getInits()[index]);
    if (auto yield = mlir::dyn_cast<mlir::scf::YieldOp>(loop.getAfter().front().getTerminator()))
    {
        out.push_back(yield.getOperand(index));
    }
    return out;
}

Reached roleOf(mlir::Value value, RoleWalk& walk)
{
    if (const auto onPath = walk.path.find(value); onPath != walk.path.end())
    {
        return {std::nullopt, onPath->second};
    }
    if (const auto answered = walk.settled.find(value); answered != walk.settled.end())
    {
        return {answered->second, NoCarryBack};
    }

    const unsigned here = walk.depth++;
    walk.path.try_emplace(value, here);
    const Reached reached = roleOfReached(value, walk);
    walk.path.erase(value);
    --walk.depth;

    if (reached.carryBack > here)
    {
        walk.settled.try_emplace(value, reached.role.value_or(Role{}));
    }
    return reached;
}

Reached roleOfReached(mlir::Value value, RoleWalk& walk)
{
    if (const auto argument = mlir::dyn_cast<mlir::BlockArgument>(value))
    {
        mlir::Operation* const owner = argument.getOwner()->getParentOp();
        if (auto forOp = mlir::dyn_cast_or_null<mlir::scf::ForOp>(owner))
        {
            if (argument == forOp.getInductionVar())
            {
                return Reached{Role{ValueRole::Index, {}}};
            }
            return roleOf(forOp.getResult(argument.getArgNumber() - 1), walk);
        }
        if (auto whileOp = mlir::dyn_cast_or_null<mlir::scf::WhileOp>(owner))
        {
            // Both regions answer this operation as their parent, and their arguments index
            // different lists: a before-region argument is an initialiser's position, an
            // after-region argument a result's. A stamp names results, so it describes the one
            // outright and the other only where the condition forwards its arguments unchanged.
            const bool before = argument.getOwner() == &whileOp.getBefore().front();
            const Role stamped =
                (!before || forwardsArgumentsInOrder(whileOp)) ? stampedRole(whileOp, argument.getArgNumber()) : Role{};
            if (stamped.role != ValueRole::Anonymous)
            {
                return Reached{stamped};
            }
            return before ? roleOfYielded(incomingTo(whileOp, argument.getArgNumber()), walk)
                          : roleOfYielded(yieldedInto(whileOp, argument.getArgNumber()), walk);
        }
        return Reached{Role{}};
    }
    const auto             result = mlir::cast<mlir::OpResult>(value);
    mlir::Operation* const op     = result.getOwner();
    return llvm::TypeSwitch<mlir::Operation*, Reached>(op)
        .Case<mlir::dsdl::MemberAddrOp>([](auto read) { return Reached{Role{ValueRole::Object, read.getMember()}}; })
        .Case<mlir::dsdl::ElementAddrOp>([](auto read) { return Reached{Role{ValueRole::Object, read.getMember()}}; })
        .Case<mlir::dsdl::LoadMemberOp>([](auto read) { return Reached{Role{ValueRole::Scalar, read.getMember()}}; })
        .Case<mlir::dsdl::LoadElementOp>([](auto read) { return Reached{Role{ValueRole::Scalar, read.getMember()}}; })
        .Case<mlir::dsdl::ArrayLengthOp>([](auto read) { return Reached{Role{ValueRole::Length, read.getMember()}}; })
        .Case<mlir::dsdl::CallSerdesOp>([](auto call) { return Reached{Role{ValueRole::Error, call.getMember()}}; })
        .Case<mlir::dsdl::UnionTagOp>([](auto) { return Reached{Role{ValueRole::Tag, {}}}; })
        .Case<mlir::dsdl::WriteBitsOp>([](auto) { return Reached{Role{ValueRole::Error, {}}}; })
        .Case<mlir::dsdl::ReadBitsOp>([](auto) { return Reached{Role{ValueRole::Scalar, {}}}; })
        .Case<mlir::dsdl::IsNullOp>([](auto) { return Reached{Role{ValueRole::Null, {}}}; })
        .Case<mlir::dsdl::IndexHoldsOp>([](auto) { return Reached{Role{ValueRole::IndexHolds, {}}}; })
        .Case<mlir::dsdl::LocalOp>([&](auto) { return Reached{Role{ValueRole::Size, memberOfNestedCaller(result)}}; })
        .Case<mlir::dsdl::BufferAtOp>(
            [&](auto) { return Reached{Role{ValueRole::Buffer, memberOfNestedCaller(result)}}; })
        .Case<mlir::dsdl::BufferOrEmptyOp>([](auto) { return Reached{Role{ValueRole::Buffer, {}}}; })
        .Case<mlir::dsdl::LoadScalarOp>([&](auto read) {
            if (!addressesSize(read.getPointer()))
            {
                return Reached{Role{ValueRole::Scalar, {}}};
            }
            // Reading back the size a nested call wrote is that member's size, and the local it
            // was written to is the operation that names the member.
            const Reached pointer = roleOf(read.getPointer(), walk);
            return Reached{Role{ValueRole::Size, pointer.role.value_or(Role{}).member}, pointer.carryBack};
        })
        .Case<mlir::func::CallOp>([](auto call) { return Reached{roleOfCall(call)}; })
        .Case<mlir::scf::IfOp, mlir::scf::WhileOp, mlir::scf::ForOp>([&](auto) {
            const Role stamped = stampedRole(op, result.getResultNumber());
            if (stamped.role != ValueRole::Anonymous)
            {
                return Reached{stamped};
            }
            return roleOfYielded(yieldedInto(op, result.getResultNumber()), walk);
        })
        .Default([](mlir::Operation*) { return Reached{Role{}}; });
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
        for (const llvm::StringRef reserved : spelling_.reservedLocals())
        {
            taken_.insert(reserved);
        }
        for (const auto& [argument, name] : llvm::zip(fn.getArguments(), parameters))
        {
            names_[argument] = name;
            taken_.insert(name);
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
        std::string name = "v" + std::to_string(counter_++);
        while (!taken_.insert(name).second)
        {
            name = "v" + std::to_string(counter_++);
        }
        return name;
    }

    /// @brief The name @p value is declared under: what its operation says it is, or a fresh one.
    ///
    /// The spelling is asked with a rising ordinal until it answers with a name the function has
    /// not used, so a repeated role is distinguished in the spelling's own style.
    ///
    /// `NamingScope` claims names from a pool this way too, and would be the one to use if the
    /// suffix were the same everywhere. It is not: it appends `_2` for every language, where Go
    /// and TypeScript want `err2`. Reaching for it would need a camel projection and a camel
    /// suffix in the policy first, and its `LocalName` row moved to them.
    std::string nameFor(const mlir::Value value)
    {
        const Role role = roleOf(value, walk_).role.value_or(Role{});
        if (role.role != ValueRole::Anonymous)
        {
            for (std::size_t ordinal = 0; ordinal < MaxNameOrdinals; ++ordinal)
            {
                std::string candidate = spelling_.valueName(role.role, role.member, ordinal);
                if (candidate.empty())
                {
                    break;
                }
                if (taken_.insert(candidate).second)
                {
                    return candidate;
                }
            }
        }
        return fresh();
    }

    /// @brief Declares a variable for @p value that a structured operation's arms assign.
    std::string variable(const mlir::Value value, const bool reassigned)
    {
        const std::string name = nameFor(value);
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
        const std::string name = nameFor(result);
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
        const std::string induction  = nameFor(op.getInductionVar());
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

    void binary(mlir::Operation* op, const BinaryOperator kind)
    {
        const mlir::Value result = op->getResult(0);
        define(result,
               spelling_.binary(kind, (*this)(op->getOperand(0)), (*this)(op->getOperand(1)), result.getType()),
               true);
    }

    void conversion(mlir::Operation* op, const Conversion kind)
    {
        const mlir::Value result = op->getResult(0);
        const mlir::Value source = op->getOperand(0);
        define(result, spelling_.convert(kind, (*this)(source), source.getType(), result.getType()), true);
    }

    llvm::Error translate(mlir::Operation*            op,
                          llvm::ArrayRef<std::string> yieldTargets,
                          llvm::ArrayRef<std::string> conditionTargets)
    {
        // Every arm answers `void`, so an arm that tries to answer with its error does not
        // compile. Answering with an `llvm::Error` instead would build a
        // `std::optional<llvm::Error>` inside the switch, which GCC cannot prove initialised
        // once the arms inline into one another.
        //
        // An arm that can fail joins its error onto this one rather than assigning over it: a
        // success is unchecked, `Error::operator=` requires a checked left side, and joining
        // moves out of it first, which is what marks it checked.
        llvm::Error outcome = llvm::Error::success();
        llvm::TypeSwitch<mlir::Operation*, void>(op)
            // Values the plan computes.
            .Case<mlir::arith::ConstantOp>([&](mlir::arith::ConstantOp constant) -> void {
                names_[constant.getResult()] = spelling_.constant(mlir::cast<mlir::TypedAttr>(constant.getValue()));
            })
            .Case<mlir::arith::AddIOp>([&](auto) -> void { binary(op, BinaryOperator::Add); })
            .Case<mlir::arith::SubIOp>([&](auto) -> void { binary(op, BinaryOperator::Sub); })
            .Case<mlir::arith::MulIOp>([&](auto) -> void { binary(op, BinaryOperator::Mul); })
            .Case<mlir::arith::DivUIOp>([&](auto) -> void { binary(op, BinaryOperator::DivU); })
            .Case<mlir::arith::DivSIOp>([&](auto) -> void { binary(op, BinaryOperator::DivS); })
            .Case<mlir::arith::RemUIOp>([&](auto) -> void { binary(op, BinaryOperator::RemU); })
            .Case<mlir::arith::RemSIOp>([&](auto) -> void { binary(op, BinaryOperator::RemS); })
            .Case<mlir::arith::AndIOp>([&](auto) -> void { binary(op, BinaryOperator::And); })
            .Case<mlir::arith::OrIOp>([&](auto) -> void { binary(op, BinaryOperator::Or); })
            .Case<mlir::arith::XOrIOp>([&](auto) -> void { binary(op, BinaryOperator::Xor); })
            .Case<mlir::arith::ShLIOp>([&](auto) -> void { binary(op, BinaryOperator::ShiftLeft); })
            .Case<mlir::arith::ShRUIOp>([&](auto) -> void { binary(op, BinaryOperator::ShiftRightU); })
            .Case<mlir::arith::ShRSIOp>([&](auto) -> void { binary(op, BinaryOperator::ShiftRightS); })
            .Case<mlir::arith::CmpIOp>([&](mlir::arith::CmpIOp compare) -> void {
                define(compare.getResult(),
                       spelling_.compare(comparisonOf(compare.getPredicate()),
                                         (*this)(compare.getLhs()),
                                         (*this)(compare.getRhs()),
                                         compare.getLhs().getType()),
                       true);
            })
            .Case<mlir::arith::SelectOp>([&](mlir::arith::SelectOp select) -> void {
                if (!select.getResult().use_empty())
                {
                    const std::string name = nameFor(select.getResult());
                    spelling_.declareSelect(w_,
                                            select.getType(),
                                            name,
                                            (*this)(select.getCondition()),
                                            (*this)(select.getTrueValue()),
                                            (*this)(select.getFalseValue()));
                    names_[select.getResult()] = name;
                }
            })
            .Case<mlir::arith::ExtUIOp>([&](auto) -> void { conversion(op, Conversion::ZeroExtend); })
            .Case<mlir::arith::ExtSIOp>([&](auto) -> void { conversion(op, Conversion::SignExtend); })
            .Case<mlir::arith::TruncIOp>([&](auto) -> void { conversion(op, Conversion::Truncate); })
            .Case<mlir::arith::IndexCastOp>([&](auto) -> void { conversion(op, Conversion::IndexCast); })
            // Calls and returns.
            .Case<mlir::func::CallOp>([&](mlir::func::CallOp call) -> void {
                if (call.getNumResults() != 1)
                {
                    outcome = llvm::joinErrors(std::move(outcome),
                                               llvm::createStringError(llvm::inconvertibleErrorCode(),
                                                                       "call to %s has %u results; the translator "
                                                                       "spells one",
                                                                       call.getCallee().str().c_str(),
                                                                       call.getNumResults()));
                    return;
                }
                define(call.getResult(0),
                       spelling_.call(spelling_.functionName(call.getCallee()), operands(op)),
                       false);
            })
            .Case<mlir::func::ReturnOp>([&](mlir::func::ReturnOp ret) -> void {
                if (ret.getNumOperands() != 1)
                {
                    outcome = llvm::joinErrors(std::move(outcome),
                                               llvm::createStringError(llvm::inconvertibleErrorCode(),
                                                                       "return with %u operands; the translator "
                                                                       "spells one",
                                                                       ret.getNumOperands()));
                    return;
                }
                spelling_.returnValue(w_, (*this)(ret.getOperand(0)));
            })
            // Structure.
            .Case<mlir::scf::IfOp>(
                [&](mlir::scf::IfOp ifOp) -> void { outcome = llvm::joinErrors(std::move(outcome), structured(ifOp)); })
            .Case<mlir::scf::WhileOp>([&](mlir::scf::WhileOp whileOp) -> void {
                outcome = llvm::joinErrors(std::move(outcome), structured(whileOp));
            })
            .Case<mlir::scf::ForOp>([&](mlir::scf::ForOp forOp) -> void {
                outcome = llvm::joinErrors(std::move(outcome), structured(forOp));
            })
            .Case<mlir::scf::YieldOp>([&](mlir::scf::YieldOp yield) -> void {
                for (const auto& [target, value] : llvm::zip(yieldTargets, yield.getOperands()))
                {
                    spelling_.assign(w_, target, (*this)(value));
                }
            })
            .Case<mlir::scf::ConditionOp>([&](mlir::scf::ConditionOp condition) -> void {
                for (const auto& [target, value] : llvm::zip(conditionTargets, condition.getArgs()))
                {
                    spelling_.assign(w_, target, (*this)(value));
                }
                spelling_.breakUnless(w_, (*this)(condition.getCondition()));
            })
            // The dialect: reads.
            .Case<mlir::dsdl::IsNullOp>([&](mlir::dsdl::IsNullOp read) -> void {
                define(read.getResult(), spelling_.isNull(read, *this), true);
            })
            .Case<mlir::dsdl::IndexHoldsOp>([&](mlir::dsdl::IndexHoldsOp test) -> void {
                define(test.getHolds(), spelling_.indexHolds(test, *this), true);
            })
            .Case<mlir::dsdl::BufferOrEmptyOp>([&](mlir::dsdl::BufferOrEmptyOp read) -> void {
                define(read.getResult(), spelling_.bufferOrEmpty(read, *this), true);
            })
            .Case<mlir::dsdl::BufferAtOp>([&](mlir::dsdl::BufferAtOp read) -> void {
                define(read.getResult(), spelling_.bufferAt(read, *this), true);
            })
            .Case<mlir::dsdl::LoadScalarOp>([&](mlir::dsdl::LoadScalarOp read) -> void {
                define(read.getResult(), spelling_.loadScalar(read, *this), true);
            })
            .Case<mlir::dsdl::LoadMemberOp>([&](mlir::dsdl::LoadMemberOp read) -> void {
                define(read.getResult(), spelling_.loadMember(read, *this), true);
            })
            .Case<mlir::dsdl::LoadElementOp>([&](mlir::dsdl::LoadElementOp read) -> void {
                define(read.getResult(), spelling_.loadElement(read, *this), true);
            })
            .Case<mlir::dsdl::MemberAddrOp>([&](mlir::dsdl::MemberAddrOp read) -> void {
                define(read.getResult(), spelling_.memberAddr(read, *this), true);
            })
            .Case<mlir::dsdl::ElementAddrOp>([&](mlir::dsdl::ElementAddrOp read) -> void {
                define(read.getResult(), spelling_.elementAddr(read, *this), true);
            })
            .Case<mlir::dsdl::ArrayLengthOp>([&](mlir::dsdl::ArrayLengthOp read) -> void {
                define(read.getResult(), spelling_.arrayLength(read, *this), true);
            })
            .Case<mlir::dsdl::UnionTagOp>([&](mlir::dsdl::UnionTagOp read) -> void {
                define(read.getResult(), spelling_.unionTag(read, *this), true);
            })
            .Case<mlir::dsdl::ReadBitsOp>([&](mlir::dsdl::ReadBitsOp read) -> void {
                define(read.getResult(), spelling_.readBits(read, *this), true);
            })
            .Case<mlir::dsdl::LocalOp>([&](mlir::dsdl::LocalOp local) -> void {
                names_[local.getResult()] = spelling_.local(w_, local, nameFor(local.getResult()), *this);
            })
            // The dialect: writes and calls.
            .Case<mlir::dsdl::WriteBitsOp>([&](mlir::dsdl::WriteBitsOp write) -> void {
                define(write.getResult(), spelling_.writeBits(write, *this), false);
            })
            .Case<mlir::dsdl::CallSerdesOp>([&](mlir::dsdl::CallSerdesOp call) -> void {
                const std::string name = call.getResult().use_empty() ? std::string{} : nameFor(call.getResult());
                spelling_.declareCallSerdes(w_, name, call, *this);
                if (!name.empty())
                {
                    names_[call.getResult()] = name;
                }
            })
            .Case<mlir::dsdl::StoreScalarOp>(
                [&](mlir::dsdl::StoreScalarOp write) -> void { spelling_.storeScalar(w_, write, *this); })
            .Case<mlir::dsdl::StoreMemberOp>(
                [&](mlir::dsdl::StoreMemberOp write) -> void { spelling_.storeMember(w_, write, *this); })
            .Case<mlir::dsdl::StoreElementOp>(
                [&](mlir::dsdl::StoreElementOp write) -> void { spelling_.storeElement(w_, write, *this); })
            .Case<mlir::dsdl::SetArrayLengthOp>(
                [&](mlir::dsdl::SetArrayLengthOp write) -> void { spelling_.setArrayLength(w_, write, *this); })
            .Case<mlir::dsdl::SetUnionTagOp>(
                [&](mlir::dsdl::SetUnionTagOp write) -> void { spelling_.setUnionTag(w_, write, *this); })
            .Case<mlir::dsdl::BitWriteOp>(
                [&](mlir::dsdl::BitWriteOp write) -> void { spelling_.bitWrite(w_, write, *this); })
            .Case<mlir::dsdl::BitReadOp>(
                [&](mlir::dsdl::BitReadOp read) -> void { spelling_.bitRead(w_, read, *this); })
            .Default([&](mlir::Operation* other) -> void {
                outcome = llvm::joinErrors(std::move(outcome),
                                           llvm::createStringError(llvm::inconvertibleErrorCode(),
                                                                   "no spelling for '%s' in a plan body",
                                                                   other->getName().getStringRef().str().c_str()));
            });
        return outcome;
    }

    /// @brief How many spellings of one role a function asks for before naming the value itself.
    static constexpr std::size_t MaxNameOrdinals = 1024;

    const BodySpelling&                      spelling_;
    SourceWriter&                            w_;
    llvm::DenseMap<mlir::Value, std::string> names_;
    RoleWalk                                 walk_;
    llvm::StringSet<>                        taken_;
    std::size_t                              counter_{0};
};

}  // namespace

namespace
{

/// @brief The word a role is named by, in the snake-cased languages.
llvm::StringRef roleWord(const ValueRole role)
{
    switch (role)
    {
    case ValueRole::Offset:
        return "offset";
    case ValueRole::Object:
        return "addr";
    case ValueRole::Buffer:
        return "buf";
    case ValueRole::Size:
        return "size";
    case ValueRole::Length:
        return "count";
    case ValueRole::Tag:
        return "tag";
    case ValueRole::Scalar:
        return "value";
    case ValueRole::Error:
        return "err";
    case ValueRole::Null:
        return "is_null";
    case ValueRole::Rejected:
        return "rejected";
    case ValueRole::IndexHolds:
        return "index_holds";
    case ValueRole::Index:
        return "i";
    case ValueRole::Anonymous:
        break;
    }
    return {};
}

/// @brief @p member and @p word joined as one snake_case name.
///
/// The member is folded by `canonicalSnakeCase`, the projection the frontend's collision check
/// and the naming policy already share, so `fooBar` and `FOO_BAR` reach one spelling however the
/// DSDL author cased them, and the two renderings of a name differ in case alone. What a language
/// calls the field itself is its own policy's answer and not always this one: C++ and Rust keep
/// the member's spelling, Go makes it Pascal. The fold also settles the underscores: a member may
/// lead or trail with one, and a target may already have escaped a reserved word by trailing one,
/// so joining either to a role word would double it, which C++ reserves.
std::string joinSnake(const llvm::StringRef member, const llvm::StringRef word)
{
    // The fold drops a leading underscore itself; a trailing one it keeps, and joining that to a
    // role word would double it, which C++ reserves.
    const std::string     folded  = canonicalSnakeCase(member);
    const llvm::StringRef trimmed = llvm::StringRef(folded).rtrim('_');
    return trimmed.empty() ? word.str() : (trimmed.str() + "_" + word.str());
}

/// @brief @p text with each underscore-separated word after the first capitalised.
std::string camelTail(const llvm::StringRef text)
{
    std::string out;
    out.reserve(text.size());
    bool capitalise = false;
    for (const char c : text)
    {
        if (c == '_')
        {
            capitalise = true;
            continue;
        }
        out.push_back(capitalise ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : c);
        capitalise = false;
    }
    return out;
}

}  // namespace

std::string snakeValueName(const ValueRole role, const llvm::StringRef member, const std::size_t ordinal)
{
    const llvm::StringRef word = roleWord(role);
    if (word.empty())
    {
        return {};
    }
    std::string name = escapeIdentifierStart(joinSnake(member, word));
    if (ordinal > 0)
    {
        name += "_" + std::to_string(ordinal + 1);
    }
    return name;
}

std::string camelValueName(const ValueRole role, const llvm::StringRef member, const std::size_t ordinal)
{
    const llvm::StringRef word = roleWord(role);
    if (word.empty())
    {
        return {};
    }
    // The same snake_case fold, then the camel spelling of it, so the two renderings differ in
    // case alone and a member reaches both the way it reaches its field.
    std::string name = escapeIdentifierStart(camelTail(joinSnake(member, word)));
    if (ordinal > 0)
    {
        name += std::to_string(ordinal + 1);
    }
    return name;
}

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
