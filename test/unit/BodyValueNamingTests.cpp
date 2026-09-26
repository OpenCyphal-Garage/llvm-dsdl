//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/Error.h>
#include <mlir/Dialect/Arith/IR/Arith.h>
#include <mlir/Dialect/Func/IR/FuncOps.h>
#include <mlir/Dialect/SCF/IR/SCF.h>
#include <mlir/IR/BuiltinOps.h>
#include <mlir/IR/DialectRegistry.h>
#include <mlir/IR/MLIRContext.h>
#include <mlir/IR/BuiltinAttributeInterfaces.h>
#include <mlir/IR/BuiltinAttributes.h>
#include <mlir/IR/Operation.h>
#include <mlir/IR/OwningOpRef.h>
#include <mlir/IR/Types.h>
#include <mlir/Parser/Parser.h>
#include <mlir/Support/LLVM.h>

#include "llvmdsdl/CodeGen/BodyTranslator.h"
#include "llvmdsdl/CodeGen/SourceWriter.h"
#include "llvmdsdl/Support/NamingPolicy.h"
#include "llvmdsdl/IR/DSDLDialect.h"
#include "llvmdsdl/IR/DSDLOps.h"
#include "llvmdsdl/Support/Language.h"

#include "UnitTests.h"

namespace
{

using llvmdsdl::BodySpelling;
using llvmdsdl::camelValueName;
using llvmdsdl::IndentPolicy;
using llvmdsdl::SourceWriter;
using llvmdsdl::translateFunction;
using llvmdsdl::snakeValueName;
using llvmdsdl::ValueRole;

bool expect(const std::string& actual, const llvm::StringRef expected, const llvm::StringRef what)
{
    if (actual == expected)
    {
        return true;
    }
    std::cerr << what.str() << ": expected '" << expected.str() << "', got '" << actual << "'\n";
    return false;
}

}  // namespace

namespace
{

using llvmdsdl::BinaryOperator;
using llvmdsdl::Comparison;
using llvmdsdl::Conversion;
using llvmdsdl::ValueNames;

/// @brief A spelling whose every value wants the name of a builtin its own bodies would call.
///
/// Everything but the naming answers with nothing: the translation under test declares one value
/// and returns it, and what that declaration reads as does not matter here. @ref reserved is what
/// @ref reservedLocals answers, so one case can run with the pool holding that builtin and one
/// with it empty.
class CollidingSpelling final : public BodySpelling
{
public:
    llvm::SmallVector<llvm::StringRef, 1> reserved;
    mutable std::vector<std::string>      declared;

    /// @brief Whether a name carries the member, or only the role it was inferred from.
    bool withMember{false};

    /// @brief How many candidates the translator has asked for, over every value it has named.
    mutable std::size_t offers{0};

    std::vector<std::string> openFunction(SourceWriter& /*w*/, mlir::func::FuncOp fn) const override
    {
        std::vector<std::string> parameters;
        parameters.reserve(fn.getNumArguments());
        for (unsigned i = 0; i < fn.getNumArguments(); ++i)
        {
            parameters.push_back("p" + std::to_string(i));
        }
        return parameters;
    }

    void declare(SourceWriter& /*w*/,
                 mlir::Type /*type*/,
                 const llvm::StringRef name,
                 llvm::StringRef /*expr*/) const override
    {
        declared.emplace_back(name);
    }

    /// @brief Names a value after its role alone, so a case can tell which role was inferred.
    [[nodiscard]] std::string valueName(const ValueRole       role,
                                        const llvm::StringRef member,
                                        const std::size_t     ordinal) const override
    {
        ++offers;
        return llvmdsdl::snakeValueName(role, withMember ? member : llvm::StringRef{}, ordinal);
    }

    [[nodiscard]] llvm::ArrayRef<llvm::StringRef> reservedLocals() const override
    {
        return reserved;
    }

