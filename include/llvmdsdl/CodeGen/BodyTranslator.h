//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Translates the functions `lower-dsdl-bodies` builds into a language's source text.
///
/// `translateFunction` walks one `func.func` -- a serialise or deserialise body, or a helper --
/// names its values, and spells each operation through a `BodySpelling`. The translator holds
/// the structure: which values are declared, and `scf.if`, `scf.while` and `scf.for` as the
/// language's structured statements. A spelling holds the language: its types and literals, its
/// operators, its runtime primitives, and its access to the object a plan reads and writes. The
/// translator is given the function and nothing else.
///
//===----------------------------------------------------------------------===//

#ifndef LLVMDSDL_CODEGEN_BODY_TRANSLATOR_H
#define LLVMDSDL_CODEGEN_BODY_TRANSLATOR_H

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/Error.h>
#include <mlir/Dialect/Func/IR/FuncOps.h>
#include <mlir/IR/BuiltinOps.h>
#include <mlir/IR/Types.h>
#include <mlir/IR/Value.h>

#include "llvmdsdl/IR/DSDLOps.h"

namespace llvmdsdl
{

class SourceWriter;

/// @brief The integer operators a body applies.
enum class BinaryOperator : std::uint8_t
{
    Add,
    Sub,
    Mul,
    DivU,
    DivS,
    RemU,
    RemS,
    And,
    Or,
    Xor,
    ShiftLeft,
    ShiftRightU,
    ShiftRightS,
};

/// @brief The integer comparisons a body applies.
enum class Comparison : std::uint8_t
{
    Eq,
    Ne,
    LtS,
    LeS,
    GtS,
    GeS,
    LtU,
    LeU,
    GtU,
    GeU,
};

/// @brief The conversions a body applies between its value types.
enum class Conversion : std::uint8_t
{
    ZeroExtend,
    SignExtend,
    Truncate,
    IndexCast,
};

/// @brief The spelled form of each value in the function being translated.
///
/// A spelling asks for an operand by value so that it can also look at the operation that
/// produced it: a bulk bit copy spells differently when its source is a container the language
/// cannot take the address of.
class ValueNames
{
public:
    ValueNames()                             = default;
    ValueNames(const ValueNames&)            = delete;
    ValueNames& operator=(const ValueNames&) = delete;
    ValueNames(ValueNames&&)                 = delete;
    ValueNames& operator=(ValueNames&&)      = delete;
    virtual ~ValueNames()                    = default;

    /// @brief The expression @p value is read as.
    [[nodiscard]] virtual std::string operator()(mlir::Value value) const = 0;
};

/// @brief One language's spelling of the plan-body vocabulary.
///
/// Expressions come back as strings; statements are written to the writer. A method receives
/// its operation, so the attributes that select a primitive or name a member are the
/// operation's own.
class BodySpelling
{
public:
    BodySpelling()                               = default;
    BodySpelling(const BodySpelling&)            = delete;
    BodySpelling& operator=(const BodySpelling&) = delete;
    BodySpelling(BodySpelling&&)                 = delete;
    BodySpelling& operator=(BodySpelling&&)      = delete;
    virtual ~BodySpelling()                      = default;

    // Functions.

    /// @brief Opens @p fn: its signature and the start of its body.
    /// @return The parameter names, one per argument.
    virtual std::vector<std::string> openFunction(SourceWriter& w, mlir::func::FuncOp fn) const = 0;

    /// @brief Closes the body @ref openFunction opened.
    virtual void closeFunction(SourceWriter& w, mlir::func::FuncOp fn) const = 0;

    /// @brief The name a call to @p callee, a function of the same module, spells.
    [[nodiscard]] virtual std::string functionName(llvm::StringRef callee) const = 0;

    // Statements.

    /// @brief Declares @p name as the value of @p expr, never assigned again.
    virtual void declare(SourceWriter& w, mlir::Type type, llvm::StringRef name, llvm::StringRef expr) const = 0;

    /// @brief Declares @p name to be assigned by the arms of a structured statement.
    /// @param[in] reassigned Whether the variable is assigned more than once on a path: a
    ///                       loop-carried value is, an `scf.if` result is not.
    virtual void declareVariable(SourceWriter& w, mlir::Type type, llvm::StringRef name, bool reassigned) const = 0;

    virtual void assign(SourceWriter& w, llvm::StringRef name, llvm::StringRef expr) const = 0;

    /// @brief Evaluates @p expr for its effect, or marks a variable the function never reads.
    virtual void discard(SourceWriter& w, llvm::StringRef expr) const = 0;

    virtual void returnValue(SourceWriter& w, llvm::StringRef expr) const = 0;

    virtual void openIf(SourceWriter& w, llvm::StringRef condition) const = 0;
    virtual void openElse(SourceWriter& w) const                          = 0;

    /// @brief Opens a loop that runs until @ref breakUnless ends it.
    virtual void openLoop(SourceWriter& w) const                               = 0;
    virtual void breakUnless(SourceWriter& w, llvm::StringRef condition) const = 0;

    /// @brief Opens a counted loop over `[lower, upper)` in steps of @p step.
    virtual void openFor(SourceWriter&   w,
                         llvm::StringRef variable,
                         llvm::StringRef lower,
                         llvm::StringRef upper,
                         llvm::StringRef step) const = 0;

