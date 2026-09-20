//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Implements the initialise-body reader of InitializerRender.h.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/CodeGen/InitializerRender.h"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include <llvm/ADT/StringRef.h>
#include <llvm/ADT/Twine.h>
#include <llvm/Support/Error.h>
#include <mlir/Dialect/Arith/IR/Arith.h>
#include <mlir/Dialect/Func/IR/FuncOps.h>
#include <mlir/Dialect/SCF/IR/SCF.h>
#include <mlir/IR/Block.h>
#include <mlir/IR/BuiltinAttributeInterfaces.h>
#include <mlir/IR/BuiltinAttributes.h>
#include <mlir/IR/Operation.h>
#include <mlir/IR/Value.h>
#include <mlir/IR/Location.h>
#include <mlir/IR/Visitors.h>
#include <mlir/Support/LLVM.h>

#include "llvmdsdl/IR/DSDLOps.h"

namespace llvmdsdl
{
namespace
{

/// @brief The constant a value is, seen through any index cast, or nothing.
std::optional<mlir::TypedAttr> constantOf(mlir::Value value)
{
    while (auto cast = value.getDefiningOp<mlir::arith::IndexCastOp>())
    {
        value = cast.getIn();
    }
    if (auto constant = value.getDefiningOp<mlir::arith::ConstantOp>())
    {
        return constant.getValue();
    }
    return std::nullopt;
}

/// @brief The integer a value is, or nothing.
std::optional<std::int64_t> integerOf(mlir::Value value)
{
    const auto constant = constantOf(value);
    if (!constant)
    {
        return std::nullopt;
    }
    if (const auto integer = mlir::dyn_cast<mlir::IntegerAttr>(*constant))
    {
        return integer.getInt();
    }
    return std::nullopt;
}

llvm::Error unrecognised(mlir::Operation* op, const llvm::Twine& why)
{
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "initialise body: %s: %s",
                                   op->getName().getStringRef().str().c_str(),
                                   why.str().c_str());
}

/// @brief Reads one counted loop: an array of scalars stored, or of composites initialised.
llvm::Error readLoop(mlir::scf::ForOp loop, InitializerShape& shape)
{
    const auto count = integerOf(loop.getUpperBound());
    if (!count)
    {
        return unrecognised(loop, "a fixed array's loop bound is not a constant");
    }
    MemberDefault entry;
    entry.count = *count;
    bool seen   = false;
    for (mlir::Operation& op : loop.getBody()->getOperations())
    {
        if (auto store = mlir::dyn_cast<mlir::dsdl::StoreElementOp>(op))
        {
            const auto value = constantOf(store.getValue());
            if (!value || seen)
            {
                return unrecognised(&op, "an element store that is not one constant per array");
            }
            entry.kind   = MemberDefault::Kind::FixedScalarArray;
            entry.member = store.getMember().str();
            entry.value  = *value;
            seen         = true;
            continue;
        }
        if (auto call = mlir::dyn_cast<mlir::dsdl::CallInitializeOp>(op))
        {
            if (seen)
            {
                return unrecognised(&op, "more than one initialiser per array");
            }
            entry.kind   = MemberDefault::Kind::FixedCompositeArray;
            entry.member = call.getMember().str();
            entry.callee = call.getCallee().str();
            seen         = true;
            continue;
        }
        if (mlir::isa<mlir::arith::ConstantOp,
                      mlir::arith::IndexCastOp,
                      mlir::dsdl::ElementAddrOp,
                      mlir::scf::YieldOp,
                      mlir::arith::OrIOp,
                      mlir::arith::SelectOp,
                      mlir::arith::CmpIOp>(op))
        {
            continue;
        }
        return unrecognised(&op, "not an operation of an initialise body's loop");
    }
    if (!seen)
    {
        return unrecognised(loop, "a loop that sets nothing");
    }
    shape.members.push_back(std::move(entry));
    return llvm::Error::success();
}

}  // namespace

