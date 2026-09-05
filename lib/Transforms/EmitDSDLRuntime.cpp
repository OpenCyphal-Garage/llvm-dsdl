//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Defines the serialisation primitives a lowered plan calls.
///
/// `runtime/dsdl_runtime.h` declares them `static inline`, so a C translation unit carries its
/// own copy and no symbol survives compilation. An object has nothing to link against, which is
/// why these are built here rather than called out to.
///
/// Everything reduces to moving a run of bits from one buffer to another at an arbitrary bit
/// offset. The header takes a byte-at-a-time path when both offsets are byte-aligned and a
/// bitwise one otherwise; both write exactly the requested bits and leave the rest of the
/// destination alone, so one bitwise loop answers for both.
///
//===----------------------------------------------------------------------===//

#include <llvm/ADT/StringRef.h>
#include <llvm/ADT/SmallVector.h>
#include <cstdint>
#include <mlir/Dialect/Arith/IR/Arith.h>
#include <mlir/Dialect/Func/IR/FuncOps.h>
#include <mlir/Dialect/LLVMIR/LLVMAttrs.h>
#include <mlir/Dialect/LLVMIR/LLVMDialect.h>
#include <mlir/Dialect/LLVMIR/LLVMTypes.h>
#include <mlir/Dialect/SCF/IR/SCF.h>
#include <mlir/IR/Block.h>
#include <mlir/IR/Builders.h>
#include <mlir/IR/BuiltinOps.h>
#include <mlir/IR/TypeRange.h>
#include <mlir/IR/Value.h>
#include <mlir/IR/Location.h>
#include <mlir/IR/DialectRegistry.h>
#include <mlir/Pass/Pass.h>
#include <mlir/Pass/PassRegistry.h>

#include <memory>
#include <mlir/Support/LLVM.h>
#include <string>

#include "llvmdsdl/Transforms/Passes.h"

