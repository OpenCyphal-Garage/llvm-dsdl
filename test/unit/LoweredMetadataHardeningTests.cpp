//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

#include <llvm/ADT/STLExtras.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/ADT/ilist_iterator.h>
#include <llvm/Support/Casting.h>
#include <mlir/Dialect/Arith/IR/Arith.h>
#include <mlir/Dialect/EmitC/IR/EmitC.h>
#include <mlir/Dialect/Func/IR/FuncOps.h>
#include <mlir/Dialect/SCF/IR/SCF.h>
#include <mlir/IR/Attributes.h>
#include <mlir/IR/DialectRegistry.h>
#include <mlir/IR/Operation.h>
#include <mlir/IR/OperationSupport.h>
#include <mlir/IR/OwningOpRef.h>
#include <mlir/IR/Visitors.h>
#include <mlir/Support/LLVM.h>
#include <array>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include <system_error>

#include "llvmdsdl/CodeGen/CEmitter.h"
#include "llvmdsdl/CodeGen/MlirLoweredFacts.h"
#include "llvmdsdl/Frontend/Lexer.h"
#include "llvmdsdl/Frontend/Parser.h"
#include "llvmdsdl/Lowering/LowerToMLIR.h"
#include "llvmdsdl/Semantics/Analyzer.h"
#include "llvmdsdl/Support/Diagnostics.h"
#include "llvmdsdl/IR/DSDLDialect.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/MLIRContext.h"
#include "llvm/Support/Error.h"
#include "llvmdsdl/Frontend/AST.h"
#include "llvmdsdl/Semantics/Model.h"

#include "UnitTests.h"

