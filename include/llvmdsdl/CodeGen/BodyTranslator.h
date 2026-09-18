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

#include <cstddef>
#include <cstdint>
#include <memory>
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

/// @brief What the operation that defines a value says the value is.
///
/// The translator reads the role from the operation alone, so it is the same in every
/// language; a spelling renders it as one of its own identifiers. `Anonymous` is an operation
/// that says nothing about its result, and the translator names those itself.
enum class ValueRole : std::uint8_t
{
    Anonymous,
    Offset,      ///< The bit offset a plan threads through its steps.
    Object,      ///< The address of a member, or of one element of an array member.
    Buffer,      ///< An address within the wire buffer.
    Size,        ///< A size in bytes, held where a nested call can write back to it.
    Length,      ///< An array member's element count.
    Tag,         ///< A union's tag.
    Scalar,      ///< A member's value, or a scalar read out of the buffer.
    Error,       ///< An error code.
    Null,        ///< Whether a pointer the plan was handed is null.
    Rejected,    ///< Whether the arguments a body was handed can be read at all.
    IndexHolds,  ///< Whether the target's index type holds a count.
    Index,       ///< A loop's induction variable.
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

    /// @brief The identifier a value of @p role is declared under.
    ///
    /// @p member is the DSDL member the defining operation names, empty when it names none.
    ///
    /// @p ordinal is which candidate is being asked for, not which value is being named: the
    /// translator asks from zero upwards until it is offered a name the function has not already
    /// used, so a value takes the first offer that is free. The first value of a role may
    /// therefore be named from an ordinal above zero, when a parameter or a reserved local has
    /// claimed the one below it. A spelling decides how a later candidate differs from an earlier
    /// one as well as how the name is cased; @ref snakeValueName and @ref camelValueName render
    /// the two styles the backends use.
    ///
    /// The translator names a value itself when this answers with an empty string, which is
    /// what @ref ValueRole::Anonymous is given.
    [[nodiscard]] virtual std::string valueName(ValueRole role, llvm::StringRef member, std::size_t ordinal) const = 0;

    /// @brief The identifiers already in scope that a body depends on.
    ///
    /// Two kinds. A body that reaches into the language's own namespace by bare name is captured
    /// by a local of the same name, and the capture is legal code that means something else. And a
    /// signature or prologue may open the body with a name @ref openFunction cannot report, since
    /// that answers one identifier per argument of the function being translated and a spelling
    /// may write more.
    ///
    /// The translator claims these before it names anything, so a value whose role lands on one is
    /// distinguished the way a repeat is, and a role word is chosen for how it reads rather than
    /// for what it avoids.
    ///
    /// A spelling that qualifies every call it makes, and opens with nothing of its own, answers
    /// with nothing.
    [[nodiscard]] virtual llvm::ArrayRef<llvm::StringRef> reservedLocals() const = 0;

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

    /// @brief Declares @p name as one of two values by @p condition.
    ///
    /// A language without a conditional expression spells this as a statement; the others
    /// declare the value of @ref select.
    virtual void declareSelect(SourceWriter&   w,
                               mlir::Type      type,
                               llvm::StringRef name,
                               llvm::StringRef condition,
                               llvm::StringRef ifTrue,
                               llvm::StringRef ifFalse) const
    {
        declare(w, type, name, select(condition, ifTrue, ifFalse, type));
    }

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
    [[nodiscard]] virtual std::string indexHolds(mlir::dsdl::IndexHoldsOp op, const ValueNames& names) const       = 0;
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

    /// @brief One move for a whole payload, where the structure is the wire image.
    ///
    /// Only a target whose objects are byte images of the wire is asked for this; the fold that
    /// produces the op runs under that target's capability. A backend whose objects are not --
    /// TypeScript's, Python's -- never receives it, and its spelling says so.
    virtual void imageRead(SourceWriter& w, mlir::dsdl::ImageReadOp op, const ValueNames& names) const       = 0;
    virtual void imageWrite(SourceWriter& w, mlir::dsdl::ImageWriteOp op, const ValueNames& names) const     = 0;
    [[nodiscard]] virtual std::string callSerdes(mlir::dsdl::CallSerdesOp op, const ValueNames& names) const = 0;

