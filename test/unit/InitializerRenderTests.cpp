//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// The initialise-body reader against every body the pipeline builds for the regulated types.
///
/// The reader recognises the operations `build-dsdl-plan-bodies` puts in an initialise body and
/// refuses anything else. Whether it recognises all of them is asked of the whole embedded
/// catalogue: every initialise body of every regulated type must read, and a handful whose shapes
/// are known must read as those shapes.
///
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>

#include <llvm/ADT/StringRef.h>
#include <llvm/Support/Error.h>
#include <mlir/Dialect/Arith/IR/Arith.h>
#include <mlir/Dialect/EmitC/IR/EmitC.h>
#include <mlir/Dialect/Func/IR/FuncOps.h>
#include <mlir/Dialect/SCF/IR/SCF.h>
#include <mlir/IR/BuiltinAttributes.h>
#include <mlir/IR/BuiltinOps.h>
#include <mlir/IR/DialectRegistry.h>
#include <mlir/IR/MLIRContext.h>
#include <mlir/Pass/PassManager.h>
#include <mlir/Support/LLVM.h>

#include "llvmdsdl/CodeGen/InitializerRender.h"
#include "llvmdsdl/CodeGen/UavcanEmbeddedCatalog.h"
#include "llvmdsdl/IR/DSDLDialect.h"
#include "llvmdsdl/Support/Diagnostics.h"
#include "llvmdsdl/Transforms/Passes.h"

#include "UnitTests.h"

namespace
{

using Kind = llvmdsdl::MemberDefault::Kind;

const char* kindName(const Kind kind)
{
    switch (kind)
    {
    case Kind::Scalar:
        return "scalar";
    case Kind::VariableArrayEmpty:
        return "variable-array-empty";
    case Kind::FixedScalarArray:
        return "fixed-scalar-array";
    case Kind::FixedCompositeArray:
        return "fixed-composite-array";
    case Kind::BoolArray:
        return "bool-array";
    case Kind::Composite:
        return "composite";
    case Kind::View:
        return "view";
    }
    return "?";
}

/// @brief The member named @p name in @p shape, or null.
const llvmdsdl::MemberDefault* member(const llvmdsdl::InitializerShape& shape, const llvm::StringRef name)
{
    for (const auto& entry : shape.members)
    {
        if (entry.member == name)
        {
            return &entry;
        }
    }
    return nullptr;
}

bool expectKind(const llvmdsdl::InitializerShape& shape,
                const char* const                 body,
                const llvm::StringRef             name,
                const Kind                        kind,
                const std::int64_t                count = 0)
{
    const auto* found = member(shape, name);
    if (found == nullptr)
    {
        std::cerr << body << ": member '" << name.str() << "' is not set\n";
        return false;
    }
    if (found->kind != kind)
    {
        std::cerr << body << ": member '" << name.str() << "' is " << kindName(found->kind) << ", expected "
                  << kindName(kind) << "\n";
        return false;
    }
    if (found->count != count)
    {
        std::cerr << body << ": member '" << name.str() << "' has count " << found->count << ", expected " << count
                  << "\n";
        return false;
    }
    return true;
}

}  // namespace

