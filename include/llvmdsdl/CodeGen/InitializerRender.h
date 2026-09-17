//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// The shape of an initialise body, for a backend that renders it as a value rather than a function.
///
/// Rust cannot hold an uninitialised struct, so its `Default` is a literal; C++ keeps a type an
/// aggregate through member initialisers; a Python dataclass declares its defaults; a TypeScript
/// interface is made by a factory returning a literal. Each of those is the initialise body
/// spelt declaratively, and this is the reading of the body they share. It says what each member
/// is set to and nothing about how to spell it: the value comes from here, the member's type and
/// name from the declaration side, and the language puts the two together.
///
/// The reader recognises exactly the operations `build-dsdl-plan-bodies` puts in an initialise
/// body and refuses anything else, by name. A backend that rendered a member the body did not
/// mention would be deriving a default from somewhere other than the body, which is the thing this
/// exists to make impossible.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_CODEGEN_INITIALIZER_RENDER_H
#define LLVMDSDL_CODEGEN_INITIALIZER_RENDER_H

#include <cstdint>
#include <string>
#include <vector>

#include <llvm/Support/Error.h>
#include <mlir/Dialect/Func/IR/FuncOps.h>
#include <mlir/IR/BuiltinAttributes.h>

namespace llvmdsdl
{

/// @brief One member's default, as the initialise body states it.
struct MemberDefault final
{
    enum class Kind : std::uint8_t
    {
        /// @brief A scalar member set to @ref value.
        Scalar,
        /// @brief A variable-length array set to a length of nought.
        VariableArrayEmpty,
        /// @brief A fixed-length array of scalars, every element set to @ref value.
        FixedScalarArray,
        /// @brief A fixed-length array of composites, every element initialised through @ref callee.
        FixedCompositeArray,
        /// @brief A fixed-length array of bools, every element false.
        BoolArray,
        /// @brief A composite member initialised through @ref callee.
        Composite,
    };

    Kind kind{Kind::Scalar};

    /// @brief The DSDL member name the operation carries.
    std::string member;

    /// @brief The constant stored, for @ref Kind::Scalar and @ref Kind::FixedScalarArray.
    mlir::TypedAttr value;

    /// @brief The element count, for the fixed array kinds.
    std::int64_t count{0};

    /// @brief The nested initialise body called, for the composite kinds.
    std::string callee;
};

/// @brief What an initialise body sets, in the order it sets it.
struct InitializerShape final
{
    /// @brief True when the body stores a union tag.
    bool isUnion{false};

    /// @brief The tag stored, when @ref isUnion.
    std::int64_t unionTag{0};

    /// @brief Every member the body sets. A union's arms are all here, in tag order.
    std::vector<MemberDefault> members;
};

/// @brief Reads the shape of @p body.
/// @param[in] body An initialise body: `llvmdsdl.plan_body = "initialize"`.
/// @return The shape, or an error naming the first operation the reader does not recognise.
llvm::Expected<InitializerShape> readInitializer(mlir::func::FuncOp body);

}  // namespace llvmdsdl

#endif  // LLVMDSDL_CODEGEN_INITIALIZER_RENDER_H
