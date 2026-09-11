//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Backend-contract gates: do a backend's serialisation bodies follow the plan operations?
///
/// One backend per run. The fixture namespace is analysed once; every row lowers it afresh, perturbs
/// either the MLIR module or the semantic module, generates with the backend, and compares the bodies
/// with a baseline. DESIGN.md, *Backend Contract*, defines the rows and their verdicts.
///
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <llvm/ADT/StringRef.h>
#include <llvm/Support/Error.h>
#include <mlir/Dialect/Arith/IR/Arith.h>
#include <mlir/Dialect/EmitC/IR/EmitC.h>
#include <mlir/Dialect/Func/IR/FuncOps.h>
#include <mlir/Dialect/SCF/IR/SCF.h>
#include <mlir/IR/Builders.h>
#include <mlir/IR/BuiltinOps.h>
#include <mlir/IR/DialectRegistry.h>
#include <mlir/IR/MLIRContext.h>
#include <mlir/IR/OwningOpRef.h>
#include <mlir/Pass/PassManager.h>
#include <mlir/Support/LLVM.h>

#include "llvmdsdl/Transforms/Passes.h"
#include "llvmdsdl/CodeGen/emitter/C.h"
#include "llvmdsdl/CodeGen/emitter/Cpp.h"
#include "llvmdsdl/CodeGen/emitter/Go.h"
#include "llvmdsdl/CodeGen/emitter/Python.h"
#include "llvmdsdl/CodeGen/emitter/Rust.h"
#include "llvmdsdl/CodeGen/emitter/Ts.h"
#include "llvmdsdl/Frontend/AST.h"
#include "llvmdsdl/Frontend/Parser.h"
#include "llvmdsdl/IR/DSDLDialect.h"
#include "llvmdsdl/IR/DSDLOps.h"
#include "llvmdsdl/Lowering/LowerToMLIR.h"
#include "llvmdsdl/Semantics/Analyzer.h"
#include "llvmdsdl/Semantics/Model.h"
#include "llvmdsdl/Support/Diagnostics.h"

namespace
{

namespace fs = std::filesystem;

constexpr const char* kFixtureType  = "contract.Scalars";
constexpr const char* kSequenceType = "contract.Sequence";
constexpr const char* kChoiceType   = "contract.Choice";

enum class Verdict : std::uint8_t
{
    Pass,
    Gap,
    Error,
};

const char* verdictName(const Verdict verdict)
{
    switch (verdict)
    {
    case Verdict::Pass:
        return "PASS";
    case Verdict::Gap:
        return "GAP";
    default:
        return "ERROR";
    }
}

struct RowResult final
{
    std::string gate;
    std::string row;
    Verdict     verdict;
    std::string detail;
};

struct Arguments final
{
    std::string backend;
    fs::path    namespaceRoot;
    fs::path    workDir;
    std::string targetTriple;
    bool        strict{false};
};

std::optional<Arguments> parseArguments(const int argc, char** argv)
{
    Arguments args;
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg   = argv[i];
        const auto        value = [&]() -> std::optional<std::string> {
            if (i + 1 >= argc)
            {
                return std::nullopt;
            }
            return std::string(argv[++i]);
        };
        if (arg == "--strict")
        {
            args.strict = true;
        }
        else if (arg == "--backend")
        {
            if (auto v = value())
            {
                args.backend = *v;
            }
        }
        else if (arg == "--namespace-root")
        {
            if (auto v = value())
            {
                args.namespaceRoot = fs::path(*v);
            }
        }
        else if (arg == "--work-dir")
        {
            if (auto v = value())
            {
                args.workDir = fs::path(*v);
            }
        }
        else if (arg == "--target-triple")
        {
            if (auto v = value())
            {
                args.targetTriple = *v;
            }
        }
        else
        {
            std::cerr << "unknown argument: " << arg << "\n";
            return std::nullopt;
        }
    }
    if (args.backend.empty() || args.namespaceRoot.empty() || args.workDir.empty())
    {
        std::cerr << "usage: llvmdsdl-backend-contract-tool --backend <c|obj|cpp|rust|go|ts|python> "
                     "--namespace-root <dir> --work-dir <dir> [--target-triple <triple>] [--strict]\n";
        return std::nullopt;
    }
    return args;
}