namespace
{

std::optional<llvmdsdl::ParsedDefinition> parseFixtureDefinition(const std::string& fileName,
                                                                 const std::string& fullName,
                                                                 const std::string& shortName,
                                                                 const std::string& text)
{
    llvmdsdl::DiagnosticEngine parseDiagnostics;
    llvmdsdl::Lexer            lexer(fileName, text);
    auto                       tokens = lexer.lex();
    llvmdsdl::Parser           parser(fileName, std::move(tokens), parseDiagnostics);
    auto                       ast = parser.parseDefinition();
    if (!ast || parseDiagnostics.hasErrors())
    {
        if (ast)
        {
            llvm::consumeError(ast.takeError());
        }
        return std::nullopt;
    }

    llvmdsdl::DiscoveredDefinition discovered;
    discovered.filePath            = fileName;
    discovered.rootNamespacePath   = "demo";
    discovered.fullName            = fullName;
    discovered.shortName           = shortName;
    discovered.namespaceComponents = {"demo"};
    discovered.majorVersion        = 1;
    discovered.minorVersion        = 0;
    discovered.text                = text;
    return llvmdsdl::ParsedDefinition{std::move(discovered), *ast};
}

std::optional<llvmdsdl::SemanticModule> buildSemanticFixture()
{
    const std::string innerText = "uint8[<=3] bytes\n"
                                  "@sealed\n";
    const std::string outerText = "demo.Inner.1.0 inner\n"
                                  "uint16 value\n"
                                  "@sealed\n";

    auto inner = parseFixtureDefinition("demo/Inner.1.0.dsdl", "demo.Inner", "Inner", innerText);
    auto outer = parseFixtureDefinition("demo/Outer.1.0.dsdl", "demo.Outer", "Outer", outerText);
    if (!inner || !outer)
    {
        return std::nullopt;
    }

    llvmdsdl::ASTModule module;
    module.definitions.push_back(*inner);
    module.definitions.push_back(*outer);

    llvmdsdl::DiagnosticEngine semDiagnostics;
    auto                       semantic = llvmdsdl::analyze(module, semDiagnostics);
    if (!semantic || semDiagnostics.hasErrors())
    {
        if (semantic)
        {
            llvm::consumeError(semantic.takeError());
        }
        return std::nullopt;
    }

    return *semantic;
}

std::optional<mlir::OwningOpRef<mlir::ModuleOp>> lowerFixture(llvmdsdl::DiagnosticEngine& diagnostics,
                                                              mlir::MLIRContext&          context)
{
    auto semantic = buildSemanticFixture();
    if (!semantic)
    {
        return std::nullopt;
    }

    auto lowered = llvmdsdl::lowerToMLIR(*semantic, context, diagnostics);
    if (!lowered)
    {
        return std::nullopt;
    }
    return lowered;
}

bool hasDiagnosticContaining(const llvmdsdl::DiagnosticEngine& diagnostics, const std::string& needle)
{
    for (const auto& d : diagnostics.diagnostics())
    {
        if (d.message.contains(needle))
        {
            return true;
        }
    }
    return false;
}

bool removeFirstIoAttribute(mlir::ModuleOp module, const std::string& attrName)
{
    bool removed = false;
    module.walk([&](mlir::Operation* op) {
        if (removed)
        {
            return;
        }
        if (op->getName().getStringRef() != "dsdl.io")
        {
            return;
        }
        if (!op->getAttr(attrName))
        {
            return;
        }
        op->removeAttr(attrName);
        removed = true;
    });
    return removed;
}

bool removeFirstSchemaAttribute(mlir::ModuleOp module, const std::string& attrName)
{
    bool removed = false;
    module.walk([&](mlir::Operation* op) {
        if (removed)
        {
            return;
        }
        if (op->getName().getStringRef() != "dsdl.schema")
        {
            return;
        }
        if (!op->getAttr(attrName))
        {
            return;
        }
        op->removeAttr(attrName);
        removed = true;
    });
    return removed;
}

bool expectFactsCollectionFailure(llvmdsdl::SemanticModule& semantic,
                                  mlir::ModuleOp            module,
                                  const std::string&        backendLabel,
                                  const std::string&        expectedDiagnostic)
{
    llvmdsdl::LoweredFactsMap  facts;
    llvmdsdl::DiagnosticEngine diagnostics;
    const bool ok = llvmdsdl::collectLoweredFactsFromMlir(semantic, module, diagnostics, backendLabel, &facts, false);
    if (ok)
    {
        std::cerr << backendLabel << " facts collection unexpectedly succeeded\n";
        return false;
    }
    if (!diagnostics.hasErrors())
    {
        std::cerr << backendLabel << " facts collection failed without diagnostics\n";
        return false;
    }
    if (!hasDiagnosticContaining(diagnostics, expectedDiagnostic))
    {
        std::cerr << backendLabel << " diagnostics did not contain expected message fragment: " << expectedDiagnostic
                  << "\n";
        return false;
    }
    return true;
}

bool expectCEmitterFailure(llvmdsdl::SemanticModule& semantic,
                           mlir::ModuleOp            module,
                           const std::string&        expectedDiagnostic)
{
    llvmdsdl::DiagnosticEngine diagnostics;
    llvmdsdl::CEmitOptions     options;
    options.outDir = (std::filesystem::temp_directory_path() / "llvmdsdl-lowered-metadata-c-emitter").string();

    std::error_code fsError;
    std::filesystem::remove_all(options.outDir, fsError);

    llvm::Error err = llvmdsdl::emitC(semantic, module, options, diagnostics);
    if (!err)
    {
        std::cerr << "C emission unexpectedly succeeded on malformed metadata fixture\n";
        return false;
    }
    llvm::consumeError(std::move(err));

    if (!diagnostics.hasErrors())
    {
        std::cerr << "C emission failed without diagnostics\n";
        return false;
    }
    if (!hasDiagnosticContaining(diagnostics, expectedDiagnostic))
    {
        std::cerr << "C emission diagnostics missing expected fragment: " << expectedDiagnostic << "\n";
        return false;
    }
    return true;
}

bool runLoweringMetadataFamilyTests(mlir::MLIRContext& context)
{
    llvmdsdl::DiagnosticEngine lowerDiagnostics;
    auto                       loweredModule = lowerFixture(lowerDiagnostics, context);
    if (!loweredModule || lowerDiagnostics.hasErrors())
    {
        std::cerr << "failed to build lowering fixture module for metadata hardening tests\n";
        return false;
    }

    auto semantic = buildSemanticFixture();
    if (!semantic)
    {
        std::cerr << "failed to build semantic fixture for metadata hardening tests\n";
        return false;
    }

    auto malformed = mlir::OwningOpRef<mlir::ModuleOp>(mlir::cast<mlir::ModuleOp>((*loweredModule)->clone()));
    if (!removeFirstIoAttribute(*malformed, "array_kind"))
    {
        std::cerr << "failed to mutate fixture: no dsdl.io array_kind attribute found\n";
        return false;
    }

    const std::array<std::string, 4> backendLabels = {"C++", "Rust", "Go", "TypeScript"};
    for (const auto& backend : backendLabels)
    {
        auto perBackend = mlir::OwningOpRef<mlir::ModuleOp>(mlir::cast<mlir::ModuleOp>((*malformed)->clone()));
        if (!expectFactsCollectionFailure(*semantic,
                                          *perBackend,
                                          backend,
                                          "failed to run lower-dsdl-exec for " + backend + " backend validation"))
        {
            return false;
        }
    }

    auto cModule = mlir::OwningOpRef<mlir::ModuleOp>(mlir::cast<mlir::ModuleOp>((*malformed)->clone()));
    return expectCEmitterFailure(*semantic, *cModule, "MLIR schema coverage validation failed for C emission");
}

bool runSchemaIdentityFamilyTests(mlir::MLIRContext& context)
{
    llvmdsdl::DiagnosticEngine lowerDiagnostics;
    auto                       loweredModule = lowerFixture(lowerDiagnostics, context);
    if (!loweredModule || lowerDiagnostics.hasErrors())
    {
        std::cerr << "failed to build lowering fixture module for schema identity tests\n";
        return false;
    }

    auto semantic = buildSemanticFixture();
    if (!semantic)
    {
        std::cerr << "failed to build semantic fixture for schema identity tests\n";
        return false;
    }

    auto malformed = mlir::OwningOpRef<mlir::ModuleOp>(mlir::cast<mlir::ModuleOp>((*loweredModule)->clone()));
    if (!removeFirstSchemaAttribute(*malformed, "full_name"))
    {
        std::cerr << "failed to mutate fixture: no dsdl.schema full_name attribute found\n";
        return false;
    }

    const std::array<std::string, 4> backendLabels = {"C++", "Rust", "Go", "TypeScript"};
    for (const auto& backend : backendLabels)
    {
        auto perBackend = mlir::OwningOpRef<mlir::ModuleOp>(mlir::cast<mlir::ModuleOp>((*malformed)->clone()));
        if (!expectFactsCollectionFailure(*semantic,
                                          *perBackend,
                                          backend,
                                          "failed to run lower-dsdl-exec for " + backend + " backend validation"))
        {
            return false;
        }
    }
    return true;
}

}  // namespace

bool runLoweredMetadataHardeningTests()
{
    mlir::DialectRegistry registry;
    registry.insert<mlir::dsdl::DSDLDialect,
                    mlir::func::FuncDialect,
                    mlir::arith::ArithDialect,
                    mlir::scf::SCFDialect,
                    mlir::emitc::EmitCDialect>();

    mlir::MLIRContext context(registry);
    context.getOrLoadDialect<mlir::dsdl::DSDLDialect>();
    context.getOrLoadDialect<mlir::func::FuncDialect>();
    context.getOrLoadDialect<mlir::arith::ArithDialect>();
    context.getOrLoadDialect<mlir::scf::SCFDialect>();
    context.getOrLoadDialect<mlir::emitc::EmitCDialect>();

    if (!runLoweringMetadataFamilyTests(context))
    {
        return false;
    }
    if (!runSchemaIdentityFamilyTests(context))
    {
        return false;
    }

    return true;
}