bool runInitializerRenderTests()
{
    mlir::DialectRegistry registry;
    registry.insert<mlir::dsdl::DSDLDialect,
                    mlir::func::FuncDialect,
                    mlir::arith::ArithDialect,
                    mlir::scf::SCFDialect,
                    mlir::emitc::EmitCDialect>();
    mlir::MLIRContext context(registry);
    context.getOrLoadDialect<mlir::dsdl::DSDLDialect>();

    llvmdsdl::DiagnosticEngine diagnostics;
    auto                       loaded = llvmdsdl::loadUavcanEmbeddedCatalog(context, diagnostics);
    if (!loaded)
    {
        std::cerr << "failed to load the embedded UAVCAN catalogue\n";
        llvm::consumeError(loaded.takeError());
        return false;
    }
    mlir::ModuleOp module = loaded->module.get();

    mlir::PassManager pm(&context);
    llvmdsdl::addLowerDSDLBodiesPipeline(pm, false);
    if (mlir::failed(pm.run(module)))
    {
        std::cerr << "the bodies pipeline failed over the embedded catalogue\n";
        return false;
    }

    // Totality: every initialise body the pass built is one the reader accepts.
    std::size_t bodies = 0;
    bool        ok     = true;
    for (mlir::func::FuncOp fn : module.getBodyRegion().front().getOps<mlir::func::FuncOp>())
    {
        const auto direction = fn->getAttrOfType<mlir::StringAttr>("llvmdsdl.plan_body");
        if (!direction || direction.getValue() != "initialize")
        {
            continue;
        }
        ++bodies;
        auto shape = llvmdsdl::readInitializer(fn);
        if (!shape)
        {
            std::cerr << fn.getSymName().str() << ": " << llvm::toString(shape.takeError()) << "\n";
            ok = false;
        }
    }
    if (bodies < 150U)
    {
        std::cerr << "expected an initialise body per regulated section; found " << bodies << "\n";
        ok = false;
    }
    if (!ok)
    {
        return false;
    }

    const auto read = [&](const char* const symbol) -> std::optional<llvmdsdl::InitializerShape> {
        auto fn = module.lookupSymbol<mlir::func::FuncOp>(symbol);
        if (!fn)
        {
            std::cerr << symbol << ": no such body\n";
            return std::nullopt;
        }
        auto shape = llvmdsdl::readInitializer(fn);
        if (!shape)
        {
            std::cerr << symbol << ": " << llvm::toString(shape.takeError()) << "\n";
            return std::nullopt;
        }
        return *shape;
    };

    // A structure of scalars and composites.
    {
        const char* const body  = "uavcan_node_Heartbeat_1_0__initialize_ir_";
        const auto        shape = read(body);
        if (!shape || shape->isUnion || shape->members.size() != 4U)
        {
            std::cerr << body << ": expected a structure of four members\n";
            return false;
        }
        ok                 = expectKind(*shape, body, "uptime", Kind::Scalar) && ok;
        ok                 = expectKind(*shape, body, "health", Kind::Composite) && ok;
        ok                 = expectKind(*shape, body, "mode", Kind::Composite) && ok;
        ok                 = expectKind(*shape, body, "vendor_specific_status_code", Kind::Scalar) && ok;
        const auto* uptime = member(*shape, "uptime");
        const auto  zero   = uptime ? mlir::dyn_cast<mlir::IntegerAttr>(uptime->value) : mlir::IntegerAttr{};
        if (!zero || zero.getInt() != 0)
        {
            std::cerr << body << ": uptime is not stored as the integer zero\n";
            ok = false;
        }
        const auto* health = member(*shape, "health");
        if (health && health->callee != "uavcan_node_Health_1_0__initialize_ir_")
        {
            std::cerr << body << ": health initialised through '" << health->callee << "'\n";
            ok = false;
        }
    }

    // A union: the tag, then every arm.
    {
        const char* const body  = "uavcan_register_Value_1_0__initialize_ir_";
        const auto        shape = read(body);
        if (!shape || !shape->isUnion || shape->unionTag != 0 || shape->members.size() != 15U)
        {
            std::cerr << body << ": expected a union of fifteen arms at tag nought\n";
            return false;
        }
        ok = expectKind(*shape, body, "empty", Kind::Composite) && ok;
        ok = expectKind(*shape, body, "real16", Kind::Composite) && ok;
    }

    // A variable-length array, a fixed bool array and a fixed scalar array.
    {
        const auto string = read("uavcan_primitive_String_1_0__initialize_ir_");
        ok                = string && expectKind(*string, "String", "value", Kind::VariableArrayEmpty) && ok;

        const auto list = read("uavcan_node_port_SubjectIDList_1_0__initialize_ir_");
        ok = list && list->isUnion && expectKind(*list, "SubjectIDList", "mask", Kind::BoolArray, 8192) && ok;
        ok = list && expectKind(*list, "SubjectIDList", "sparse_list", Kind::VariableArrayEmpty) && ok;

        const auto info = read("uavcan_node_GetInfo_1_0__response__initialize_ir_");
        ok              = info && expectKind(*info, "GetInfo.Response", "unique_id", Kind::FixedScalarArray, 16) && ok;
    }

    return ok;
}