llvm::Expected<InitializerShape> readInitializer(mlir::func::FuncOp body)
{
    InitializerShape shape;
    llvm::Error      failure = llvm::Error::success();

    body.walk<mlir::WalkOrder::PreOrder>([&](mlir::Operation* op) -> mlir::WalkResult {
        if (failure)
        {
            return mlir::WalkResult::interrupt();
        }
        auto fail = [&](llvm::Error error) {
            failure = std::move(error);
            return mlir::WalkResult::interrupt();
        };

        if (auto loop = mlir::dyn_cast<mlir::scf::ForOp>(op))
        {
            if (auto error = readLoop(loop, shape))
            {
                return fail(std::move(error));
            }
            return mlir::WalkResult::skip();
        }
        if (auto tag = mlir::dyn_cast<mlir::dsdl::SetUnionTagOp>(op))
        {
            const auto value = integerOf(tag.getValue());
            if (!value)
            {
                return fail(unrecognised(op, "a union tag that is not a constant"));
            }
            shape.isUnion  = true;
            shape.unionTag = *value;
            return mlir::WalkResult::advance();
        }
        if (auto store = mlir::dyn_cast<mlir::dsdl::StoreMemberOp>(op))
        {
            const auto value = constantOf(store.getValue());
            if (!value)
            {
                return fail(unrecognised(op, "a stored value that is not a constant"));
            }
            shape.members.push_back({MemberDefault::Kind::Scalar, store.getMember().str(), *value, 0, {}});
            return mlir::WalkResult::advance();
        }
        if (auto length = mlir::dyn_cast<mlir::dsdl::SetArrayLengthOp>(op))
        {
            const auto value = integerOf(length.getValue());
            if (!value || *value != 0)
            {
                return fail(unrecognised(op, "a length other than nought"));
            }
            shape.members.push_back(
                {MemberDefault::Kind::VariableArrayEmpty, length.getMember().str(), mlir::TypedAttr{}, 0, {}});
            return mlir::WalkResult::advance();
        }
        if (auto read = mlir::dyn_cast<mlir::dsdl::BitReadOp>(op))
        {
            auto       packed = read.getDestination().getDefiningOp<mlir::dsdl::ElementAddrOp>();
            const auto count  = integerOf(read.getWidth());
            const auto size   = integerOf(read.getBufferSizeBytes());
            if (!packed || !count || !size || *size != 0)
            {
                return fail(unrecognised(op, "a bit read that is not a bool array from no bytes"));
            }
            shape.members.push_back(
                {MemberDefault::Kind::BoolArray, packed.getMember().str(), mlir::TypedAttr{}, *count, {}});
            return mlir::WalkResult::advance();
        }
        if (auto call = mlir::dyn_cast<mlir::dsdl::CallInitializeOp>(op))
        {
            shape.members.push_back(
                {MemberDefault::Kind::Composite, call.getMember().str(), mlir::TypedAttr{}, 0, call.getCallee().str()});
            return mlir::WalkResult::advance();
        }
        if (auto clear = mlir::dyn_cast<mlir::dsdl::ClearViewOp>(op))
        {
            shape.members.push_back({MemberDefault::Kind::View, clear.getMember().str(), mlir::TypedAttr{}, 0, {}});
            return mlir::WalkResult::advance();
        }
        // The scaffolding of the body: its null check, its constants, the addresses it takes, and
        // the error it threads. None of these sets a member.
        if (mlir::isa<mlir::func::FuncOp,
                      mlir::func::ReturnOp,
                      mlir::scf::IfOp,
                      mlir::scf::YieldOp,
                      mlir::arith::ConstantOp,
                      mlir::arith::IndexCastOp,
                      mlir::arith::OrIOp,
                      mlir::arith::SelectOp,
                      mlir::arith::CmpIOp,
                      mlir::dsdl::IsNullOp,
                      mlir::dsdl::MemberAddrOp,
                      mlir::dsdl::ElementAddrOp,
                      mlir::dsdl::LocalOp>(op))
        {
            return mlir::WalkResult::advance();
        }
        return fail(unrecognised(op, "not an operation of an initialise body"));
    });

    if (failure)
    {
        return std::move(failure);
    }
    return shape;
}

}  // namespace llvmdsdl