    /// @brief Declares @p name as the error code a nested call answers with.
    ///
    /// A language whose call answers two values spells this as a statement; the others declare
    /// the value of @ref callSerdes. @p name is empty when the plan does not read the code.
    virtual void declareCallSerdes(SourceWriter&            w,
                                   llvm::StringRef          name,
                                   mlir::dsdl::CallSerdesOp op,
                                   const ValueNames&        names) const
    {
        if (name.empty())
        {
            discard(w, callSerdes(op, names));
            return;
        }
        declare(w, op.getError().getType(), name, callSerdes(op, names));
    }

    /// @brief Declares @p name as the error code a nested initialiser answers with.
    ///
    /// No spelling translates an initialise body as a function. The five languages with a body
    /// translator render initialisation declaratively, from the same body, and C reaches it through
    /// EmitC; so the default refuses, with a reason, rather than leaving a body it was handed
    /// mis-spelt. A spelling that does translate one imperatively overrides this.
    virtual void declareCallInitialize(SourceWriter&                w,
                                       llvm::StringRef              name,
                                       mlir::dsdl::CallInitializeOp op,
                                       const ValueNames&            names) const;
};

/// @brief Renders @p role as `member_role`, `role` when @p member is empty, and appends
///        `_<ordinal + 1>` to a repeat.
std::string snakeValueName(ValueRole role, llvm::StringRef member, std::size_t ordinal);

/// @brief Renders @p role as `memberRole`, `role` when @p member is empty, and appends
///        `<ordinal + 1>` to a repeat.
std::string camelValueName(ValueRole role, llvm::StringRef member, std::size_t ordinal);

/// @brief What the translator asks of one module once, for every function it translates from it.
///
/// A helper states the role of its answer as an attribute on itself, so naming a call's result
/// means resolving the callee, and resolving a symbol by walking the module's top-level operations
/// costs a pass over every type the run generates. Asking once per call made that cost the
/// catalogue's size squared; the answers hold for as long as the module does, so this holds them.
///
/// Build one from the module being generated, hand it to every @ref translateFunction called on
/// that module's functions, and let it go before the module does. It answers about the operations
/// the module holds, so it describes that module and no other.
class PlanBodyLookups final
{
public:
    explicit PlanBodyLookups(mlir::ModuleOp module);
    PlanBodyLookups(const PlanBodyLookups&)            = delete;
    PlanBodyLookups& operator=(const PlanBodyLookups&) = delete;
    PlanBodyLookups(PlanBodyLookups&&)                 = delete;
    PlanBodyLookups& operator=(PlanBodyLookups&&)      = delete;
    ~PlanBodyLookups();

    /// @brief The role the result of @p call carries, from the markers the lowering left.
    /// @return @ref ValueRole::Anonymous where the callee carries none of them.
    [[nodiscard]] ValueRole roleOfCallee(mlir::func::CallOp call);

private:
    struct State;
    std::unique_ptr<State> state_;
};

/// @brief Spells @p fn through @p spelling into @p w.
/// @param[in,out] lookups What @p fn's module has already been asked, shared across its functions.
/// @return An error naming the first operation the translator has no spelling for.
llvm::Error translateFunction(mlir::func::FuncOp  fn,
                              const BodySpelling& spelling,
                              SourceWriter&       w,
                              PlanBodyLookups&    lookups);

/// @brief The functions `lower-dsdl-bodies` built for @p schemaSym, in module order.
std::vector<mlir::func::FuncOp> schemaFunctions(mlir::ModuleOp module, llvm::StringRef schemaSym);

/// @brief The direction of a plan body, "serialize" or "deserialize"; nullopt for a helper.
std::optional<llvm::StringRef> planBodyDirection(mlir::func::FuncOp fn);

}  // namespace llvmdsdl

#endif  // LLVMDSDL_CODEGEN_BODY_TRANSLATOR_H