    virtual void closeBlock(SourceWriter& w) const = 0;

    // Values.

    [[nodiscard]] virtual std::string constant(mlir::TypedAttr value) const = 0;

    [[nodiscard]] virtual std::string binary(BinaryOperator  op,
                                             llvm::StringRef lhs,
                                             llvm::StringRef rhs,
                                             mlir::Type      type) const = 0;

    [[nodiscard]] virtual std::string compare(Comparison      comparison,
                                              llvm::StringRef lhs,
                                              llvm::StringRef rhs,
                                              mlir::Type      type) const = 0;

    [[nodiscard]] virtual std::string select(llvm::StringRef condition,
                                             llvm::StringRef ifTrue,
                                             llvm::StringRef ifFalse,
                                             mlir::Type      type) const = 0;

    [[nodiscard]] virtual std::string convert(Conversion      conversion,
                                              llvm::StringRef value,
                                              mlir::Type      from,
                                              mlir::Type      to) const = 0;

    [[nodiscard]] virtual std::string call(llvm::StringRef callee, llvm::ArrayRef<std::string> args) const = 0;

    // The dialect.

    /// @brief Whether @p op's result is spelled where it is used rather than declared first.
    ///
    /// An address the language forms from a container expression has no value of its own to
    /// declare.
    [[nodiscard]] virtual bool spellsInline(mlir::Operation* op) const = 0;

    [[nodiscard]] virtual std::string isNull(mlir::dsdl::IsNullOp op, const ValueNames& names) const               = 0;
    [[nodiscard]] virtual std::string bufferOrEmpty(mlir::dsdl::BufferOrEmptyOp op, const ValueNames& names) const = 0;
    [[nodiscard]] virtual std::string bufferAt(mlir::dsdl::BufferAtOp op, const ValueNames& names) const           = 0;
    [[nodiscard]] virtual std::string loadScalar(mlir::dsdl::LoadScalarOp op, const ValueNames& names) const       = 0;
    virtual void storeScalar(SourceWriter& w, mlir::dsdl::StoreScalarOp op, const ValueNames& names) const         = 0;

    /// @brief Declares the local @p op holds under @p name.
    /// @return The expression that addresses it.
    [[nodiscard]] virtual std::string local(SourceWriter&       w,
                                            mlir::dsdl::LocalOp op,
                                            llvm::StringRef     name,
                                            const ValueNames&   names) const = 0;

    [[nodiscard]] virtual std::string loadMember(mlir::dsdl::LoadMemberOp op, const ValueNames& names) const     = 0;
    virtual void storeMember(SourceWriter& w, mlir::dsdl::StoreMemberOp op, const ValueNames& names) const       = 0;
    [[nodiscard]] virtual std::string loadElement(mlir::dsdl::LoadElementOp op, const ValueNames& names) const   = 0;
    virtual void storeElement(SourceWriter& w, mlir::dsdl::StoreElementOp op, const ValueNames& names) const     = 0;
    [[nodiscard]] virtual std::string memberAddr(mlir::dsdl::MemberAddrOp op, const ValueNames& names) const     = 0;
    [[nodiscard]] virtual std::string elementAddr(mlir::dsdl::ElementAddrOp op, const ValueNames& names) const   = 0;
    [[nodiscard]] virtual std::string arrayLength(mlir::dsdl::ArrayLengthOp op, const ValueNames& names) const   = 0;
    virtual void setArrayLength(SourceWriter& w, mlir::dsdl::SetArrayLengthOp op, const ValueNames& names) const = 0;
    [[nodiscard]] virtual std::string unionTag(mlir::dsdl::UnionTagOp op, const ValueNames& names) const         = 0;
    virtual void setUnionTag(SourceWriter& w, mlir::dsdl::SetUnionTagOp op, const ValueNames& names) const       = 0;
    [[nodiscard]] virtual std::string writeBits(mlir::dsdl::WriteBitsOp op, const ValueNames& names) const       = 0;
    [[nodiscard]] virtual std::string readBits(mlir::dsdl::ReadBitsOp op, const ValueNames& names) const         = 0;
    virtual void bitWrite(SourceWriter& w, mlir::dsdl::BitWriteOp op, const ValueNames& names) const             = 0;
    virtual void bitRead(SourceWriter& w, mlir::dsdl::BitReadOp op, const ValueNames& names) const               = 0;
    [[nodiscard]] virtual std::string callSerdes(mlir::dsdl::CallSerdesOp op, const ValueNames& names) const     = 0;
};

/// @brief Spells @p fn through @p spelling into @p w.
/// @return An error naming the first operation the translator has no spelling for.
llvm::Error translateFunction(mlir::func::FuncOp fn, const BodySpelling& spelling, SourceWriter& w);

/// @brief The functions `lower-dsdl-bodies` built for @p schemaSym, in module order.
std::vector<mlir::func::FuncOp> schemaFunctions(mlir::ModuleOp module, llvm::StringRef schemaSym);

/// @brief The direction of a plan body, "serialize" or "deserialize"; nullopt for a helper.
std::optional<llvm::StringRef> planBodyDirection(mlir::func::FuncOp fn);

}  // namespace llvmdsdl

#endif  // LLVMDSDL_CODEGEN_BODY_TRANSLATOR_H
