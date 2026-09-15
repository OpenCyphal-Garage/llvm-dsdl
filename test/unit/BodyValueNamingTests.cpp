//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cctype>
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
#include "llvmdsdl/IR/DSDLDialect.h"
#include "llvmdsdl/IR/DSDLOps.h"

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
    [[nodiscard]] std::string valueName(const ValueRole role,
                                        llvm::StringRef /*member*/,
                                        const std::size_t ordinal) const override
    {
        return llvmdsdl::snakeValueName(role, {}, ordinal);
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
    [[nodiscard]] std::string isNull(mlir::dsdl::IsNullOp /*op*/, const ValueNames& /*names*/) const override
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

/// @brief Translates @p source through a spelling reserving @p reserved, and answers its names.
///
/// An operation that states a role has the spelling asked for a name rather than being numbered,
/// which is the path both the reserved pool and the stamp's arity check sit on.
std::optional<std::vector<std::string>> declaredNamesFor(const llvm::StringRef                 source,
                                                         const llvm::ArrayRef<llvm::StringRef> reserved)
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
    spelling.reserved.assign(reserved.begin(), reserved.end());
    std::ostringstream out;
    SourceWriter       w(out, IndentPolicy::spaces(2));
    if (auto err = translateFunction(fn, spelling, w))
    {
        std::cerr << "translation failed: " << llvm::toString(std::move(err)) << "\n";
        return std::nullopt;
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

    return ok;
}