    void                      closeFunction(SourceWriter& /*w*/, mlir::func::FuncOp /*fn*/) const override {}
    [[nodiscard]] std::string functionName(llvm::StringRef /*callee*/) const override
    {
        return {};
    }
    void declareVariable(SourceWriter& /*w*/,
                         mlir::Type /*type*/,
                         const llvm::StringRef name,
                         bool /*reassigned*/) const override
    {
        declared.emplace_back(name);
    }
    void assign(SourceWriter& /*w*/, llvm::StringRef /*name*/, llvm::StringRef /*expr*/) const override {}
    void discard(SourceWriter& /*w*/, llvm::StringRef /*expr*/) const override {}
    void returnValue(SourceWriter& /*w*/, llvm::StringRef /*expr*/) const override {}
    void openIf(SourceWriter& /*w*/, llvm::StringRef /*condition*/) const override {}
    void openElse(SourceWriter& /*w*/) const override {}
    void openLoop(SourceWriter& /*w*/) const override {}
    void breakUnless(SourceWriter& /*w*/, llvm::StringRef /*condition*/) const override {}
    void openFor(SourceWriter& /*w*/,
                 llvm::StringRef /*variable*/,
                 llvm::StringRef /*lower*/,
                 llvm::StringRef /*upper*/,
                 llvm::StringRef /*step*/) const override
    {
    }
    void                      closeBlock(SourceWriter& /*w*/) const override {}
    [[nodiscard]] std::string constant(mlir::TypedAttr /*value*/) const override
    {
        return {};
    }
    [[nodiscard]] std::string binary(BinaryOperator /*op*/,
                                     llvm::StringRef /*lhs*/,
                                     llvm::StringRef /*rhs*/,
                                     mlir::Type /*type*/) const override
    {
        return {};
    }
    [[nodiscard]] std::string compare(Comparison /*comparison*/,
                                      llvm::StringRef /*lhs*/,
                                      llvm::StringRef /*rhs*/,
                                      mlir::Type /*type*/) const override
    {
        return {};
    }
    [[nodiscard]] std::string select(llvm::StringRef /*condition*/,
                                     llvm::StringRef /*ifTrue*/,
                                     llvm::StringRef /*ifFalse*/,
                                     mlir::Type /*type*/) const override
    {
        return {};
    }
    [[nodiscard]] std::string convert(Conversion /*conversion*/,
                                      llvm::StringRef /*value*/,
                                      mlir::Type /*from*/,
                                      mlir::Type /*to*/) const override
    {
        return {};
    }
    [[nodiscard]] std::string call(llvm::StringRef /*callee*/, llvm::ArrayRef<std::string> /*args*/) const override
    {
        return {};
    }
    [[nodiscard]] bool spellsInline(mlir::Operation* /*op*/) const override
    {
        return false;
    }
    [[nodiscard]] std::string logicalNot(llvm::StringRef /*expr*/) const override
    {
        return {};
    }
    [[nodiscard]] std::string isNull(mlir::dsdl::IsNullOp /*op*/, const ValueNames& /*names*/) const override
    {
        return {};
    }
    [[nodiscard]] std::string isNotNull(mlir::dsdl::IsNullOp /*op*/, const ValueNames& /*names*/) const override
    {
        return {};
    }
    [[nodiscard]] std::string indexHolds(mlir::dsdl::IndexHoldsOp /*op*/, const ValueNames& /*names*/) const override
    {
        return {};
    }
    [[nodiscard]] std::string bufferOrEmpty(mlir::dsdl::BufferOrEmptyOp /*op*/,
                                            const ValueNames& /*names*/) const override
    {
        return {};
    }
    [[nodiscard]] std::string bufferAt(mlir::dsdl::BufferAtOp /*op*/, const ValueNames& /*names*/) const override
    {
        return {};
    }
    [[nodiscard]] std::string loadScalar(mlir::dsdl::LoadScalarOp /*op*/, const ValueNames& /*names*/) const override
    {
        return {};
    }
    void storeScalar(SourceWriter& /*w*/, mlir::dsdl::StoreScalarOp /*op*/, const ValueNames& /*names*/) const override
    {
    }
    [[nodiscard]] std::string local(SourceWriter& /*w*/,
                                    mlir::dsdl::LocalOp /*op*/,
                                    llvm::StringRef /*name*/,
                                    const ValueNames& /*names*/) const override
    {
        return {};
    }
    [[nodiscard]] std::string loadMember(mlir::dsdl::LoadMemberOp /*op*/, const ValueNames& /*names*/) const override
    {
        return {};
    }
    void storeMember(SourceWriter& /*w*/, mlir::dsdl::StoreMemberOp /*op*/, const ValueNames& /*names*/) const override
    {
    }
    [[nodiscard]] std::string loadElement(mlir::dsdl::LoadElementOp /*op*/, const ValueNames& /*names*/) const override
    {
        return {};
    }
    void storeElement(SourceWriter& /*w*/,
                      mlir::dsdl::StoreElementOp /*op*/,
                      const ValueNames& /*names*/) const override
    {
    }
    [[nodiscard]] std::string memberAddr(mlir::dsdl::MemberAddrOp /*op*/, const ValueNames& /*names*/) const override
    {
        return {};
    }
    [[nodiscard]] std::string elementAddr(mlir::dsdl::ElementAddrOp /*op*/, const ValueNames& /*names*/) const override
    {
        return {};
    }
    [[nodiscard]] std::string arrayLength(mlir::dsdl::ArrayLengthOp /*op*/, const ValueNames& /*names*/) const override
    {
        return {};
    }
    void setArrayLength(SourceWriter& /*w*/,
                        mlir::dsdl::SetArrayLengthOp /*op*/,
                        const ValueNames& /*names*/) const override
    {
    }
    [[nodiscard]] std::string unionTag(mlir::dsdl::UnionTagOp /*op*/, const ValueNames& /*names*/) const override
    {
        return {};
    }
    void setUnionTag(SourceWriter& /*w*/, mlir::dsdl::SetUnionTagOp /*op*/, const ValueNames& /*names*/) const override
    {
    }
    [[nodiscard]] std::string writeBits(mlir::dsdl::WriteBitsOp /*op*/, const ValueNames& /*names*/) const override
    {
        return {};
    }
    [[nodiscard]] std::string readBits(mlir::dsdl::ReadBitsOp /*op*/, const ValueNames& /*names*/) const override
    {
        return {};
    }
    void bitWrite(SourceWriter& /*w*/, mlir::dsdl::BitWriteOp /*op*/, const ValueNames& /*names*/) const override {}
    void imageRead(SourceWriter& /*w*/, mlir::dsdl::ImageReadOp /*op*/, const ValueNames& /*names*/) const override {}