void printErrors(const llvmdsdl::DiagnosticEngine& diagnostics)
{
    for (const auto& d : diagnostics.diagnostics())
    {
        if (d.level == llvmdsdl::DiagnosticLevel::Error)
        {
            std::cerr << "  " << d.location.file << ":" << d.location.line << ":" << d.location.column << ": "
                      << d.message << "\n";
        }
    }
}

// --- generation ---------------------------------------------------------------------------------

llvm::Error emitWith(const std::string&              backend,
                     const llvmdsdl::SemanticModule& semantic,
                     mlir::ModuleOp                  module,
                     const fs::path&                 outDir,
                     const std::string&              targetTriple,
                     llvmdsdl::DiagnosticEngine&     diagnostics)
{
    fs::remove_all(outDir);
    fs::create_directories(outDir);
    if (backend == "c" || backend == "obj")
    {
        llvmdsdl::emitter::c::Options options;
        options.outDir = outDir.string();
        if (backend == "obj")
        {
            options.artifact     = llvmdsdl::emitter::c::Artifact::Object;
            options.targetTriple = targetTriple;
            return llvmdsdl::emitter::c::emitObject(semantic, module, options, diagnostics);
        }
        return llvmdsdl::emitter::c::emit(semantic, module, options, diagnostics);
    }
    if (backend == "cpp")
    {
        llvmdsdl::emitter::cpp::Options options;
        options.outDir = outDir.string();
        return llvmdsdl::emitter::cpp::emit(semantic, module, options, diagnostics);
    }
    if (backend == "rust")
    {
        llvmdsdl::emitter::rust::Options options;
        options.outDir = outDir.string();
        return llvmdsdl::emitter::rust::emit(semantic, module, options, diagnostics);
    }
    if (backend == "go")
    {
        llvmdsdl::emitter::go::Options options;
        options.outDir = outDir.string();
        return llvmdsdl::emitter::go::emit(semantic, module, options, diagnostics);
    }
    if (backend == "ts")
    {
        llvmdsdl::emitter::ts::Options options;
        options.outDir = outDir.string();
        return llvmdsdl::emitter::ts::emit(semantic, module, options, diagnostics);
    }
    if (backend == "python")
    {
        llvmdsdl::emitter::python::Options options;
        options.outDir = outDir.string();
        return llvmdsdl::emitter::python::emit(semantic, module, options, diagnostics);
    }
    return llvm::createStringError(llvm::inconvertibleErrorCode(), "unknown backend: " + backend);
}

// --- comparison ---------------------------------------------------------------------------------

bool isBinary(const fs::path& path)
{
    const auto ext = path.extension().string();
    return ext == ".o" || ext == ".a" || ext == ".obj";
}

/// Files whose contents carry the backend's bodies. Headers and declarations are outside the body
/// contract for the C-family lanes; the other backends keep declarations and bodies in one file.
bool inScope(const std::string& backend, const fs::path& relative)
{
    const auto ext = relative.extension().string();
    if (backend == "c")
    {
        return ext == ".c";
    }
    if (backend == "obj")
    {
        return isBinary(relative);
    }
    return true;
}

bool isCommentLine(const std::string& line)
{
    const auto first = line.find_first_not_of(" \t\r");
    if (first == std::string::npos)
    {
        return true;
    }
    const auto rest = line.substr(first);
    return rest.starts_with("//") || rest.starts_with('#') || rest.starts_with("/*") || rest.starts_with('*');
}