namespace llvmdsdl
{
namespace
{

constexpr llvm::StringLiteral kSizeBitsAttr{"llvmdsdl.size_bits"};

/// @brief Builds a function with @p name, or returns the one already built.
mlir::LLVM::LLVMFuncOp declare(mlir::OpBuilder& b,
                               mlir::ModuleOp   module,
                               llvm::StringRef  name,
                               mlir::TypeRange  arguments,
                               mlir::TypeRange  results)
{
    if (auto existing = module.lookupSymbol<mlir::LLVM::LLVMFuncOp>(name))
    {
        return existing;
    }
    mlir::OpBuilder::InsertionGuard const guard(b);
    b.setInsertionPointToStart(module.getBody());
    auto type =
        mlir::LLVM::LLVMFunctionType::get(results.empty() ? mlir::Type{mlir::LLVM::LLVMVoidType::get(b.getContext())}
                                                          : results.front(),
                                          llvm::SmallVector<mlir::Type>(arguments.begin(), arguments.end()));
    return mlir::LLVM::LLVMFuncOp::create(b, module.getLoc(), name, type);
}

mlir::Value constant(mlir::OpBuilder& b, mlir::Location loc, mlir::Type type, const std::int64_t value)
{
    return mlir::arith::ConstantOp::create(b, loc, type, b.getIntegerAttr(type, value));
}

/// @brief The byte at @p index of @p buffer.
mlir::Value loadByte(mlir::OpBuilder& b, mlir::Location loc, mlir::Value buffer, mlir::Value index)
{
    auto i8  = b.getIntegerType(8);
    auto ptr = mlir::LLVM::GEPOp::create(b,
                                         loc,
                                         mlir::LLVM::LLVMPointerType::get(b.getContext()),
                                         i8,
                                         buffer,
                                         mlir::ValueRange{index});
    return mlir::LLVM::LoadOp::create(b, loc, i8, ptr);
}

void storeByte(mlir::OpBuilder& b, mlir::Location loc, mlir::Value buffer, mlir::Value index, mlir::Value value)
{
    auto ptr = mlir::LLVM::GEPOp::create(b,
                                         loc,
                                         mlir::LLVM::LLVMPointerType::get(b.getContext()),
                                         b.getIntegerType(8),
                                         buffer,
                                         mlir::ValueRange{index});
    mlir::LLVM::StoreOp::create(b, loc, value, ptr);
}

/// @brief Zero-extends @p value to @p target, which at equal widths is @p value itself.
mlir::Value widen(mlir::OpBuilder& b, mlir::Location loc, mlir::Value value, mlir::Type target)
{
    if (value.getType() == target)
    {
        return value;
    }
    return mlir::arith::ExtUIOp::create(b, loc, target, value);
}

/// @brief Converts @p value to @p target, whichever way the widths run.
///
/// A mask is built in a type wide enough to hold a shift the byte itself could not, and what
/// that costs is a conversion whose direction depends on what the target spells `size_t` at.
mlir::Value resize(mlir::OpBuilder& b, mlir::Location loc, mlir::Value value, mlir::Type target)
{
    const auto from = mlir::cast<mlir::IntegerType>(value.getType()).getWidth();
    const auto to   = mlir::cast<mlir::IntegerType>(target).getWidth();
    if (from == to)
    {
        return value;
    }
    return (from > to) ? mlir::Value{mlir::arith::TruncIOp::create(b, loc, target, value)}
                       : mlir::Value{mlir::arith::ExtUIOp::create(b, loc, target, value)};
}

/// @brief `saturate_fragment_bits`: what of @p length can be taken before the buffer ends.
mlir::Value saturate(mlir::OpBuilder& b,
                     mlir::Location   loc,
                     mlir::Value      sizeBytes,
                     mlir::Value      offset,
                     mlir::Value      length)
{
    auto              sizeTy   = sizeBytes.getType();
    const mlir::Value sizeBits = mlir::arith::MulIOp::create(b, loc, sizeBytes, constant(b, loc, sizeTy, 8));
    const mlir::Value reached  = mlir::arith::MinUIOp::create(b, loc, sizeBits, offset);
    const mlir::Value tail     = mlir::arith::SubIOp::create(b, loc, sizeBits, reached);
    return mlir::arith::MinUIOp::create(b, loc, length, tail);
}

/// @brief A stack slot of @p bytes, zeroed.
mlir::Value scratch(mlir::OpBuilder& b, mlir::Location loc, const std::int64_t bytes)
{
    auto              i8   = b.getIntegerType(8);
    auto              ptr  = mlir::LLVM::LLVMPointerType::get(b.getContext());
    const mlir::Value one  = mlir::LLVM::ConstantOp::create(b, loc, b.getIntegerType(64), b.getI64IntegerAttr(bytes));
    mlir::Value       slot = mlir::LLVM::AllocaOp::create(b, loc, ptr, i8, one);
    const mlir::Value zero = constant(b, loc, i8, 0);
    for (std::int64_t i = 0; i < bytes; ++i)
    {
        storeByte(b,
                  loc,
                  slot,
                  mlir::LLVM::ConstantOp::create(b, loc, b.getIntegerType(64), b.getI64IntegerAttr(i)),
                  zero);
    }
    return slot;
}

/// @brief Reads @p bytes from @p buffer as one little-endian integer of @p resultTy.
mlir::Value assembleLE(mlir::OpBuilder&   b,
                       mlir::Location     loc,
                       mlir::Value        buffer,
                       const std::int64_t bytes,
                       mlir::Type         resultTy)
{
    mlir::Value out = constant(b, loc, resultTy, 0);
    for (std::int64_t i = 0; i < bytes; ++i)
    {
        const mlir::Value byte =
            loadByte(b,
                     loc,
                     buffer,
                     mlir::LLVM::ConstantOp::create(b, loc, b.getIntegerType(64), b.getI64IntegerAttr(i)));
        const mlir::Value wide = widen(b, loc, byte, resultTy);
        out = mlir::arith::OrIOp::create(b,
                                         loc,
                                         out,
                                         mlir::arith::ShLIOp::create(b, loc, wide, constant(b, loc, resultTy, i * 8)));
    }
    return out;
}

/// @brief Writes @p value into @p buffer as @p bytes little-endian bytes.
void disassembleLE(mlir::OpBuilder&   b,
                   mlir::Location     loc,
                   mlir::Value        buffer,
                   mlir::Value        value,
                   const std::int64_t bytes)
{
    auto i8   = b.getIntegerType(8);
    auto type = value.getType();
    for (std::int64_t i = 0; i < bytes; ++i)
    {
        const mlir::Value shifted = mlir::arith::ShRUIOp::create(b, loc, value, constant(b, loc, type, i * 8));
        storeByte(b,
                  loc,
                  buffer,
                  mlir::LLVM::ConstantOp::create(b, loc, b.getIntegerType(64), b.getI64IntegerAttr(i)),
                  mlir::arith::TruncIOp::create(b, loc, i8, shifted));
    }
}

/// @brief Starts a function body, or answers none when one was already built.
mlir::Block* begin(mlir::OpBuilder& b, mlir::LLVM::LLVMFuncOp fn)
{
    if (!fn.getBody().empty())
    {
        return nullptr;
    }
    // `static inline` in the header gives every translation unit its own copy and exports none,
    // and an object built from this carries the same. Exported, each object would define the
    // primitives again and no two could be linked together.
    fn.setLinkage(mlir::LLVM::Linkage::Internal);
    mlir::Block* entry = fn.addEntryBlock(b);
    b.setInsertionPointToStart(entry);
    return entry;
}

/// @brief Defines `copy_bits`, which every other primitive is expressed through.
///
/// Two paths, as the header has. When both offsets sit on a byte boundary the whole run moves a
/// byte at a time and only the last partial byte is merged; otherwise the run moves in pieces of
/// up to eight bits, each piece being what is left before the nearer of the two bytes ends.
///
/// The header spells the aligned path with `memmove`. A loop is written here instead so that an
/// object carries no undefined symbol: the point of this lane is that what it emits needs nothing
/// linked in behind it. Overlapping buffers are outside the contract either way -- the header
/// asserts the two differ.
void buildCopyBits(mlir::OpBuilder& b, mlir::ModuleOp module, mlir::Type sizeTy)
{
    auto loc = module.getLoc();
    auto i8  = b.getIntegerType(8);
    auto i32 = b.getIntegerType(32);
    auto ptr = mlir::LLVM::LLVMPointerType::get(b.getContext());
    auto fn  = declare(b, module, "dsdl_runtime_copy_bits", {ptr, sizeTy, sizeTy, ptr, sizeTy}, {});

    mlir::OpBuilder::InsertionGuard const guard(b);
    fn.setLinkage(mlir::LLVM::Linkage::Internal);
    mlir::Block* entry = begin(b, fn);
    if (entry == nullptr)
    {
        return;
    }

    const mlir::Value dst    = entry->getArgument(0);
    const mlir::Value dstOff = entry->getArgument(1);
    const mlir::Value length = entry->getArgument(2);
    const mlir::Value src    = entry->getArgument(3);
    const mlir::Value srcOff = entry->getArgument(4);

    auto              idxTy = b.getIndexType();
    const mlir::Value eight = constant(b, loc, sizeTy, 8);
    const mlir::Value zero  = constant(b, loc, sizeTy, 0);

    const mlir::Value srcMod = mlir::arith::RemUIOp::create(b, loc, srcOff, eight);
    const mlir::Value dstMod = mlir::arith::RemUIOp::create(b, loc, dstOff, eight);
    const mlir::Value aligned =
        mlir::arith::AndIOp::create(b,
                                    loc,
                                    mlir::arith::CmpIOp::create(b, loc, mlir::arith::CmpIPredicate::eq, srcMod, zero),
                                    mlir::arith::CmpIOp::create(b, loc, mlir::arith::CmpIPredicate::eq, dstMod, zero));

    auto choice = mlir::scf::IfOp::create(b, loc, mlir::TypeRange{}, aligned, true, true);
    {
        mlir::OpBuilder::InsertionGuard const inner(b);
        b.setInsertionPointToStart(choice.thenBlock());

        const mlir::Value bytes    = mlir::arith::DivUIOp::create(b, loc, length, eight);
        const mlir::Value srcStart = mlir::arith::DivUIOp::create(b, loc, srcOff, eight);
        const mlir::Value dstStart = mlir::arith::DivUIOp::create(b, loc, dstOff, eight);

        auto move = mlir::scf::ForOp::create(b,
                                             loc,
                                             mlir::arith::ConstantIndexOp::create(b, loc, 0),
                                             mlir::arith::IndexCastOp::create(b, loc, idxTy, bytes),
                                             mlir::arith::ConstantIndexOp::create(b, loc, 1));
        {
            mlir::OpBuilder::InsertionGuard const body(b);
            b.setInsertionPointToStart(move.getBody());
            const mlir::Value at = mlir::arith::IndexCastOp::create(b, loc, sizeTy, move.getInductionVar());
            storeByte(b,
                      loc,
                      dst,
                      mlir::arith::AddIOp::create(b, loc, dstStart, at),
                      loadByte(b, loc, src, mlir::arith::AddIOp::create(b, loc, srcStart, at)));
        }

        // A length that is not a whole number of bytes leaves a few bits in the byte after the
        // run; the rest of that byte is the destination's and stays as it was.
        const mlir::Value spare = mlir::arith::RemUIOp::create(b, loc, length, eight);
        auto              partial =
            mlir::scf::IfOp::create(b,
                                    loc,
                                    mlir::TypeRange{},
                                    mlir::arith::CmpIOp::create(b, loc, mlir::arith::CmpIPredicate::ne, spare, zero),
                                    true,
                                    false);
        {
            mlir::OpBuilder::InsertionGuard const body(b);
            b.setInsertionPointToStart(partial.thenBlock());
            const mlir::Value one  = constant(b, loc, i32, 1);
            const mlir::Value mask = mlir::arith::TruncIOp::
                create(b,
                       loc,
                       i8,
                       mlir::arith::SubIOp::create(b,
                                                   loc,
                                                   mlir::arith::ShLIOp::create(b, loc, one, resize(b, loc, spare, i32)),
                                                   one));
            const mlir::Value lastSrc = mlir::arith::AddIOp::create(b, loc, srcStart, bytes);
            const mlir::Value lastDst = mlir::arith::AddIOp::create(b, loc, dstStart, bytes);
            const mlir::Value keep =
                mlir::arith::AndIOp::create(b,
                                            loc,
                                            loadByte(b, loc, dst, lastDst),
                                            mlir::arith::XOrIOp::create(b, loc, mask, constant(b, loc, i8, -1)));
            const mlir::Value take = mlir::arith::AndIOp::create(b, loc, loadByte(b, loc, src, lastSrc), mask);
            storeByte(b, loc, dst, lastDst, mlir::arith::OrIOp::create(b, loc, keep, take));
            mlir::scf::YieldOp::create(b, loc);
        }
        mlir::scf::YieldOp::create(b, loc);
    }
    {
        mlir::OpBuilder::InsertionGuard const inner(b);
        b.setInsertionPointToStart(choice.elseBlock());

        // Ben Dyer's algorithm, as the header carries it: each turn moves whatever is left before
        // the nearer of the two bytes ends, so a run crosses at most one boundary per turn.
        const mlir::Value last = mlir::arith::AddIOp::create(b, loc, srcOff, length);
        auto              loop =
            mlir::scf::WhileOp::create(b, loc, mlir::TypeRange{sizeTy, sizeTy}, mlir::ValueRange{srcOff, dstOff});
        {
            mlir::OpBuilder::InsertionGuard const cond(b);
            mlir::Block* before = b.createBlock(&loop.getBefore(), {}, {sizeTy, sizeTy}, {loc, loc});
            b.setInsertionPointToStart(before);
            mlir::scf::ConditionOp::create(b,
                                           loc,
                                           mlir::arith::CmpIOp::create(b,
                                                                       loc,
                                                                       mlir::arith::CmpIPredicate::ugt,
                                                                       last,
                                                                       before->getArgument(0)),
                                           before->getArguments());
        }
        {
            mlir::OpBuilder::InsertionGuard const body(b);
            mlir::Block* after = b.createBlock(&loop.getAfter(), {}, {sizeTy, sizeTy}, {loc, loc});
            b.setInsertionPointToStart(after);
            const mlir::Value sAt = after->getArgument(0);
            const mlir::Value dAt = after->getArgument(1);

            const mlir::Value sMod = mlir::arith::RemUIOp::create(b, loc, sAt, eight);
            const mlir::Value dMod = mlir::arith::RemUIOp::create(b, loc, dAt, eight);
            const mlir::Value most = mlir::arith::MaxUIOp::create(b, loc, sMod, dMod);
            const mlir::Value size = mlir::arith::MinUIOp::create(b,
                                                                  loc,
                                                                  mlir::arith::SubIOp::create(b, loc, eight, most),
                                                                  mlir::arith::SubIOp::create(b, loc, last, sAt));

            // The mask is built a byte wider than it is used: a run of eight bits would shift a
            // one clean out of a byte, and the header computes it in `unsigned` for that reason.
            const mlir::Value wide = resize(b, loc, size, i32);
            const mlir::Value mask = mlir::arith::TruncIOp::
                create(b,
                       loc,
                       i8,
                       mlir::arith::ShLIOp::create(b,
                                                   loc,
                                                   mlir::arith::SubIOp::create(b,
                                                                               loc,
                                                                               mlir::arith::ShLIOp::create(b,
                                                                                                           loc,
                                                                                                           constant(b,
                                                                                                                    loc,
                                                                                                                    i32,
                                                                                                                    1),
                                                                                                           wide),
                                                                               constant(b, loc, i32, 1)),
                                                   resize(b, loc, dMod, i32)));

            const mlir::Value taken = mlir::arith::ShLIOp::
                create(b,
                       loc,
                       mlir::arith::ShRUIOp::create(b,
                                                    loc,
                                                    loadByte(b,
                                                             loc,
                                                             src,
                                                             mlir::arith::DivUIOp::create(b, loc, sAt, eight)),
                                                    mlir::arith::TruncIOp::create(b, loc, i8, sMod)),
                       mlir::arith::TruncIOp::create(b, loc, i8, dMod));

            const mlir::Value where = mlir::arith::DivUIOp::create(b, loc, dAt, eight);
            const mlir::Value keep =
                mlir::arith::AndIOp::create(b,
                                            loc,
                                            loadByte(b, loc, dst, where),
                                            mlir::arith::XOrIOp::create(b, loc, mask, constant(b, loc, i8, -1)));
            storeByte(b,
                      loc,
                      dst,
                      where,
                      mlir::arith::OrIOp::create(b, loc, keep, mlir::arith::AndIOp::create(b, loc, taken, mask)));

            mlir::scf::YieldOp::create(b,
                                       loc,
                                       mlir::ValueRange{mlir::arith::AddIOp::create(b, loc, sAt, size),
                                                        mlir::arith::AddIOp::create(b, loc, dAt, size)});
        }
        mlir::scf::YieldOp::create(b, loc);
    }
    mlir::LLVM::ReturnOp::create(b, loc, mlir::ValueRange{});
}

/// @brief `get_bits`: a run of bits, with anything past the buffer's end read as zero.
void buildGetBits(mlir::OpBuilder& b, mlir::ModuleOp module, mlir::Type sizeTy)
{
    auto loc = module.getLoc();
    auto i8  = b.getIntegerType(8);
    auto ptr = mlir::LLVM::LLVMPointerType::get(b.getContext());
    auto fn  = declare(b, module, "dsdl_runtime_get_bits", {ptr, ptr, sizeTy, sizeTy, sizeTy}, {});
    mlir::OpBuilder::InsertionGuard const guard(b);
    mlir::Block*                          entry = begin(b, fn);
    if (entry == nullptr)
    {
        return;
    }
    const mlir::Value output = entry->getArgument(0);
    const mlir::Value buffer = entry->getArgument(1);
    const mlir::Value size   = entry->getArgument(2);
    const mlir::Value offset = entry->getArgument(3);
    const mlir::Value length = entry->getArgument(4);

    const mlir::Value sat   = saturate(b, loc, size, offset, length);
    const mlir::Value eight = constant(b, loc, sizeTy, 8);
    const mlir::Value seven = constant(b, loc, sizeTy, 7);

    // Zero from where the readable part ends to the end of what was asked for, so a short
    // buffer reads as zeroes rather than as whatever the caller's slot held.
    const mlir::Value from = mlir::arith::DivUIOp::create(b, loc, sat, eight);
    const mlir::Value until =
        mlir::arith::DivUIOp::create(b, loc, mlir::arith::AddIOp::create(b, loc, length, seven), eight);
    auto idxTy = b.getIndexType();
    auto clear = mlir::scf::ForOp::create(b,
                                          loc,
                                          mlir::arith::IndexCastOp::create(b, loc, idxTy, from),
                                          mlir::arith::IndexCastOp::create(b, loc, idxTy, until),
                                          mlir::arith::ConstantIndexOp::create(b, loc, 1));
    {
        mlir::OpBuilder::InsertionGuard const inner(b);
        b.setInsertionPointToStart(clear.getBody());
        storeByte(b,
                  loc,
                  output,
                  mlir::arith::IndexCastOp::create(b, loc, sizeTy, clear.getInductionVar()),
                  constant(b, loc, i8, 0));
    }
    mlir::LLVM::CallOp::create(b,
                               loc,
                               module.lookupSymbol<mlir::LLVM::LLVMFuncOp>("dsdl_runtime_copy_bits"),
                               mlir::ValueRange{output, constant(b, loc, sizeTy, 0), sat, buffer, offset});
    mlir::LLVM::ReturnOp::create(b, loc, mlir::ValueRange{});
}

/// @brief The buffer-too-small check every writer starts with.
mlir::Value tooSmall(mlir::OpBuilder& b,
                     mlir::Location   loc,
                     mlir::Value      sizeBytes,
                     mlir::Value      offset,
                     mlir::Value      length,
                     const bool       inclusive)
{
    auto              sizeTy   = sizeBytes.getType();
    const mlir::Value sizeBits = mlir::arith::MulIOp::create(b, loc, sizeBytes, constant(b, loc, sizeTy, 8));
    const mlir::Value need     = length ? mlir::arith::AddIOp::create(b, loc, offset, length) : offset;
    return mlir::arith::CmpIOp::create(b,
                                       loc,
                                       inclusive ? mlir::arith::CmpIPredicate::ule : mlir::arith::CmpIPredicate::ult,
                                       sizeBits,
                                       need);
}

/// @brief `set_uxx` and `set_ixx`, which write the same bits.
void buildSetInteger(mlir::OpBuilder& b, mlir::ModuleOp module, mlir::Type sizeTy, llvm::StringRef name)
{
    auto                                  loc = module.getLoc();
    auto                                  i8  = b.getIntegerType(8);
    auto                                  i64 = b.getIntegerType(64);
    auto                                  ptr = mlir::LLVM::LLVMPointerType::get(b.getContext());
    auto                                  fn  = declare(b, module, name, {ptr, sizeTy, sizeTy, i64, i8}, {i8});
    mlir::OpBuilder::InsertionGuard const guard(b);
    mlir::Block*                          entry = begin(b, fn);
    if (entry == nullptr)
    {
        return;
    }
    const mlir::Value buffer = entry->getArgument(0);
    const mlir::Value size   = entry->getArgument(1);
    const mlir::Value offset = entry->getArgument(2);
    const mlir::Value value  = entry->getArgument(3);
    const mlir::Value width  = widen(b, loc, entry->getArgument(4), sizeTy);

    const mlir::Value short_  = tooSmall(b, loc, size, offset, width, false);
    auto              guarded = mlir::scf::IfOp::create(b, loc, mlir::TypeRange{i8}, short_, true, true);
    {
        mlir::OpBuilder::InsertionGuard const inner(b);
        b.setInsertionPointToStart(guarded.thenBlock());
        // -DSDL_RUNTIME_ERROR_SERIALIZATION_BUFFER_TOO_SMALL
        mlir::scf::YieldOp::create(b, loc, mlir::ValueRange{constant(b, loc, i8, -3)});
    }
    {
        mlir::OpBuilder::InsertionGuard const inner(b);
        b.setInsertionPointToStart(guarded.elseBlock());
        const mlir::Value slot = scratch(b, loc, 8);
        disassembleLE(b, loc, slot, value, 8);
        const mlir::Value written = mlir::arith::MinUIOp::create(b, loc, width, constant(b, loc, sizeTy, 64));
        mlir::LLVM::CallOp::create(b,
                                   loc,
                                   module.lookupSymbol<mlir::LLVM::LLVMFuncOp>("dsdl_runtime_copy_bits"),
                                   mlir::ValueRange{buffer, offset, written, slot, constant(b, loc, sizeTy, 0)});
        mlir::scf::YieldOp::create(b, loc, mlir::ValueRange{constant(b, loc, i8, 0)});
    }
    mlir::LLVM::ReturnOp::create(b, loc, mlir::ValueRange{guarded.getResult(0)});
}

/// @brief `set_bit`, the one-bit case.
void buildSetBit(mlir::OpBuilder& b, mlir::ModuleOp module, mlir::Type sizeTy)
{
    auto loc = module.getLoc();
    auto i1  = b.getIntegerType(1);
    auto i8  = b.getIntegerType(8);
    auto ptr = mlir::LLVM::LLVMPointerType::get(b.getContext());
    auto fn  = declare(b, module, "dsdl_runtime_set_bit", {ptr, sizeTy, sizeTy, i1}, {i8});
    mlir::OpBuilder::InsertionGuard const guard(b);
    mlir::Block*                          entry = begin(b, fn);
    if (entry == nullptr)
    {
        return;
    }
    const mlir::Value buffer = entry->getArgument(0);
    const mlir::Value size   = entry->getArgument(1);
    const mlir::Value offset = entry->getArgument(2);
    const mlir::Value value  = entry->getArgument(3);

    const mlir::Value short_  = tooSmall(b, loc, size, offset, mlir::Value{}, true);
    auto              guarded = mlir::scf::IfOp::create(b, loc, mlir::TypeRange{i8}, short_, true, true);
    {
        mlir::OpBuilder::InsertionGuard const inner(b);
        b.setInsertionPointToStart(guarded.thenBlock());
        mlir::scf::YieldOp::create(b, loc, mlir::ValueRange{constant(b, loc, i8, -3)});
    }
    {
        mlir::OpBuilder::InsertionGuard const inner(b);
        b.setInsertionPointToStart(guarded.elseBlock());
        const mlir::Value slot = scratch(b, loc, 1);
        storeByte(b,
                  loc,
                  slot,
                  mlir::LLVM::ConstantOp::create(b, loc, b.getIntegerType(64), b.getI64IntegerAttr(0)),
                  widen(b, loc, value, i8));
        mlir::LLVM::CallOp::create(b,
                                   loc,
                                   module.lookupSymbol<mlir::LLVM::LLVMFuncOp>("dsdl_runtime_copy_bits"),
                                   mlir::ValueRange{buffer,
                                                    offset,
                                                    constant(b, loc, sizeTy, 1),
                                                    slot,
                                                    constant(b, loc, sizeTy, 0)});
        mlir::scf::YieldOp::create(b, loc, mlir::ValueRange{constant(b, loc, i8, 0)});
    }
    mlir::LLVM::ReturnOp::create(b, loc, mlir::ValueRange{guarded.getResult(0)});
}

/// @brief `get_u8` through `get_u64`: the readable bits, little-endian, zero-extended.
void buildGetUnsigned(mlir::OpBuilder& b, mlir::ModuleOp module, mlir::Type sizeTy, const std::int64_t bits)
{
    auto loc    = module.getLoc();
    auto i8     = b.getIntegerType(8);
    auto result = b.getIntegerType(static_cast<unsigned>(bits));
    auto ptr    = mlir::LLVM::LLVMPointerType::get(b.getContext());
    auto fn     = declare(b, module, "dsdl_runtime_get_u" + std::to_string(bits), {ptr, sizeTy, sizeTy, i8}, {result});
    mlir::OpBuilder::InsertionGuard const guard(b);
    mlir::Block*                          entry = begin(b, fn);
    if (entry == nullptr)
    {
        return;
    }
    const mlir::Value buffer = entry->getArgument(0);
    const mlir::Value size   = entry->getArgument(1);
    const mlir::Value offset = entry->getArgument(2);
    const mlir::Value width  = widen(b, loc, entry->getArgument(3), sizeTy);

    const mlir::Value capped = mlir::arith::MinUIOp::create(b, loc, width, constant(b, loc, sizeTy, bits));
    const mlir::Value taken  = saturate(b, loc, size, offset, capped);
    const mlir::Value slot   = scratch(b, loc, bits / 8);
    mlir::LLVM::CallOp::create(b,
                               loc,
                               module.lookupSymbol<mlir::LLVM::LLVMFuncOp>("dsdl_runtime_copy_bits"),
                               mlir::ValueRange{slot, constant(b, loc, sizeTy, 0), taken, buffer, offset});
    mlir::LLVM::ReturnOp::create(b, loc, mlir::ValueRange{assembleLE(b, loc, slot, bits / 8, result)});
}

/// @brief `get_i8` through `get_i64`: the same bits, sign-extended from the width asked for.
///
/// The sign bit is taken from the requested width rather than the readable one, so a value
/// running past the end of the buffer keeps the sign its declared width gives it.
void buildGetSigned(mlir::OpBuilder& b, mlir::ModuleOp module, mlir::Type sizeTy, const std::int64_t bits)
{
    auto loc    = module.getLoc();
    auto i8     = b.getIntegerType(8);
    auto result = b.getIntegerType(static_cast<unsigned>(bits));
    auto ptr    = mlir::LLVM::LLVMPointerType::get(b.getContext());
    auto fn     = declare(b, module, "dsdl_runtime_get_i" + std::to_string(bits), {ptr, sizeTy, sizeTy, i8}, {result});
    mlir::OpBuilder::InsertionGuard const guard(b);
    mlir::Block*                          entry = begin(b, fn);
    if (entry == nullptr)
    {
        return;
    }
    const mlir::Value width  = entry->getArgument(3);
    const mlir::Value capped = mlir::arith::MinUIOp::create(b, loc, width, constant(b, loc, i8, bits));
    auto unsignedFn          = module.lookupSymbol<mlir::LLVM::LLVMFuncOp>("dsdl_runtime_get_u" + std::to_string(bits));
    const mlir::Value raw    = mlir::LLVM::CallOp::create(b,
                                                          loc,
                                                          unsignedFn,
                                                          mlir::ValueRange{entry->getArgument(0),
                                                                           entry->getArgument(1),
                                                                           entry->getArgument(2),
                                                                           capped})
                                   .getResult();

    // Sign-extend by shifting the sign bit to the top and back down again.
    const mlir::Value wide  = widen(b, loc, capped, result);
    const mlir::Value slack = mlir::arith::SubIOp::create(b, loc, constant(b, loc, result, bits), wide);
    const mlir::Value up    = mlir::arith::ShLIOp::create(b, loc, raw, slack);
    const mlir::Value down  = mlir::arith::ShRSIOp::create(b, loc, up, slack);
    // A zero width shifts by the whole type, which is undefined; there is nothing to sign there.
    const mlir::Value none =
        mlir::arith::CmpIOp::create(b, loc, mlir::arith::CmpIPredicate::eq, wide, constant(b, loc, result, 0));
    mlir::LLVM::ReturnOp::create(b,
                                 loc,
                                 mlir::ValueRange{mlir::arith::SelectOp::create(b, loc, none, raw, down).getResult()});
}

/// @brief `get_bit`, which is the low bit of a one-bit read.
void buildGetBit(mlir::OpBuilder& b, mlir::ModuleOp module, mlir::Type sizeTy)
{
    auto                                  loc = module.getLoc();
    auto                                  i1  = b.getIntegerType(1);
    auto                                  i8  = b.getIntegerType(8);
    auto                                  ptr = mlir::LLVM::LLVMPointerType::get(b.getContext());
    auto                                  fn  = declare(b, module, "dsdl_runtime_get_bit", {ptr, sizeTy, sizeTy}, {i1});
    mlir::OpBuilder::InsertionGuard const guard(b);
    mlir::Block*                          entry = begin(b, fn);
    if (entry == nullptr)
    {
        return;
    }
    const mlir::Value raw =
        mlir::LLVM::CallOp::create(b,
                                   loc,
                                   module.lookupSymbol<mlir::LLVM::LLVMFuncOp>("dsdl_runtime_get_u8"),
                                   mlir::ValueRange{entry->getArgument(0),
                                                    entry->getArgument(1),
                                                    entry->getArgument(2),
                                                    constant(b, loc, i8, 1)})
            .getResult();
    mlir::LLVM::ReturnOp::create(b,
                                 loc,
                                 mlir::ValueRange{mlir::arith::CmpIOp::create(b,
                                                                              loc,
                                                                              mlir::arith::CmpIPredicate::eq,
                                                                              raw,
                                                                              constant(b, loc, i8, 1))
                                                      .getResult()});
}

/// @brief `float16_pack`: the header's own conversion, which is not `fptrunc`.
///
/// It differs from the hardware conversion at the edges -- every NaN becomes one quiet pattern,
/// and an overflowing finite value saturates to the largest binary16 rather than to infinity --
/// so reproducing the arithmetic is what keeps the wire the same.
mlir::Value packFloat16(mlir::OpBuilder& b, mlir::Location loc, mlir::Value value)
{
    auto             i16 = b.getIntegerType(16);
    auto             i32 = b.getIntegerType(32);
    const mlir::Type f32 = mlir::cast<mlir::Type>(b.getF32Type());

    const auto u32     = [&](const std::int64_t v) { return constant(b, loc, i32, v); };
    const auto asFloat = [&](mlir::Value bits) {
        return mlir::arith::BitcastOp::create(b, loc, f32, bits).getResult();
    };
    const auto asBits = [&](mlir::Value real) { return mlir::arith::BitcastOp::create(b, loc, i32, real).getResult(); };

    const mlir::Value roundMask = u32(~static_cast<std::int64_t>(0x0FFF) & 0xFFFFFFFF);
    const mlir::Value f32inf    = u32(255LL << 23);
    const mlir::Value f16inf    = u32(31LL << 23);
    const mlir::Value magic     = u32(15LL << 23);

    const mlir::Value bits = asBits(value);
    const mlir::Value sign = mlir::arith::AndIOp::create(b, loc, bits, u32(1LL << 31));
    const mlir::Value rest = mlir::arith::XOrIOp::create(b, loc, bits, sign);

    const mlir::Value special = mlir::arith::CmpIOp::create(b, loc, mlir::arith::CmpIPredicate::uge, rest, f32inf);
    auto              chosen  = mlir::scf::IfOp::create(b, loc, mlir::TypeRange{i16}, special, true, true);
    {
        mlir::OpBuilder::InsertionGuard const guard(b);
        b.setInsertionPointToStart(chosen.thenBlock());
        // Not a number keeps one pattern; the infinities keep theirs.
        const mlir::Value payload = mlir::arith::AndIOp::create(b, loc, rest, u32(0x7FFFFF));
        const mlir::Value isNaN  = mlir::arith::CmpIOp::create(b, loc, mlir::arith::CmpIPredicate::ne, payload, u32(0));
        const mlir::Value beyond = mlir::arith::CmpIOp::create(b, loc, mlir::arith::CmpIPredicate::ugt, rest, f32inf);
        const mlir::Value edge =
            mlir::arith::SelectOp::create(b, loc, beyond, constant(b, loc, i16, 0x7FFF), constant(b, loc, i16, 0x7C00));
        mlir::scf::YieldOp::create(b,
                                   loc,
                                   mlir::ValueRange{mlir::arith::SelectOp::create(b,
                                                                                  loc,
                                                                                  isNaN,
                                                                                  constant(b, loc, i16, 0x7E00),
                                                                                  edge)});
    }
    {
        mlir::OpBuilder::InsertionGuard const guard(b);
        b.setInsertionPointToStart(chosen.elseBlock());
        const mlir::Value rounded = mlir::arith::AndIOp::create(b, loc, rest, roundMask);
        const mlir::Value scaled  = asBits(mlir::arith::MulFOp::create(b, loc, asFloat(rounded), asFloat(magic)));
        const mlir::Value undone  = mlir::arith::SubIOp::create(b, loc, scaled, roundMask);
        const mlir::Value capped =
            mlir::arith::SelectOp::create(b,
                                          loc,
                                          mlir::arith::CmpIOp::create(b,
                                                                      loc,
                                                                      mlir::arith::CmpIPredicate::ugt,
                                                                      undone,
                                                                      f16inf),
                                          f16inf,
                                          undone);
        mlir::scf::YieldOp::create(b,
                                   loc,
                                   mlir::ValueRange{
                                       mlir::arith::TruncIOp::
                                           create(b, loc, i16, mlir::arith::ShRUIOp::create(b, loc, capped, u32(13)))});
    }
    const mlir::Value signBit =
        mlir::arith::TruncIOp::create(b, loc, i16, mlir::arith::ShRUIOp::create(b, loc, sign, u32(16)));
    return mlir::arith::OrIOp::create(b, loc, chosen.getResult(0), signBit);
}

/// @brief `float16_unpack`, the inverse of @ref packFloat16.
mlir::Value unpackFloat16(mlir::OpBuilder& b, mlir::Location loc, mlir::Value packed)
{
    auto             i32 = b.getIntegerType(32);
    const mlir::Type f32 = mlir::cast<mlir::Type>(b.getF32Type());

    const auto u32     = [&](const std::int64_t v) { return constant(b, loc, i32, v); };
    const auto asFloat = [&](mlir::Value bits) {
        return mlir::arith::BitcastOp::create(b, loc, f32, bits).getResult();
    };
    const auto asBits = [&](mlir::Value real) { return mlir::arith::BitcastOp::create(b, loc, i32, real).getResult(); };

    const mlir::Value wide  = widen(b, loc, packed, i32);
    const mlir::Value magic = u32(0xEFLL << 23);
    const mlir::Value limit = u32(0x8FLL << 23);

    const mlir::Value shifted =
        mlir::arith::ShLIOp::create(b, loc, mlir::arith::AndIOp::create(b, loc, wide, u32(0x7FFF)), u32(13));
    const mlir::Value scaled = mlir::arith::MulFOp::create(b, loc, asFloat(shifted), asFloat(magic));
    const mlir::Value big =
        mlir::arith::CmpFOp::create(b, loc, mlir::arith::CmpFPredicate::OGE, scaled, asFloat(limit));
    const mlir::Value bits =
        mlir::arith::SelectOp::create(b,
                                      loc,
                                      big,
                                      mlir::arith::OrIOp::create(b, loc, asBits(scaled), u32(0xFFLL << 23)),
                                      asBits(scaled));
    const mlir::Value sign =
        mlir::arith::ShLIOp::create(b, loc, mlir::arith::AndIOp::create(b, loc, wide, u32(0x8000)), u32(16));
    return asFloat(mlir::arith::OrIOp::create(b, loc, bits, sign));
}

/// @brief `set_f16`, `set_f32` and `set_f64`, each an integer write of the value's bits.
void buildSetFloat(mlir::OpBuilder& b, mlir::ModuleOp module, mlir::Type sizeTy, const std::int64_t bits)
{
    auto             loc = module.getLoc();
    auto             i8  = b.getIntegerType(8);
    auto             i64 = b.getIntegerType(64);
    auto             ptr = mlir::LLVM::LLVMPointerType::get(b.getContext());
    const mlir::Type realTy =
        (bits == 64) ? mlir::cast<mlir::Type>(b.getF64Type()) : mlir::cast<mlir::Type>(b.getF32Type());
    auto fn = declare(b, module, "dsdl_runtime_set_f" + std::to_string(bits), {ptr, sizeTy, sizeTy, realTy}, {i8});
    mlir::OpBuilder::InsertionGuard const guard(b);
    mlir::Block*                          entry = begin(b, fn);
    if (entry == nullptr)
    {
        return;
    }
    const mlir::Value value = entry->getArgument(3);
    mlir::Value       carried;
    if (bits == 16)
    {
        carried = widen(b, loc, packFloat16(b, loc, value), i64);
    }
    else
    {
        auto intTy = b.getIntegerType(static_cast<unsigned>(bits));
        carried    = widen(b, loc, mlir::arith::BitcastOp::create(b, loc, intTy, value), i64);
    }
    const mlir::Value written =
        mlir::LLVM::CallOp::create(b,
                                   loc,
                                   module.lookupSymbol<mlir::LLVM::LLVMFuncOp>("dsdl_runtime_set_uxx"),
                                   mlir::ValueRange{entry->getArgument(0),
                                                    entry->getArgument(1),
                                                    entry->getArgument(2),
                                                    carried,
                                                    constant(b, loc, i8, bits)})
            .getResult();
    mlir::LLVM::ReturnOp::create(b, loc, mlir::ValueRange{written});
}

/// @brief `get_f16`, `get_f32` and `get_f64`, each an integer read reinterpreted.
void buildGetFloat(mlir::OpBuilder& b, mlir::ModuleOp module, mlir::Type sizeTy, const std::int64_t bits)
{
    auto             loc = module.getLoc();
    auto             i8  = b.getIntegerType(8);
    auto             ptr = mlir::LLVM::LLVMPointerType::get(b.getContext());
    const mlir::Type realTy =
        (bits == 64) ? mlir::cast<mlir::Type>(b.getF64Type()) : mlir::cast<mlir::Type>(b.getF32Type());
    auto fn = declare(b, module, "dsdl_runtime_get_f" + std::to_string(bits), {ptr, sizeTy, sizeTy}, {realTy});
    mlir::OpBuilder::InsertionGuard const guard(b);
    mlir::Block*                          entry = begin(b, fn);
    if (entry == nullptr)
    {
        return;
    }
    const std::int64_t read = (bits == 16) ? 16 : bits;
    mlir::Value const  raw  = mlir::LLVM::CallOp::create(b,
                                                         loc,
                                                         module.lookupSymbol<mlir::LLVM::LLVMFuncOp>(
                                                             "dsdl_runtime_get_u" + std::to_string(read)),
                                                         mlir::ValueRange{entry->getArgument(0),
                                                                          entry->getArgument(1),
                                                                          entry->getArgument(2),
                                                                          constant(b, loc, i8, read)})
                                  .getResult();
    mlir::Value const  out =
        (bits == 16) ? unpackFloat16(b, loc, raw) : mlir::arith::BitcastOp::create(b, loc, realTy, raw).getResult();
    mlir::LLVM::ReturnOp::create(b, loc, mlir::ValueRange{out});
}

struct EmitDSDLRuntimePass : public mlir::PassWrapper<EmitDSDLRuntimePass, mlir::OperationPass<mlir::ModuleOp>>
{
    llvm::StringRef getArgument() const final
    {
        return "emit-dsdl-runtime";
    }
    llvm::StringRef getDescription() const final
    {
        return "Define the serialisation primitives a lowered plan calls";
    }
    void getDependentDialects(mlir::DialectRegistry& registry) const override
    {
        registry.insert<mlir::LLVM::LLVMDialect,
                        mlir::func::FuncDialect,
                        mlir::arith::ArithDialect,
                        mlir::scf::SCFDialect>();
    }