    void imageWrite(SourceWriter& /*w*/, mlir::dsdl::ImageWriteOp /*op*/, const ValueNames& /*names*/) const override {}

    [[nodiscard]] std::string viewBytes(mlir::dsdl::LoadViewOp /*op*/, const ValueNames& /*names*/) const override
    {
        return {};
    }
    [[nodiscard]] std::string viewSize(mlir::dsdl::LoadViewOp /*op*/, const ValueNames& /*names*/) const override
    {
        return {};
    }
    void storeView(SourceWriter& /*w*/, mlir::dsdl::StoreViewOp /*op*/, const ValueNames& /*names*/) const override {}
    void clearView(SourceWriter& /*w*/, mlir::dsdl::ClearViewOp /*op*/, const ValueNames& /*names*/) const override {}
    void copyBytes(SourceWriter& /*w*/, mlir::dsdl::CopyBytesOp /*op*/, const ValueNames& /*names*/) const override {}

    void bitRead(SourceWriter& /*w*/, mlir::dsdl::BitReadOp /*op*/, const ValueNames& /*names*/) const override {}
    [[nodiscard]] std::string callSerdes(mlir::dsdl::CallSerdesOp /*op*/, const ValueNames& /*names*/) const override
    {
        return {};
    }
};

/// @brief A body that reads an array's length: one operation that states a role.
constexpr llvm::StringLiteral ArrayLengthBody = R"mlir(
  func.func @body(%arg0: !dsdl.ptr<!dsdl.object<"a.B.1.0">>) -> i64 {
    %0 = dsdl.array_length %arg0 "items" : <!dsdl.object<"a.B.1.0">>
    return %0 : i64
  }
)mlir";

/// @brief The same read, feeding an `scf.if` whose stamp names more results than it has.
///
/// This is what canonicalisation leaves behind: it drops a result nothing reads and carries the
/// attribute onto the operation it rebuilds, so the stamp no longer says which result is which.
/// The role has to come from the value yielded into it instead.
constexpr llvm::StringLiteral StaleStampBody = R"mlir(
  func.func @body(%arg0: !dsdl.ptr<!dsdl.object<"a.B.1.0">>, %arg1: i1) -> i64 {
    %0 = dsdl.array_length %arg0 "items" : <!dsdl.object<"a.B.1.0">>
    %1 = scf.if %arg1 -> (i64) {
      scf.yield %0 : i64
    } else {
      scf.yield %0 : i64
    } {llvmdsdl.result_roles = ["offset", "error"]}
    return %1 : i64
  }
)mlir";

/// @brief A loop whose stamp has outlived its results, carrying a value that states a role.
///
/// An `scf.while` answers with what its `scf.condition` forwards, and that is a before-region
/// argument, which the initialiser carries on the first pass and the after region's `scf.yield` on
/// every one after it. Both have to be asked: taking the initialiser alone would claim a role the
/// carried path may contradict.
constexpr llvm::StringLiteral StaleStampLoop = R"mlir(
  func.func @body(%arg0: !dsdl.ptr<!dsdl.object<"a.B.1.0">>, %arg1: i1) -> i64 {
    %0 = dsdl.array_length %arg0 "items" : <!dsdl.object<"a.B.1.0">>
    %1 = scf.while (%a = %0) : (i64) -> i64 {
      scf.condition(%arg1) %a : i64
    } do {
    ^bb0(%b: i64):
      %2 = dsdl.array_length %arg0 "items" : <!dsdl.object<"a.B.1.0">>
      scf.yield %2 : i64
    } attributes {llvmdsdl.result_roles = ["offset", "error"]}
    return %1 : i64
  }
)mlir";

/// @brief The same loop, carrying a count in and a union tag back.
///
/// The two paths disagree, so the value is one thing on entry and another on every iteration
/// after it, and no role describes it. Asking the initialiser alone would name it after the count.
constexpr llvm::StringLiteral DisagreeingLoop = R"mlir(
  func.func @body(%arg0: !dsdl.ptr<!dsdl.object<"a.B.1.0">>, %arg1: i1) -> i64 {
    %0 = dsdl.array_length %arg0 "items" : <!dsdl.object<"a.B.1.0">>
    %1 = scf.while (%a = %0) : (i64) -> i64 {
      scf.condition(%arg1) %a : i64
    } do {
    ^bb0(%b: i64):
      %2 = dsdl.union_tag %arg0 : <!dsdl.object<"a.B.1.0">>
      scf.yield %2 : i64
    } attributes {llvmdsdl.result_roles = ["offset", "error"]}
    return %1 : i64
  }
)mlir";