/// Comment-only and blank lines are dropped before comparing text, so a declaration comment that
/// restates a DSDL type cannot register as a body change.
std::string normalisedContents(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    std::string   contents((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (isBinary(path))
    {
        return contents;
    }
    std::string        out;
    std::istringstream lines(contents);
    for (std::string line; std::getline(lines, line);)
    {
        if (!isCommentLine(line))
        {
            out += line;
            out += '\n';
        }
    }
    return out;
}

std::map<fs::path, std::string> snapshot(const std::string& backend, const fs::path& root)
{
    std::map<fs::path, std::string> files;
    if (!fs::exists(root))
    {
        return files;
    }
    for (const auto& entry : fs::recursive_directory_iterator(root))
    {
        if (!entry.is_regular_file())
        {
            continue;
        }
        const auto relative = fs::relative(entry.path(), root);
        if (inScope(backend, relative))
        {
            files.emplace(relative, normalisedContents(entry.path()));
        }
    }
    return files;
}

/// Relative paths whose in-scope contents differ between two output trees, including files present
/// in only one of them.
std::vector<std::string> differingFiles(const std::map<fs::path, std::string>& a,
                                        const std::map<fs::path, std::string>& b)
{
    std::vector<std::string> out;
    for (const auto& [path, contents] : a)
    {
        const auto it = b.find(path);
        if (it == b.end() || it->second != contents)
        {
            out.push_back(path.string());
        }
    }
    for (const auto& [path, contents] : b)
    {
        if (!a.contains(path))
        {
            out.push_back(path.string());
        }
    }
    std::ranges::sort(out);
    return out;
}

// --- perturbations ------------------------------------------------------------------------------

mlir::dsdl::SerializationPlanOp fixturePlan(mlir::ModuleOp module, const llvm::StringRef fullName = kFixtureType)
{
    mlir::dsdl::SerializationPlanOp found;
    module->walk([&](mlir::dsdl::SchemaOp schema) {
        if (schema.getFullName() != fullName)
        {
            return;
        }
        schema->walk([&](mlir::dsdl::SerializationPlanOp plan) {
            if (!found)
            {
                found = plan;
            }
        });
    });
    return found;
}

mlir::dsdl::IOOp fieldOp(mlir::dsdl::SerializationPlanOp plan, llvm::StringRef name)
{
    mlir::dsdl::IOOp found;
    plan->walk([&](mlir::dsdl::IOOp io) {
        if (!found && io.getName() == name)
        {
            found = io;
        }
    });
    return found;
}

mlir::dsdl::IOOp paddingOp(mlir::dsdl::SerializationPlanOp plan)
{
    mlir::dsdl::IOOp found;
    plan->walk([&](mlir::dsdl::IOOp io) {
        if (!found && io.getKind() == "padding")
        {
            found = io;
        }
    });
    return found;
}

void setWidth(mlir::dsdl::IOOp io, const std::int64_t bits)
{
    mlir::OpBuilder builder(io.getContext());
    io.setBitLengthAttr(builder.getI64IntegerAttr(bits));
    io.setMinBitsAttr(builder.getI64IntegerAttr(bits));
    io.setMaxBitsAttr(builder.getI64IntegerAttr(bits));
}

using IrPerturbation = std::function<bool(mlir::ModuleOp)>;

struct IrRow final
{
    std::string    name;
    IrPerturbation apply;
};

/// Each scalar row moves bits between one operation class and the padding so the plan's serialised
/// size is unchanged; the array and union rows narrow one element or option. Only operations differ.
std::vector<IrRow> irRows()
{
    return {
        {"array-element-width",
         [](mlir::ModuleOp module) {
             auto plan  = fixturePlan(module, kSequenceType);
             auto items = plan ? fieldOp(plan, "items") : mlir::dsdl::IOOp{};
             if (!items || items.getBitLength() < 2)
             {
                 return false;
             }
             mlir::OpBuilder builder(items.getContext());
             items.setBitLengthAttr(builder.getI64IntegerAttr(items.getBitLength() - 1));
             return true;
         }},
        {"union-option-width",
         [](mlir::ModuleOp module) {
             auto plan  = fixturePlan(module, kChoiceType);
             auto small = plan ? fieldOp(plan, "small") : mlir::dsdl::IOOp{};
             if (!small || small.getBitLength() < 2)
             {
                 return false;
             }
             setWidth(small, small.getBitLength() - 1);
             return true;
         }},
        {"unsigned-and-signed-width",
         [](mlir::ModuleOp module) {
             auto plan = fixturePlan(module);
             auto a    = plan ? fieldOp(plan, "a") : mlir::dsdl::IOOp{};
             auto b    = plan ? fieldOp(plan, "b") : mlir::dsdl::IOOp{};
             if (!a || !b)
             {
                 return false;
             }
             setWidth(a, a.getBitLength() + 1);
             setWidth(b, b.getBitLength() - 1);
             return true;
         }},
        {"float-width",
         [](mlir::ModuleOp module) {
             auto plan = fixturePlan(module);
             auto c    = plan ? fieldOp(plan, "c") : mlir::dsdl::IOOp{};
             auto pad  = plan ? paddingOp(plan) : mlir::dsdl::IOOp{};
             if (!c || !pad || c.getBitLength() != 16 || pad.getBitLength() < 17)
             {
                 return false;
             }
             setWidth(c, 32);
             setWidth(pad, pad.getBitLength() - 16);
             return true;
         }},
        {"alignment",
         [](mlir::ModuleOp module) {
             auto plan = fixturePlan(module);
             auto a    = plan ? fieldOp(plan, "a") : mlir::dsdl::IOOp{};
             auto b    = plan ? fieldOp(plan, "b") : mlir::dsdl::IOOp{};
             auto pad  = plan ? paddingOp(plan) : mlir::dsdl::IOOp{};
             if (!a || !b || !pad)
             {
                 return false;
             }
             // Byte-aligning `b` after `a`'s bits pads to the next byte boundary.
             const std::int64_t padding = 8 - (a.getBitLength() % 8);
             if (padding == 8 || pad.getBitLength() <= padding)
             {
                 return false;
             }
             mlir::OpBuilder builder(b.getContext());
             b.setAlignmentBitsAttr(builder.getI64IntegerAttr(8));
             setWidth(pad, pad.getBitLength() - padding);
             return true;
         }},
    };
}

using ModelPerturbation = std::function<bool(llvmdsdl::SemanticModule&)>;

struct ModelRow final
{
    std::string       name;
    ModelPerturbation apply;
};

llvmdsdl::SemanticField* modelField(llvmdsdl::SemanticModule& semantic, const std::string& name)
{
    for (auto& def : semantic.definitions)
    {
        if (def.info.fullName != kFixtureType)
        {
            continue;
        }
        for (auto& field : def.request.fields)
        {
            if (field.name == name)
            {
                return &field;
            }
        }
    }
    return nullptr;
}

/// Each row changes a wire fact in the semantic module that no declaration depends on.
std::vector<ModelRow> modelRows()
{
    return {
        {"cast-mode",
         [](llvmdsdl::SemanticModule& semantic) {
             auto* a = modelField(semantic, "a");
             if (a == nullptr)
             {
                 return false;
             }
             a->resolvedType.castMode = (a->resolvedType.castMode == llvmdsdl::CastMode::Truncated)
                                            ? llvmdsdl::CastMode::Saturated
                                            : llvmdsdl::CastMode::Truncated;
             return true;
         }},
        {"field-widths",
         [](llvmdsdl::SemanticModule& semantic) {
             auto* a = modelField(semantic, "a");
             auto* b = modelField(semantic, "b");
             if (a == nullptr || b == nullptr)
             {
                 return false;
             }
             a->resolvedType.bitLength += 1;
             b->resolvedType.bitLength -= 1;
             return true;
         }},
    };
}

// --- driver -------------------------------------------------------------------------------------

struct Session final
{
    Arguments                  args;
    mlir::MLIRContext&         context;
    const llvmdsdl::ASTModule& ast;
    std::vector<RowResult>     results;

    std::optional<llvmdsdl::SemanticModule> analyse()
    {
        llvmdsdl::DiagnosticEngine diagnostics;
        auto                       semantic = llvmdsdl::analyze(ast, diagnostics);
        if (!semantic)
        {
            llvm::consumeError(semantic.takeError());
            printErrors(diagnostics);
            return std::nullopt;
        }
        return std::move(*semantic);
    }

    mlir::OwningOpRef<mlir::ModuleOp> lower(const llvmdsdl::SemanticModule& semantic)
    {
        llvmdsdl::DiagnosticEngine diagnostics;
        auto                       module = llvmdsdl::lowerToMLIR(semantic, context, diagnostics);
        if (!module)
        {
            printErrors(diagnostics);
        }
        return module;
    }

    /// Runs the pipeline every backend's bodies are translations of, as dsdlc does before emission.
    static bool lowerBodies(mlir::ModuleOp module)
    {
        mlir::PassManager pm(module.getContext());
        llvmdsdl::addLowerDSDLBodiesPipeline(pm, false);
        return mlir::succeeded(pm.run(module));
    }

    /// Generates into `work-dir/<name>`; the snapshot of in-scope files, or nullopt on failure.
    std::optional<std::map<fs::path, std::string>> generate(const std::string&              name,
                                                            const llvmdsdl::SemanticModule& semantic,
                                                            mlir::ModuleOp                  module) const
    {
        llvmdsdl::DiagnosticEngine diagnostics;
        const fs::path             outDir = args.workDir / name;
        if (auto err = emitWith(args.backend, semantic, module, outDir, args.targetTriple, diagnostics))
        {
            std::cerr << "  " << args.backend << " failed on " << name << ": " << llvm::toString(std::move(err))
                      << "\n";
            printErrors(diagnostics);
            return std::nullopt;
        }
        return snapshot(args.backend, outDir);
    }

    void record(std::string gate, std::string row, Verdict verdict, std::string detail)
    {
        results.push_back({std::move(gate), std::move(row), verdict, std::move(detail)});
    }

    bool run()
    {
        auto semantic = analyse();
        if (!semantic)
        {
            return false;
        }
        auto baselineModule = lower(*semantic);
        if (!baselineModule || !lowerBodies(*baselineModule))
        {
            return false;
        }
        const auto baseline = generate("baseline", *semantic, *baselineModule);
        if (!baseline)
        {
            record("determinism", "baseline", Verdict::Error, "baseline generation failed");
            return true;
        }
        if (baseline->empty())
        {
            record("determinism", "baseline", Verdict::Error, "baseline produced no in-scope files");
            return true;
        }

        // Determinism: the same inputs into a different directory.
        {
            auto module = lower(*semantic);
            auto repeat =
                (module && lowerBodies(*module)) ? generate("baseline-repeat", *semantic, *module) : std::nullopt;
            if (!repeat)
            {
                record("determinism", "repeat", Verdict::Error, "generation failed");
            }
            else
            {
                const auto diff = differingFiles(*baseline, *repeat);
                record("determinism",
                       "repeat",
                       diff.empty() ? Verdict::Pass : Verdict::Error,
                       diff.empty() ? "identical" : "differs: " + diff.front());
            }
        }

        // Operation reflection: perturb the operations, hold the model.
        for (const auto& row : irRows())
        {
            auto module = lower(*semantic);
            if (!module)
            {
                record("operation-reflection", row.name, Verdict::Error, "lowering failed");
                continue;
            }
            if (!row.apply(*module))
            {
                record("operation-reflection", row.name, Verdict::Error, "fixture does not fit the row");
                continue;
            }
            if (!lowerBodies(*module))
            {
                record("operation-reflection", row.name, Verdict::Error, "lowering failed on perturbed operations");
                continue;
            }
            const auto generated = generate("ir-" + row.name, *semantic, *module);
            if (!generated)
            {
                record("operation-reflection", row.name, Verdict::Error, "generation failed on perturbed operations");
                continue;
            }
            const auto diff = differingFiles(*baseline, *generated);
            record("operation-reflection",
                   row.name,
                   diff.empty() ? Verdict::Gap : Verdict::Pass,
                   diff.empty() ? "bodies identical to baseline" : "bodies changed: " + diff.front());
        }

        // Model independence: perturb the model, hold the operations.
        for (const auto& row : modelRows())
        {
            auto altered = analyse();
            if (!altered || !row.apply(*altered))
            {
                record("model-independence", row.name, Verdict::Error, "fixture does not fit the row");
                continue;
            }
            auto module = lower(*semantic);
            if (!module || !lowerBodies(*module))
            {
                record("model-independence", row.name, Verdict::Error, "lowering failed");
                continue;
            }
            const auto generated = generate("model-" + row.name, *altered, *module);
            if (!generated)
            {
                record("model-independence", row.name, Verdict::Error, "generation failed on altered model");
                continue;
            }
            const auto diff = differingFiles(*baseline, *generated);
            record("model-independence",
                   row.name,
                   diff.empty() ? Verdict::Pass : Verdict::Gap,
                   diff.empty() ? "bodies identical to baseline" : "bodies followed the model: " + diff.front());
        }
        return true;
    }
};

int runBackendContract(int argc, char** argv)
{
    const auto args = parseArguments(argc, argv);
    if (!args)
    {
        return 2;
    }

    mlir::DialectRegistry registry;
    registry.insert<mlir::dsdl::DSDLDialect,
                    mlir::func::FuncDialect,
                    mlir::arith::ArithDialect,
                    mlir::scf::SCFDialect,
                    mlir::emitc::EmitCDialect>();
    mlir::MLIRContext context(registry);
    context.loadAllAvailableDialects();

    llvmdsdl::DiagnosticEngine parseDiagnostics;
    const std::string          root = args->namespaceRoot.string();
    auto                       ast  = llvmdsdl::parseDefinitions({root}, {}, parseDiagnostics);
    if (!ast)
    {
        llvm::consumeError(ast.takeError());
        std::cerr << "backend-contract: parsing " << root << " failed\n";
        printErrors(parseDiagnostics);
        return 2;
    }

    Session session{*args, context, *ast, {}};
    if (!session.run())
    {
        std::cerr << "backend-contract: could not analyse or lower the fixture namespace\n";
        return 2;
    }

    bool anyError = false;
    bool anyGap   = false;
    std::cout << "backend-contract backend=" << args->backend << "\n";
    for (const auto& r : session.results)
    {
        std::cout << "  " << r.gate << " / " << r.row << ": " << verdictName(r.verdict) << " -- " << r.detail << "\n";
        anyError = anyError || r.verdict == Verdict::Error;
        anyGap   = anyGap || r.verdict == Verdict::Gap;
    }
    const char* verdict = "PASS";
    if (anyError)
    {
        verdict = "ERROR";
    }
    else if (anyGap)
    {
        verdict = "GAP";
    }
    std::cout << "BACKEND_CONTRACT backend=" << args->backend << " verdict=" << verdict << "\n";
    if (anyError)
    {
        return 2;
    }
    if (anyGap)
    {
        if (args->strict)
        {
            return 1;
        }
        std::cout << "  gap reported, not enforced: " << args->backend
                  << " is not in LLVMDSDL_BACKEND_CONTRACT_ENFORCED\n";
    }
    return 0;
}

}  // namespace

/// @brief Turns an escaping exception into a diagnostic and a failure status.
///
/// Without this the exception would leave `main` and reach std::terminate, which prints nothing a
/// user can act on.
int main(int argc, char** argv)
{
    try
    {
        return runBackendContract(argc, argv);
    } catch (const std::exception& e)
    {
        std::cerr << "backend-contract: unhandled exception: " << e.what() << "\n";
        return 2;
    } catch (...)
    {
        std::cerr << "backend-contract: unhandled exception of unknown type\n";
        return 2;
    }
}