    // NOLINTNEXTLINE(misc-override-with-different-visibility) -- MLIR declares passes this way.
    void runOnOperation() override
    {
        auto module = getOperation();
        // The width the conversion resolved, so a primitive takes its buffer size in the same
        // type the calls to it pass.
        unsigned sizeBits = 64;
        if (const auto recorded = module->getAttrOfType<mlir::IntegerAttr>(kSizeBitsAttr))
        {
            sizeBits = static_cast<unsigned>(recorded.getInt());
        }
        mlir::OpBuilder b(&getContext());
        auto            sizeTy = b.getIntegerType(sizeBits);
        buildCopyBits(b, module, sizeTy);
        buildGetBits(b, module, sizeTy);
        buildSetInteger(b, module, sizeTy, "dsdl_runtime_set_uxx");
        buildSetInteger(b, module, sizeTy, "dsdl_runtime_set_ixx");
        buildSetBit(b, module, sizeTy);
        for (const std::int64_t width : {8, 16, 32, 64})
        {
            buildGetUnsigned(b, module, sizeTy, width);
            buildGetSigned(b, module, sizeTy, width);
        }
        buildGetBit(b, module, sizeTy);
        for (const std::int64_t width : {16, 32, 64})
        {
            buildSetFloat(b, module, sizeTy, width);
            buildGetFloat(b, module, sizeTy, width);
        }
    }
};

}  // namespace

std::unique_ptr<mlir::Pass> createEmitDSDLRuntimePass()
{
    return std::make_unique<EmitDSDLRuntimePass>();
}

void registerEmitDSDLRuntimePass()
{
    static bool once = false;
    if (once)
    {
        return;
    }
    once = true;
    static mlir::PassRegistration<EmitDSDLRuntimePass> const reg;
}

}  // namespace llvmdsdl