/// @brief A counted loop that returns its carried value unchanged, under a stamp gone stale.
///
/// The body yields the iteration argument it was given, so following the carry reaches the value
/// the walk is already inside. That says nothing about it, and taking it for a role of its own
/// would let the carry veto what the initialiser states.
constexpr llvm::StringLiteral SelfCarryingLoop = R"mlir(
  func.func @body(%arg0: !dsdl.ptr<!dsdl.object<"a.B.1.0">>) -> i64 {
    %c0 = arith.constant 0 : index
    %c1 = arith.constant 1 : index
    %c4 = arith.constant 4 : index
    %0 = dsdl.array_length %arg0 "items" : <!dsdl.object<"a.B.1.0">>
    %1 = scf.for %i = %c0 to %c4 step %c1 iter_args(%carried = %0) -> (i64) {
      scf.yield %carried : i64
    } {llvmdsdl.result_roles = ["offset", "error"]}
    return %1 : i64
  }
)mlir";

/// @brief A chain of @p depth guards, each stamped for two results and left holding one.
///
/// Both arms of every guard yield the guard below it, so following the yielded values reaches
/// that guard twice -- and each of those two reaches the one below twice again. A walk that
/// settles nothing therefore costs 2^depth; one that settles a value the first time it answers
/// costs depth. This is the shape `buildUnionOption` emits, once canonicalisation has taken a
/// result away from under the stamps.
std::string guardChain(const unsigned depth)
{
    std::string out = "  func.func @body(%arg0: !dsdl.ptr<!dsdl.object<\"a.B.1.0\">>, %cond: i1) -> i64 {\n"
                      "    %v0 = dsdl.array_length %arg0 \"items\" : <!dsdl.object<\"a.B.1.0\">>\n";
    for (unsigned i = 1; i <= depth; ++i)
    {
        const std::string prev = "%v" + std::to_string(i - 1);
        out.append("    %v").append(std::to_string(i));
        out.append(" = scf.if %cond -> (i64) { scf.yield ").append(prev);
        out.append(" : i64 } else { scf.yield ").append(prev);
        out.append(" : i64 } {llvmdsdl.result_roles = [\"offset\", \"error\"]}\n");
    }
    out.append("    return %v").append(std::to_string(depth)).append(" : i64\n  }\n");
    return out;
}

/// @brief A loop that does not forward its before-region arguments in order.
///
/// Two arguments go in, the condition forwards only the second, and the stamp names the one
/// result it therefore has. A stamp names results, so reading it at a before-region argument's
/// position describes whichever result happens to sit there -- here it would call the array's
/// count an offset. The count is what flows in, and that is what the argument is.
constexpr llvm::StringLiteral SkewedLoop = R"mlir(
  func.func @body(%arg0: !dsdl.ptr<!dsdl.object<"a.B.1.0">>, %cond: i1) -> i64 {
    %count = dsdl.array_length %arg0 "items" : <!dsdl.object<"a.B.1.0">>
    %tag = dsdl.union_tag %arg0 : <!dsdl.object<"a.B.1.0">>
    %r = scf.while (%a = %count, %b = %tag) : (i64, i64) -> i64 {
      scf.condition(%cond) %b : i64
    } do {
    ^bb0(%x: i64):
      scf.yield %count, %tag : i64, i64
    } attributes {llvmdsdl.result_roles = ["offset"]}
    return %r : i64
  }
)mlir";

/// @brief A loop whose condition forwards something other than its argument.
///
/// The count goes in, a union tag comes out, and the after region hands its own argument back.
/// Following the carry therefore reaches that after-region argument, which is fed by the
/// condition and not by the initialiser: the carry is a count on entry and a tag thereafter, so
/// no role describes it. Reading the after-region argument from the initialiser instead would
/// have both paths agree on a count.
constexpr llvm::StringLiteral ForwardedLoop = R"mlir(
  func.func @body(%arg0: !dsdl.ptr<!dsdl.object<"a.B.1.0">>, %cond: i1) -> i64 {
    %count = dsdl.array_length %arg0 "items" : <!dsdl.object<"a.B.1.0">>
    %r = scf.while (%a = %count) : (i64) -> i64 {
      %t = dsdl.union_tag %arg0 : <!dsdl.object<"a.B.1.0">>
      scf.condition(%cond) %t : i64
    } do {
    ^bb0(%x: i64):
      scf.yield %x : i64
    } attributes {llvmdsdl.result_roles = ["offset", "error"]}
    return %r : i64
  }
)mlir";

/// @brief A guard whose stamp has gone stale, over one that still carries the offset.
///
/// The inner guard still names its result. The outer one reaches that offset down one arm and an
/// `arith` value down the other, and no operation says what an `arith` value is. That silence is
/// not a statement that the value has no role, so it must not take the offset away.
constexpr llvm::StringLiteral SilentArm = R"mlir(
  func.func @body(%arg0: !dsdl.ptr<!dsdl.object<"a.B.1.0">>, %cond: i1) -> i64 {
    %zero = arith.constant 0 : i64
    %inner = scf.if %cond -> (i64) {
      scf.yield %zero : i64
    } else {
      scf.yield %zero : i64
    } {llvmdsdl.result_roles = ["offset"]}
    %outer = scf.if %cond -> (i64) {
      scf.yield %inner : i64
    } else {
      scf.yield %zero : i64
    } {llvmdsdl.result_roles = ["offset", "error"]}
    return %outer : i64
  }
)mlir";

