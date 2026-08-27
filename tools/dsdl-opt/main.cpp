//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Entry point for the `dsdl-opt` MLIR pass driver.
///
/// This binary registers the DSDL dialect and project pass pipeline, then
/// delegates command-line execution to `mlir-opt` infrastructure.
///
//===----------------------------------------------------------------------===//

#include <llvm/Support/LogicalResult.h>
#include <llvm/ADT/StringRef.h>
#include <mlir/Dialect/Arith/IR/Arith.h>
#include <mlir/Dialect/EmitC/IR/EmitC.h>
#include <mlir/Dialect/Func/IR/FuncOps.h>
#include <mlir/Dialect/SCF/IR/SCF.h>
#include <mlir/IR/DialectRegistry.h>

#include "llvmdsdl/IR/DSDLDialect.h"
#include "llvmdsdl/Transforms/Passes.h"
#include "llvmdsdl/Version.h"
#include "mlir/Tools/mlir-opt/MlirOptMain.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/raw_ostream.h"

/// @brief Program entry point for `dsdl-opt`.
///
/// @param[in] argc Argument count.
/// @param[in] argv Argument vector.
/// @return Zero when MLIR optimization driver exits successfully; non-zero on
///         option parsing or pass pipeline failure.
int main(int argc, char** argv)
{
    llvm::InitLLVM const y(argc, argv);
    for (int i = 1; i < argc; ++i)
    {
        const llvm::StringRef arg(argv[i]);
        if (arg == "--version" || arg == "-V")
        {
            llvm::outs() << "dsdl-opt " << llvmdsdl::kVersionString << "\n";
            return 0;
        }
    }

    mlir::DialectRegistry registry;
    registry.insert<mlir::dsdl::DSDLDialect,
                    mlir::func::FuncDialect,
                    mlir::arith::ArithDialect,
                    mlir::scf::SCFDialect,
                    mlir::emitc::EmitCDialect>();

    llvmdsdl::registerDSDLPasses();

    return failed(mlir::MlirOptMain(argc, argv, "dsdl-opt", registry));
}