/// @brief One buffer address read by two nested calls, which name different members.
///
/// `dsdl.buffer_at` has no memory effect, so CSE may merge two that address the same offset. The
/// address then belongs to both calls and to neither member, and the use list has no order that
/// would prefer one of them.
constexpr llvm::StringLiteral SharedAddress = R"mlir(
  func.func @body(%arg0: !dsdl.ptr<!dsdl.object<"a.B.1.0">>,
                  %buf: !dsdl.ptr<!dsdl.byte>,
                  %size: !dsdl.ptr<!dsdl.size>) -> i8 {
    %four = arith.constant 4 : i64
    %at = dsdl.buffer_at %buf[%four] : <!dsdl.byte> -> <!dsdl.byte>
    %alpha = dsdl.member_addr %arg0 "alpha" : <!dsdl.object<"a.B.1.0">> -> <!dsdl.object<"a.C.1.0">>
    %beta = dsdl.member_addr %arg0 "beta" : <!dsdl.object<"a.B.1.0">> -> <!dsdl.object<"a.C.1.0">>
    %e1 = dsdl.call_serdes @f(%alpha, %at, %size) {direction = "serialize", member = "alpha"} :
        <!dsdl.object<"a.C.1.0">>, <!dsdl.byte>, <!dsdl.size>
    %e2 = dsdl.call_serdes @f(%beta, %at, %size) {direction = "serialize", member = "beta"} :
        <!dsdl.object<"a.C.1.0">>, <!dsdl.byte>, <!dsdl.size>
    return %e1 : i8
  }
)mlir";

/// @brief A body of @p count array lengths, every one of them wanting the name `count`.
///
/// Each takes the next free ordinal, so the names run `count`, `count_2`, `count_3`. Each is also
/// summed into the result, since a value nothing reads is never spelled and so never named. What
/// the case is about is how many candidates the translator has to ask for to reach those names.
std::string sameRoleRun(const unsigned count)
{
    std::string out = "  func.func @body(%arg0: !dsdl.ptr<!dsdl.object<\"a.B.1.0\">>) -> i64 {\n";
    for (unsigned i = 0; i < count; ++i)
    {
        out.append("    %v").append(std::to_string(i));
        out.append(" = dsdl.array_length %arg0 \"items\" : <!dsdl.object<\"a.B.1.0\">>\n");
    }
    for (unsigned i = 1; i < count; ++i)
    {
        out.append("    %s").append(std::to_string(i)).append(" = arith.addi %");
        out.append((i == 1) ? "v0" : ("s" + std::to_string(i - 1)));
        out.append(", %v").append(std::to_string(i)).append(" : i64\n");
    }
    out.append("    return %s").append(std::to_string(count - 1)).append(" : i64\n  }\n");
    return out;
}

/// @brief A body calling two helpers that differ only in the marker the lowering left on them.
///
/// Neither call says what its answer is; the function it resolves to does. Naming both from one
/// module is what says the lookup answers about the callee it found rather than about the module,
/// the first call it was asked, or the name it was asked under.
constexpr llvm::StringLiteral MarkedCalls = R"mlir(
  func.func private @check(i64) -> i8 attributes {llvmdsdl.plan_capacity_check}
  func.func private @scalar(i64) -> i8 attributes {llvmdsdl.scalar_float_helper}
  func.func @body(%arg0: i64) -> i8 {
    %e = func.call @check(%arg0) : (i64) -> i8
    %s = func.call @scalar(%arg0) : (i64) -> i8
    %r = arith.addi %e, %s : i8
    return %r : i8
  }
)mlir";

/// @brief Translates `@body` of @p source and answers the names it declared.
///
/// The lookups are built from the module the body belongs to, which is how a run uses them: once
/// per module, for every function generated from it.
std::optional<std::vector<std::string>> declaredNamesOfBody(const llvm::StringRef source)
{
    mlir::DialectRegistry registry;
    registry
        .insert<mlir::dsdl::DSDLDialect, mlir::func::FuncDialect, mlir::arith::ArithDialect, mlir::scf::SCFDialect>();
    mlir::MLIRContext context(registry);
    context.getOrLoadDialect<mlir::dsdl::DSDLDialect>();

    mlir::OwningOpRef<mlir::ModuleOp> module = mlir::parseSourceString<mlir::ModuleOp>(source, &context);
    if (!module)
    {
        std::cerr << "the call fixture did not parse\n";
        return std::nullopt;
    }
    mlir::func::FuncOp body;
    for (mlir::func::FuncOp fn : module->getBody()->getOps<mlir::func::FuncOp>())
    {
        if (fn.getSymName() == "body")
        {
            body = fn;
        }
    }
    if (!body)
    {
        std::cerr << "the call fixture holds no @body\n";
        return std::nullopt;
    }
    CollidingSpelling         spelling;
    std::ostringstream        out;
    SourceWriter              w(out, IndentPolicy::spaces(2));
    llvmdsdl::PlanBodyLookups lookups(*module);
    if (auto err = translateFunction(body, spelling, w, lookups))
    {
        std::cerr << "translation failed: " << llvm::toString(std::move(err)) << "\n";
        return std::nullopt;
    }
    return spelling.declared;
}

/// @brief Translates @p source through a spelling reserving @p reserved, and answers its names.
///
/// An operation that states a role has the spelling asked for a name rather than being numbered,
/// which is the path both the reserved pool and the stamp's arity check sit on.
std::optional<std::vector<std::string>> declaredNamesFor(const llvm::StringRef                 source,
                                                         const llvm::ArrayRef<llvm::StringRef> reserved,
                                                         const bool                            withMember = false,
                                                         std::size_t* const                    offers     = nullptr)
{
    mlir::DialectRegistry registry;
    registry
        .insert<mlir::dsdl::DSDLDialect, mlir::func::FuncDialect, mlir::arith::ArithDialect, mlir::scf::SCFDialect>();
    mlir::MLIRContext context(registry);
    context.getOrLoadDialect<mlir::dsdl::DSDLDialect>();

    mlir::OwningOpRef<mlir::ModuleOp> module = mlir::parseSourceString<mlir::ModuleOp>(source, &context);
    if (!module)
    {
        std::cerr << "the naming fixture did not parse\n";
        return std::nullopt;
    }
    auto fn = mlir::dyn_cast<mlir::func::FuncOp>(&module->getBody()->front());
    if (!fn)
    {
        std::cerr << "the naming fixture holds no function\n";
        return std::nullopt;
    }

    CollidingSpelling spelling;
    spelling.withMember = withMember;
    spelling.reserved.assign(reserved.begin(), reserved.end());
    std::ostringstream        out;
    SourceWriter              w(out, IndentPolicy::spaces(2));
    llvmdsdl::PlanBodyLookups lookups(*module);
    if (auto err = translateFunction(fn, spelling, w, lookups))
    {
        std::cerr << "translation failed: " << llvm::toString(std::move(err)) << "\n";
        return std::nullopt;
    }
    if (offers != nullptr)
    {
        *offers = spelling.offers;
    }
    return spelling.declared;
}

}  // namespace

bool runBodyValueNamingTests()
{
    bool ok = true;

    // A role with no member is the role's own word; a member carries it.
    ok = expect(snakeValueName(ValueRole::Error, {}, 0), "err", "snake error") && ok;
    ok = expect(snakeValueName(ValueRole::Error, "frequency", 0), "frequency_err", "snake member error") && ok;
    ok = expect(camelValueName(ValueRole::Size, "acoustic_power", 0), "acousticPowerSize", "camel member size") && ok;

    // An ordinal past the first distinguishes a repeat in the style of its language.
    ok = expect(snakeValueName(ValueRole::Error, {}, 1), "err_2", "snake repeat") && ok;
    ok = expect(camelValueName(ValueRole::Error, {}, 2), "err3", "camel repeat") && ok;

    // A member that already trails an underscore -- a target's escape of a reserved word, or a
    // DSDL name in its own right -- must not double it: C++ reserves an identifier containing
    // '__'. A leading one is dropped for the same reason.
    ok = expect(snakeValueName(ValueRole::Scalar, "break_", 0), "break_value", "snake trailing underscore") && ok;
    ok = expect(snakeValueName(ValueRole::Scalar, "_foo", 0), "foo_value", "snake leading underscore") && ok;
    ok = expect(camelValueName(ValueRole::Scalar, "_foo", 0), "fooValue", "camel leading underscore") && ok;

    // A member is folded by the projection the emitters and the frontend's collision check share,
    // so a name that is not already snake_case reaches a local the way it reaches its field, and
    // the two renderings differ in case alone.
    ok = expect(snakeValueName(ValueRole::Scalar, "fooBar", 0), "foo_bar_value", "snake camel member") && ok;
    ok = expect(camelValueName(ValueRole::Scalar, "fooBar", 0), "fooBarValue", "camel camel member") && ok;
    ok = expect(snakeValueName(ValueRole::Scalar, "FOO_BAR", 0), "foo_bar_value", "snake shouting member") && ok;
    ok = expect(camelValueName(ValueRole::Scalar, "FOO_BAR", 0), "fooBarValue", "camel shouting member") && ok;

    // `_9axis` is a legal DSDL name, and the fold drops its underscore: no target accepts an
    // identifier that begins with a digit, so one is put back.
    ok = expect(snakeValueName(ValueRole::Scalar, "_9axis", 0), "_9axis_value", "snake digit-led member") && ok;
    ok = expect(camelValueName(ValueRole::Scalar, "_9axis", 0), "_9axisValue", "camel digit-led member") && ok;

    // An array's count is spelled 'count' rather than 'len' because it reads better and stays
    // free: BodySpelling::reservedLocals claims the builtin a Go or Python body calls, so a role
    // landing on one is bumped rather than capturing the call.
    ok = expect(snakeValueName(ValueRole::Length, {}, 0), "count", "snake length") && ok;
    ok = expect(camelValueName(ValueRole::Length, "name", 0), "nameCount", "camel member length") && ok;

    // The offset a plan threads through its steps: a role no operation states, stamped on the
    // structured results by build-dsdl-plan-bodies.
    ok = expect(snakeValueName(ValueRole::Offset, {}, 0), "offset", "snake offset") && ok;
    ok = expect(snakeValueName(ValueRole::Offset, {}, 1), "offset_2", "snake offset repeat") && ok;
    ok = expect(camelValueName(ValueRole::Offset, {}, 1), "offset2", "camel offset repeat") && ok;

    // A predicate is named for what it asks, which is what the operation is called.
    ok = expect(snakeValueName(ValueRole::Null, {}, 0), "is_null", "snake null") && ok;
    ok = expect(camelValueName(ValueRole::IndexHolds, {}, 0), "indexHolds", "camel index holds") && ok;
    ok = expect(snakeValueName(ValueRole::Rejected, {}, 0), "rejected", "snake rejected") && ok;

    // An operation that says nothing about its result leaves the value to the translator.
    ok = expect(snakeValueName(ValueRole::Anonymous, "frequency", 0), "", "snake anonymous") && ok;
    ok = expect(camelValueName(ValueRole::Anonymous, "frequency", 0), "", "camel anonymous") && ok;

    // The renderings above are half of it. The translator claims each name from a pool holding
    // the function's parameters and the spelling's reserved locals, so a role landing on a
    // builtin the body calls is bumped instead of capturing the call. Drive that.
    const auto unreserved = declaredNamesFor(ArrayLengthBody, {});
    const auto reserved   = declaredNamesFor(ArrayLengthBody, {llvm::StringRef{"count"}});
    if (!unreserved.has_value() || !reserved.has_value())
    {
        return false;
    }
    if ((unreserved->size() != 1) || (reserved->size() != 1))
    {
        std::cerr << "expected one declared value from the array-length body\n";
        return false;
    }
    ok = expect(unreserved->front(), "count", "a name nothing claims is taken as offered") && ok;
    ok = expect(reserved->front(), "count_2", "a name the spelling reserves is bumped") && ok;

    // A stamp that has outlived its results names none of them, so the role comes from the value
    // yielded in. Taking the stamp at face value would name this one after its first entry.
    const auto stale = declaredNamesFor(StaleStampBody, {});
    if (!stale.has_value())
    {
        return false;
    }
    if (stale->size() != 2)
    {
        std::cerr << "expected two declared values from the stale-stamp body\n";
        return false;
    }
    ok = expect(stale->at(1), "count_2", "a stale stamp defers to the value yielded in") && ok;

    // A loop answers with what its condition forwards, which is a before-region argument reached
    // from the initialiser and from the after region's yield. A stale stamp on one must not cost
    // the role those carry.
    const auto loop = declaredNamesFor(StaleStampLoop, {});
    if (!loop.has_value())
    {
        return false;
    }
    if (loop->empty())
    {
        std::cerr << "expected the loop body to declare something\n";
        return false;
    }
    for (const std::string& name : *loop)
    {
        if (!name.starts_with("count"))
        {
            std::cerr << "a loop value fell back to '" << name << "' rather than keeping its role\n";
            ok = false;
        }
    }

    // A loop that returns its carry unchanged says nothing new about it, so the role its
    // initialiser states stands. Reading the self-carry as a role of its own loses it.
    const auto carried = declaredNamesFor(SelfCarryingLoop, {});
    if (!carried.has_value())
    {
        return false;
    }
    for (const std::string& name : *carried)
    {
        if (!name.starts_with("count") && !name.starts_with("i"))
        {
            std::cerr << "a self-carried loop value fell back to '" << name << "'\n";
            ok = false;
        }
    }

    // When the two paths disagree the value is a count on entry and a tag thereafter, so no role
    // describes it and the translator names it itself. Asking the initialiser alone would call it
    // a count on every iteration but the first.
    const auto disagreeing = declaredNamesFor(DisagreeingLoop, {});
    if (!disagreeing.has_value())
    {
        return false;
    }
    const bool carriedIsAnonymous = std::any_of(disagreeing->begin(), disagreeing->end(), [](const std::string& name) {
        return (name.size() > 1) && (name.front() == 'v') && (std::isdigit(name[1]) != 0);
    });
    if (!carriedIsAnonymous)
    {
        std::cerr << "a loop whose paths disagree kept a role it cannot claim\n";
        ok = false;
    }

    // The walk must settle a value it has answered rather than re-derive it down every path
    // that reaches it. At this depth the two costs are 2^25 visits and 25 -- six orders apart --
    // so the budget below detects a walk that has stopped settling, and is not a measurement of
    // one that has not.
    const auto started = std::chrono::steady_clock::now();
    const auto chained = declaredNamesFor(guardChain(25), {});
    const auto elapsed = std::chrono::steady_clock::now() - started;
    if (!chained.has_value())
    {
        return false;
    }
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    if (ms > 2000)
    {
        std::cerr << "the guard chain took " << ms << " ms: the role walk is re-deriving values it has settled\n";
        ok = false;
    }

    // A role the body repeats takes the next free ordinal, and the search for it resumes where
    // that role left off. Restarting at zero costs one offer per value already named, so 60
    // values cost 1,830 offers rather than 60. The budget is above the one and far below the
    // other, and the names are asserted alongside it: the search may be cheaper, never different.
    std::size_t runOffers = 0;
    const auto  run       = declaredNamesFor(sameRoleRun(60), {}, false, &runOffers);
    if (!run.has_value())
    {
        return false;
    }
    std::vector<std::string> repeated;
    for (const auto& name : *run)
    {
        if (llvm::StringRef(name).starts_with("count"))
        {
            repeated.push_back(name);
        }
    }
    if (repeated.size() != 60)
    {
        std::cerr << "the same-role run named " << repeated.size() << " values `count`, expected 60\n";
        ok = false;
    }
    else
    {
        ok = expect(repeated.front(), "count", "the first of a repeated role") && ok;
        ok = expect(repeated.at(1), "count_2", "the second of a repeated role") && ok;
        ok = expect(repeated.back(), "count_60", "the last of a repeated role") && ok;
    }
    if (runOffers > 120)
    {
        std::cerr << "naming 60 values of one role took " << runOffers
                  << " offers: the ordinal search is restarting at zero\n";
        ok = false;
    }

    // Two helpers, two markers, one module: each call is named from the function it resolves to.
    const auto marked = declaredNamesOfBody(MarkedCalls);
    if (!marked.has_value())
    {
        return false;
    }
    if (marked->size() < 2)
    {
        std::cerr << "a call to a marked helper was not named at all\n";
        ok = false;
    }
    else
    {
        ok = expect(marked->at(0), "err", "the marker on a capacity check names its answer") && ok;
        ok = expect(marked->at(1), "value", "the marker on a scalar helper names its answer") && ok;
    }

    // The count goes in at the first argument and the stamp's first name is `offset`, so a stamp
    // read at an argument's position names it after a result it is not. Two values carry the
    // count here -- the read and the argument it feeds -- so both must be spelled for it.
    const auto skewed = declaredNamesFor(SkewedLoop, {});
    if (!skewed.has_value())
    {
        return false;
    }
    const auto counts = std::count_if(skewed->begin(), skewed->end(), [](const std::string& name) {
        return name.starts_with("count");
    });
    if (counts < 2)
    {
        std::cerr << "a loop argument took its name from the result slot beside it, not from what reaches it\n";
        ok = false;
    }

    // The carry is a count on entry and a tag on every iteration after it, so the translator must
    // name it itself. Only one value here is a count: the read that feeds the loop.
    const auto forwarded = declaredNamesFor(ForwardedLoop, {});
    if (!forwarded.has_value())
    {
        return false;
    }
    const auto onlyCount = std::count_if(forwarded->begin(), forwarded->end(), [](const std::string& name) {
        return name.starts_with("count");
    });
    if (onlyCount != 1)
    {
        std::cerr << "a loop carry was called a count on every iteration, though only the first one is\n";
        ok = false;
    }

    // The offset reaches the outer guard down one arm, and nothing contradicts it down the other.
    // An `Offset` is stated by a stamp and by nothing else, so a guard that loses it here has no
    // second way to recover it.
    const auto silent = declaredNamesFor(SilentArm, {});
    if (!silent.has_value())
    {
        return false;
    }
    const auto offsets = std::count_if(silent->begin(), silent->end(), [](const std::string& name) {
        return name.starts_with("offset");
    });
    if (offsets != 2)
    {
        std::cerr << "an arm that states nothing took the offset away from the one that states it\n";
        ok = false;
    }

    // Neither member describes an address both calls read, so the name carries none. Taking the
    // first user's would name it after whichever the use list happened to present.
    const auto shared = declaredNamesFor(SharedAddress, {}, true);
    if (!shared.has_value())
    {
        return false;
    }
    for (const std::string& name : *shared)
    {
        if (name.contains("_buf"))
        {
            std::cerr << "a shared buffer address was named '" << name
                      << "', after one of the two calls that read it\n";
            ok = false;
        }
    }

    // A role word reaches a body as a local of that name, and the renderers do not strop: the
    // translator bumps a name the function has taken, not one the language has. So no role word
    // may be a keyword anywhere, and that is a property of the vocabulary rather than of a run.
    static constexpr ValueRole          EveryRole[]     = {ValueRole::Offset,
                                                           ValueRole::Object,
                                                           ValueRole::Buffer,
                                                           ValueRole::Size,
                                                           ValueRole::Length,
                                                           ValueRole::Tag,
                                                           ValueRole::Scalar,
                                                           ValueRole::Error,
                                                           ValueRole::Null,
                                                           ValueRole::Rejected,
                                                           ValueRole::IndexHolds,
                                                           ValueRole::Index};
    static constexpr llvmdsdl::Language EveryLanguage[] = {llvmdsdl::Language::C,
                                                           llvmdsdl::Language::Cpp,
                                                           llvmdsdl::Language::Rust,
                                                           llvmdsdl::Language::Go,
                                                           llvmdsdl::Language::TypeScript,
                                                           llvmdsdl::Language::Python};
    for (const ValueRole role : EveryRole)
    {
        for (const std::string& word : {snakeValueName(role, {}, 0), camelValueName(role, {}, 0)})
        {
            for (const llvmdsdl::Language language : EveryLanguage)
            {
                if (llvmdsdl::codegenIsKeyword(language, word))
                {
                    std::cerr << "the role word '" << word << "' is a keyword in one of the targets\n";
                    ok = false;
                }
            }
        }
    }

    return ok;
}
