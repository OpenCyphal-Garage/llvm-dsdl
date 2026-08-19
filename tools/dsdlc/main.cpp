//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Entry point for the `dsdlc` command-line frontend.
///
//===----------------------------------------------------------------------===//

#include <llvm/ADT/StringRef.h>
#include <mlir/Dialect/Arith/IR/Arith.h>
#include <mlir/Dialect/EmitC/IR/EmitC.h>
#include <mlir/Dialect/Func/IR/FuncOps.h>
#include <mlir/Dialect/SCF/IR/SCF.h>
#include <mlir/IR/DialectRegistry.h>
#include <mlir/IR/OwningOpRef.h>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <optional>
#include <queue>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <system_error>

#include "llvmdsdl/CodeGen/CEmitter.h"
#include "llvmdsdl/CodeGen/CppEmitter.h"
#include "llvmdsdl/CodeGen/EmitCommon.h"
#include "llvmdsdl/CodeGen/GoEmitter.h"
#include "llvmdsdl/CodeGen/ObjectEmitter.h"
#include "llvmdsdl/CodeGen/PythonEmitter.h"
#include "llvmdsdl/CodeGen/EmitTrace.h"
#include "llvmdsdl/CodeGen/RustEmitter.h"
#include "llvmdsdl/CodeGen/TsEmitter.h"
#include "llvmdsdl/CodeGen/UavcanEmbeddedCatalog.h"
#include "llvmdsdl/Frontend/ASTPrinter.h"
#include "llvmdsdl/Frontend/DepfilePlanner.h"
#include "llvmdsdl/Frontend/Parser.h"
#include "llvmdsdl/Frontend/SourceLocation.h"
#include "llvmdsdl/Frontend/TargetResolution.h"
#include "llvmdsdl/IR/DSDLDialect.h"
#include "llvmdsdl/Lowering/LowerToMLIR.h"
#include "llvmdsdl/Semantics/Analyzer.h"
#include "llvmdsdl/Semantics/Model.h"
#include "llvmdsdl/Support/Diagnostics.h"
#include "llvmdsdl/Version.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/MLIRContext.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/raw_ostream.h"

namespace
{

// Emit-order verifier side channel: writes the abstract op trace a string emitter recorded into
// `sink` to `path`, one op per line ("OP_NAME" or "OP_NAME <payload>"). The comparator diffs
// these across backends; enabled per run by the LLVMDSDL_EMIT_TRACE environment variable.
//
// LLVMDSDL_EMIT_TRACE_MUTATE=swap-tag-validate additionally swaps each VALIDATE_TAG with the
// MASK_TAG that follows it before writing — the verifier's end-to-end mutation negative
// control (a genuine mask-before-validate reorder flowing through the real pipeline). It
// affects only this diagnostic trace file, never the generated code.
void writeEmitTrace(const std::string& path, const llvmdsdl::EmitTraceSink& sink)
{
    std::ofstream os(path, std::ios::binary | std::ios::trunc);
    if (!os)
    {
        llvm::errs() << "warning: could not open LLVMDSDL_EMIT_TRACE file: " << path << "\n";
        return;
    }
    auto              events = sink.events();
    const char* const mutate = std::getenv("LLVMDSDL_EMIT_TRACE_MUTATE");
    if ((mutate != nullptr) && (std::string_view(mutate) == "swap-tag-validate"))
    {
        for (std::size_t i = 0; i + 1 < events.size(); ++i)
        {
            if (events[i].op == llvmdsdl::EmitTraceOp::ValidateTag &&
                events[i + 1].op == llvmdsdl::EmitTraceOp::MaskTag)
            {
                std::swap(events[i], events[i + 1]);
            }
        }
    }
    for (const auto& event : events)
    {
        if (event.op == llvmdsdl::EmitTraceOp::SectionStart)
        {
            // "SECTION <canonical.type.Name.maj.min> <serialize|deserialize>": segment header
            // the comparator keys on, so divergences localize to one (type, direction).
            os << llvmdsdl::emitTraceOpName(event.op) << ' ' << event.label << ' '
               << (event.payload == 0 ? "serialize" : "deserialize") << '\n';
            continue;
        }
        os << llvmdsdl::emitTraceOpName(event.op);
        if (event.payload >= 0)
        {
            os << ' ' << event.payload;
        }
        os << '\n';
    }
}

struct CliOptions final
{
    std::vector<std::string> positionalTargets;

    /// `+`-sigil targets naming the embedded catalog, with the sigil stripped.
    std::vector<std::string> builtinTargets;

    std::vector<std::string> lookupDirs;

    std::string targetLanguage;
    std::string outDir{"dsdl_out"};

    bool helpRequested{false};
    bool versionRequested{false};
    bool noTargetNamespaces{false};
    bool noOverwrite{false};
    bool allowUnregulatedFixedPortId{false};
    bool omitDependencies{false};
    bool noEmbeddedUavcan{false};

    /// @brief Criteria selecting when support code is generated.
    llvmdsdl::SupportGeneration supportGeneration{llvmdsdl::SupportGeneration::AsNeeded};
    bool                        sawGenerateSupport{false};
    bool                        optimizeLoweredSerDes{false};
    bool                        emitDeprecationAttributes{true};
    bool                        dryRun{false};
    bool                        listOutputs{false};
    bool                        listInputs{false};
    bool                        emitDepfiles{false};

    /// @brief Per-tranche manifest enabling removal of outputs this run no longer produces.
    std::string pruneManifest;

    int verbose{0};

    llvmdsdl::CppProfile                  cppProfile{llvmdsdl::CppProfile::Both};
    std::string                           rustCrateName{"llvmdsdl_generated"};
    llvmdsdl::RustProfile                 rustProfile{llvmdsdl::RustProfile::Std};
    llvmdsdl::RustRuntimeSpecialization   rustRuntimeSpecialization{llvmdsdl::RustRuntimeSpecialization::Portable};
    llvmdsdl::RustMemoryMode              rustMemoryMode{llvmdsdl::RustMemoryMode::MaxInline};
    std::uint32_t                         rustInlineThresholdBytes{256U};
    std::string                           goModuleName{"llvmdsdl_generated"};
    std::string                           tsModuleName{"llvmdsdl_generated"};
    llvmdsdl::TsRuntimeSpecialization     tsRuntimeSpecialization{llvmdsdl::TsRuntimeSpecialization::Portable};
    llvmdsdl::PythonRuntimeSpecialization pyRuntimeSpecialization{llvmdsdl::PythonRuntimeSpecialization::Portable};
    std::string                           pyPackageName{"dsdl_gen"};
    std::string                           objTargetEndianness;
    std::string                           objTargetTriple;
    std::string                           objArchiveName{"llvmdsdl_generated"};
    std::string                           objAbiLanguage{"c"};
    std::uint32_t                         jobs{0U};
    bool                                  objNoArchive{false};

    bool sawCppProfile{false};
    bool sawRustCrateName{false};
    bool sawRustProfile{false};
    bool sawRustRuntimeSpecialization{false};
    bool sawRustMemoryMode{false};
    bool sawRustInlineThreshold{false};
    bool sawGoModule{false};
    bool sawTsModule{false};
    bool sawTsRuntimeSpecialization{false};
    bool sawPyPackage{false};
    bool sawPyRuntimeSpecialization{false};
    bool sawObjTargetEndianness{false};
    bool sawObjTargetTriple{false};
    bool sawObjArchiveName{false};
    bool sawObjAbiLanguage{false};
    bool sawObjNoArchive{false};

    std::uint32_t fileMode{0444U};
};

bool isHelpToken(llvm::StringRef arg)
{
    return arg == "--help" || arg == "-h";
}

bool isVersionToken(llvm::StringRef arg)
{
    return arg == "--version" || arg == "-V";
}

/// @brief Maps a `--target-language` value onto the naming policies its output will use.
///
/// Only source-emitting targets contribute: `ast` and `mlir` produce no identifiers, so they name no
/// language and the frontend runs no output-name collision check for them. `obj` emits both a C++
/// header and a C shim, so it names both. See the decisions section of
/// docs/development/identifier-stropping.md.
llvm::SmallVector<llvmdsdl::OutputLanguage, 2> namingLanguagesForTarget(const llvm::StringRef language)
{
    using llvmdsdl::CodegenNamingLanguage;
    if (language == "c")
    {
        return {{CodegenNamingLanguage::C, "c"}};
    }
    if (language == "cpp")
    {
        return {{CodegenNamingLanguage::Cpp, "cpp"}};
    }
    if (language == "rust")
    {
        return {{CodegenNamingLanguage::Rust, "rust"}};
    }
    if (language == "go")
    {
        return {{CodegenNamingLanguage::Go, "go"}};
    }
    if (language == "ts")
    {
        return {{CodegenNamingLanguage::TypeScript, "ts"}};
    }
    if (language == "python")
    {
        return {{CodegenNamingLanguage::Python, "python"}};
    }
    if (language == "obj")
    {
        return {{CodegenNamingLanguage::Cpp, "obj"}, {CodegenNamingLanguage::C, "obj"}};
    }
    return {};
}

bool isCodegenLanguage(llvm::StringRef language)
{
    return language == "c" || language == "cpp" || language == "rust" || language == "go" || language == "ts" ||
           language == "python" || language == "obj";
}

// Languages whose output is source the caller then builds. The `obj` lane is excluded: it stages and
// compiles its own sources, so its support code is an internal detail of a `.o`/`.a` artifact.
bool emitsSourceTree(llvm::StringRef language)
{
    return isCodegenLanguage(language) && language != "obj";
}

bool isKnownLanguage(llvm::StringRef language)
{
    return isCodegenLanguage(language) || language == "ast" || language == "mlir";
}

void printUsage()
{
    llvm::errs() << "Usage: dsdlc --target-language <ast|mlir|c|cpp|rust|go|ts|python|obj> [options] "
                    "[target_files_or_root_namespace ...]\n"
                 << "Try: dsdlc --help\n";
}

void printHelp()
{
    llvm::errs() << "NAME\n"
                 << "  dsdlc - DSDL frontend, MLIR lowerer, and multi-language code generator\n\n"
                 << "SYNOPSIS\n"
                 << "  dsdlc --target-language <lang> [options] [target_files_or_root_namespace ...]\n"
                 << "  dsdlc --help\n"
                 << "  dsdlc --version\n\n"
                 << "LANGUAGES\n"
                 << "  ast | mlir | c | cpp | rust | go | ts | python | obj\n\n"
                 << "TARGET OPTIONS\n"
                 << "  target_files_or_root_namespace\n"
                 << "      One or more DSDL files or root-namespace folders.\n"
                 << "      Folder targets expand recursively to .dsdl files unless --no-target-namespaces.\n"
                 << "      Colon syntax is supported: <root>:<relative/path/Type.1.0.dsdl>.\n"
                 << "  +<selector>\n"
                 << "      Target the embedded uavcan catalog: a namespace (+uavcan.node), a type\n"
                 << "      (+uavcan.node.Heartbeat), or a version (+uavcan.node.Heartbeat.1.0).\n"
                 << "      Requires --target-language 'mlir' or a codegen language.\n"
                 << "  --no-target-namespaces\n"
                 << "      Reject folder positional targets.\n"
                 << "  --lookup-dir, -I <dir>\n"
                 << "      Repeatable lookup roots for dependency resolution and target root inference.\n"
                 << "      Also merges DSDL_INCLUDE_PATH and CYPHAL_PATH.\n\n"
                 << "COMMON OPTIONS\n"
                 << "  --target-language, -l <lang>\n"
                 << "      Required output mode selector.\n"
                 << "  --outdir, -O <dir>\n"
                 << "      Output directory root for codegen languages (default: dsdl_out).\n"
                 << "  --generate-support {always,never,as-needed,only}\n"
                 << "      Change the criteria used to enable or disable support code generation.\n"
                 << "      Support code is everything not derived from a definition: runtime\n"
                 << "      headers and modules, package manifests, and scaffolding.\n"
                 << "        as-needed (default) - generate support if it is needed.\n"
                 << "        always              - always generate support code.\n"
                 << "        never               - never generate support code.\n"
                 << "        only                - only generate support code.\n"
                 << "      'always' and 'only' need no positional targets. Requires a\n"
                 << "      source-emitting --target-language (c, cpp, rust, go, ts, python).\n"
                 << "  --prune-manifest <file>\n"
                 << "      Record this run's outputs in <file> and, on the next run, delete the\n"
                 << "      outputs it recorded that are no longer produced. One manifest per dsdlc\n"
                 << "      invocation: a build that splits a namespace into tranches gives each\n"
                 << "      tranche its own, so a tranche prunes only what it owns. Removals are\n"
                 << "      confined to --outdir. Ignored under --dry-run and the --list-* modes.\n"
                 << "  --optimize-lowered-serdes\n"
                 << "      Enable optional MLIR optimization for lowered serialization plans.\n"
                 << "  --no-deprecation-attributes\n"
                 << "      Suppress the language-native deprecation attributes that @deprecated\n"
                 << "      definitions carry by default in C, C++, and Rust. Use this when a\n"
                 << "      -Werror build must keep using deprecated definitions. The deprecation\n"
                 << "      notice and metadata constant are emitted regardless.\n"
                 << "  --no-overwrite\n"
                 << "      Fail if an output file already exists.\n"
                 << "  --file-mode <mode>\n"
                 << "      File mode for generated files using auto-base parsing (default: 0o444).\n"
                 << "  --allow-unregulated-fixed-port-id\n"
                 << "      Allow fixed port IDs outside regulated ranges.\n"
                 << "  --omit-dependencies\n"
                 << "      Emit only explicit targets; dependencies are still resolved and analyzed.\n"
                 << "  --no-embedded-uavcan\n"
                 << "      Disable automatic embedded uavcan dependency catalog for mlir/codegen targets.\n"
                 << "  --verbose, -v\n"
                 << "      Increase verbosity (-v, -vv).\n"
                 << "  --dry-run, -d\n"
                 << "      Run full planning/validation without filesystem writes.\n"
                 << "  --jobs, -j <N>\n"
                 << "      Worker parallelism hint (N>=1). Currently used by the obj backend compile stage.\n"
                 << "  -MD\n"
                 << "      Emit make-style .d dependency files alongside generated outputs.\n"
                 << "  --list-inputs\n"
                 << "      Emit semicolon-separated input file list (implies --dry-run).\n"
                 << "  --list-outputs\n"
                 << "      Emit semicolon-separated output file list (implies --dry-run).\n"
                 << "      When combined with --list-inputs, emits inputs first then one empty separator value.\n"
                 << "  --help, -h\n"
                 << "      Print this help text.\n"
                 << "  --version, -V\n"
                 << "      Print tool version and exit.\n\n"
                 << "BACKEND OPTIONS\n"
                 << "  C++:    --cpp-profile <std|pmr|both|autosar>\n"
                 << "  Rust:   --rust-crate-name <name>\n"
                 << "          --rust-profile <std|no-std-alloc>\n"
                 << "          --rust-runtime-specialization <portable|fast>\n"
                 << "          --rust-memory-mode <max-inline|inline-then-pool>\n"
                 << "          --rust-inline-threshold-bytes <N>\n"
                 << "  Go:     --go-module <name>\n"
                 << "  TS:     --ts-module <name>\n"
                 << "          --ts-runtime-specialization <portable|fast>\n"
                 << "  Python: --py-package <name>\n"
                 << "          --py-runtime-specialization <portable|fast>\n"
                 << "  Obj:    --target-endianness <little|big>\n"
                 << "          --target-triple <triple>\n"
                 << "          --obj-archive-name <name>\n"
                 << "          --obj-abi-language <c|cpp>\n"
                 << "          --obj-no-archive\n";
}

void printDiagnostics(const llvmdsdl::DiagnosticEngine& diagnostics)
{
    for (const auto& d : diagnostics.diagnostics())
    {
        llvm::StringRef level = "note";
        if (d.level == llvmdsdl::DiagnosticLevel::Warning)
        {
            level = "warning";
        }
        else if (d.level == llvmdsdl::DiagnosticLevel::Error)
        {
            level = "error";
        }
        llvm::errs() << d.location.str() << ": " << level << ": " << d.message << "\n";
    }
}

std::string resolveOutputRoot(const std::string& outDir)
{
    if (outDir.empty())
    {
        return "stdout";
    }
    std::error_code ec;
    const auto      abs = std::filesystem::absolute(outDir, ec);
    if (!ec)
    {
        return abs.string();
    }
    return outDir;
}

void printRunSummary(llvm::StringRef                     command,
                     llvm::StringRef                     outputRoot,
                     std::uint64_t                       generatedFiles,
                     std::chrono::steady_clock::duration elapsed)
{
    const auto elapsedMs         = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    const auto elapsedWholeSec   = elapsedMs / 1000;
    const auto elapsedFractionMs = elapsedMs % 1000;

    llvm::errs() << "Run summary:\n"
                 << "  command: " << command << "\n"
                 << "  output root: " << outputRoot << "\n"
                 << "  files generated: " << generatedFiles << "\n"
                 << "  elapsed: " << elapsedWholeSec << ".";
    if (elapsedFractionMs < 100)
    {
        llvm::errs() << "0";
    }
    if (elapsedFractionMs < 10)
    {
        llvm::errs() << "0";
    }
    llvm::errs() << elapsedFractionMs << "s\n";
}

std::string formatDurationMilliseconds(const std::chrono::steady_clock::duration elapsed)
{
    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    return std::to_string(elapsedMs) + " ms";
}

std::string normalizePathForCompare(const std::string& path)
{
    std::error_code             ec;
    const std::filesystem::path p(path);
    auto                        n = std::filesystem::weakly_canonical(p, ec);
    if (ec)
    {
        ec.clear();
        n = std::filesystem::absolute(p, ec);
        if (ec)
        {
            return p.lexically_normal().string();
        }
    }
    return n.lexically_normal().string();
}

llvm::Expected<std::uint32_t> parseFileMode(llvm::StringRef text)
{
    llvm::StringRef body = text;
    unsigned        base = 10U;

    if (body.size() >= 2U && body[0] == '0' && (body[1] == 'o' || body[1] == 'O'))
    {
        base = 8U;
        body = body.drop_front(2);
    }
    else if (body.size() >= 2U && body[0] == '0' && (body[1] == 'x' || body[1] == 'X'))
    {
        base = 16U;
        body = body.drop_front(2);
    }
    else if (body.size() >= 2U && body[0] == '0' && (body[1] == 'b' || body[1] == 'B'))
    {
        base = 2U;
        body = body.drop_front(2);
    }

    if (body.empty())
    {
        return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                       "invalid --file-mode value: %s",
                                       text.str().c_str());
    }

    std::uint64_t parsed{};
    if (body.getAsInteger(base, parsed) || parsed > 07777U)
    {
        return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                       "invalid --file-mode value: %s",
                                       text.str().c_str());
    }
    return static_cast<std::uint32_t>(parsed);
}

llvm::Expected<CliOptions> parseCli(int argc, char** argv)
{
    CliOptions options;

    auto requireValue = [&](int& i, llvm::StringRef optionName) -> llvm::Expected<std::string> {
        if (i + 1 >= argc)
        {
            return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                           "missing value for %s",
                                           optionName.str().c_str());
        }
        return std::string(argv[++i]);
    };

    // A leading '+' names the embedded catalog rather than the filesystem. This is target-token
    // syntax, not option syntax, so it stays significant after `--`; a real file whose name starts
    // with '+' is reached as `./+name`.
    const auto addTargetToken = [&options](llvm::StringRef token) {
        if (token.starts_with('+'))
        {
            options.builtinTargets.emplace_back(token.substr(1U).str());
            return;
        }
        options.positionalTargets.emplace_back(token.str());
    };

    for (int i = 1; i < argc; ++i)
    {
        llvm::StringRef arg(argv[i]);

        if (arg == "--")
        {
            for (++i; i < argc; ++i)
            {
                addTargetToken(argv[i]);
            }
            break;
        }

        if (isHelpToken(arg))
        {
            options.helpRequested = true;
            continue;
        }
        if (isVersionToken(arg))
        {
            options.versionRequested = true;
            continue;
        }

        if (arg == "--no-target-namespaces")
        {
            options.noTargetNamespaces = true;
            continue;
        }
        if (arg == "--lookup-dir" || arg == "-I")
        {
            auto value = requireValue(i, arg);
            if (!value)
            {
                return value.takeError();
            }
            options.lookupDirs.push_back(*value);
            continue;
        }
        if (arg == "--outdir" || arg == "-O")
        {
            auto value = requireValue(i, arg);
            if (!value)
            {
                return value.takeError();
            }
            options.outDir = *value;
            continue;
        }
        if (arg == "--prune-manifest")
        {
            auto value = requireValue(i, arg);
            if (!value)
            {
                return value.takeError();
            }
            options.pruneManifest = *value;
            continue;
        }
        if (arg == "--target-language" || arg == "-l")
        {
            auto value = requireValue(i, arg);
            if (!value)
            {
                return value.takeError();
            }
            options.targetLanguage = *value;
            continue;
        }

        if (arg == "--no-overwrite")
        {
            options.noOverwrite = true;
            continue;
        }
        if (arg == "--file-mode")
        {
            auto value = requireValue(i, arg);
            if (!value)
            {
                return value.takeError();
            }
            auto mode = parseFileMode(*value);
            if (!mode)
            {
                return mode.takeError();
            }
            options.fileMode = *mode;
            continue;
        }
        if (arg == "--allow-unregulated-fixed-port-id")
        {
            options.allowUnregulatedFixedPortId = true;
            continue;
        }
        if (arg == "--generate-support")
        {
            auto value = requireValue(i, arg);
            if (!value)
            {
                return value.takeError();
            }
            options.sawGenerateSupport = true;
            if (*value == "as-needed")
            {
                options.supportGeneration = llvmdsdl::SupportGeneration::AsNeeded;
            }
            else if (*value == "always")
            {
                options.supportGeneration = llvmdsdl::SupportGeneration::Always;
            }
            else if (*value == "never")
            {
                options.supportGeneration = llvmdsdl::SupportGeneration::Never;
            }
            else if (*value == "only")
            {
                options.supportGeneration = llvmdsdl::SupportGeneration::Only;
            }
            else
            {
                return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                               "invalid --generate-support value: %s",
                                               value->c_str());
            }
            continue;
        }
        if (arg == "--omit-dependencies")
        {
            options.omitDependencies = true;
            continue;
        }
        if (arg == "--no-embedded-uavcan")
        {
            options.noEmbeddedUavcan = true;
            continue;
        }
        if (arg == "--dry-run" || arg == "-d")
        {
            options.dryRun = true;
            continue;
        }
        if (arg == "-MD")
        {
            options.emitDepfiles = true;
            continue;
        }
        if (arg == "--list-outputs")
        {
            options.listOutputs = true;
            continue;
        }
        if (arg == "--list-inputs")
        {
            options.listInputs = true;
            continue;
        }
        if (arg == "--jobs" || arg == "-j")
        {
            auto value = requireValue(i, arg);
            if (!value)
            {
                return value.takeError();
            }
            std::uint64_t parsed{};
            if (llvm::StringRef(*value).getAsInteger(10, parsed) || parsed == 0U ||
                parsed > std::numeric_limits<std::uint32_t>::max())
            {
                return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                               "invalid --jobs value: %s",
                                               value->c_str());
            }
            options.jobs = static_cast<std::uint32_t>(parsed);
            continue;
        }
        if (arg == "--verbose")
        {
            ++options.verbose;
            continue;
        }
        if (arg.starts_with("-") && arg.size() > 1 &&
            arg.drop_front().find_if([](const char c) { return c != 'v'; }) == llvm::StringRef::npos)
        {
            options.verbose += static_cast<int>(arg.size() - 1);
            continue;
        }

        if (arg == "--optimize-lowered-serdes")
        {
            options.optimizeLoweredSerDes = true;
            continue;
        }
        if (arg == "--no-deprecation-attributes")
        {
            options.emitDeprecationAttributes = false;
            continue;
        }
        if (arg == "--cpp-profile")
        {
            auto value = requireValue(i, arg);
            if (!value)
            {
                return value.takeError();
            }
            options.sawCppProfile = true;
            if (*value == "std")
            {
                options.cppProfile = llvmdsdl::CppProfile::Std;
            }
            else if (*value == "pmr")
            {
                options.cppProfile = llvmdsdl::CppProfile::Pmr;
            }
            else if (*value == "both")
            {
                options.cppProfile = llvmdsdl::CppProfile::Both;
            }
            else if (*value == "autosar")
            {
                options.cppProfile = llvmdsdl::CppProfile::Autosar;
            }
            else
            {
                return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                               "invalid --cpp-profile value: %s",
                                               value->c_str());
            }
            continue;
        }
        if (arg == "--rust-crate-name")
        {
            auto value = requireValue(i, arg);
            if (!value)
            {
                return value.takeError();
            }
            options.sawRustCrateName = true;
            options.rustCrateName    = *value;
            continue;
        }
        if (arg == "--rust-profile")
        {
            auto value = requireValue(i, arg);
            if (!value)
            {
                return value.takeError();
            }
            options.sawRustProfile = true;
            if (*value == "std")
            {
                options.rustProfile = llvmdsdl::RustProfile::Std;
            }
            else if (*value == "no-std-alloc")
            {
                options.rustProfile = llvmdsdl::RustProfile::NoStdAlloc;
            }
            else
            {
                return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                               "invalid --rust-profile value: %s",
                                               value->c_str());
            }
            continue;
        }
        if (arg == "--rust-runtime-specialization")
        {
            auto value = requireValue(i, arg);
            if (!value)
            {
                return value.takeError();
            }
            options.sawRustRuntimeSpecialization = true;
            if (*value == "portable")
            {
                options.rustRuntimeSpecialization = llvmdsdl::RustRuntimeSpecialization::Portable;
            }
            else if (*value == "fast")
            {
                options.rustRuntimeSpecialization = llvmdsdl::RustRuntimeSpecialization::Fast;
            }
            else
            {
                return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                               "invalid --rust-runtime-specialization value: %s",
                                               value->c_str());
            }
            continue;
        }
        if (arg == "--rust-memory-mode")
        {
            auto value = requireValue(i, arg);
            if (!value)
            {
                return value.takeError();
            }
            options.sawRustMemoryMode = true;
            if (*value == "max-inline")
            {
                options.rustMemoryMode = llvmdsdl::RustMemoryMode::MaxInline;
            }
            else if (*value == "inline-then-pool")
            {
                options.rustMemoryMode = llvmdsdl::RustMemoryMode::InlineThenPool;
            }
            else
            {
                return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                               "invalid --rust-memory-mode value: %s",
                                               value->c_str());
            }
            continue;
        }
        if (arg == "--rust-inline-threshold-bytes")
        {
            auto value = requireValue(i, arg);
            if (!value)
            {
                return value.takeError();
            }
            options.sawRustInlineThreshold = true;
            std::uint64_t parsed{};
            if (llvm::StringRef(*value).getAsInteger(10, parsed) || parsed == 0U ||
                parsed > std::numeric_limits<std::uint32_t>::max())
            {
                return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                               "invalid --rust-inline-threshold-bytes value: %s",
                                               value->c_str());
            }
            options.rustInlineThresholdBytes = static_cast<std::uint32_t>(parsed);
            continue;
        }
        if (arg == "--go-module")
        {
            auto value = requireValue(i, arg);
            if (!value)
            {
                return value.takeError();
            }
            options.sawGoModule  = true;
            options.goModuleName = *value;
            continue;
        }
        if (arg == "--ts-module")
        {
            auto value = requireValue(i, arg);
            if (!value)
            {
                return value.takeError();
            }
            options.sawTsModule  = true;
            options.tsModuleName = *value;
            continue;
        }
        if (arg == "--ts-runtime-specialization")
        {
            auto value = requireValue(i, arg);
            if (!value)
            {
                return value.takeError();
            }
            options.sawTsRuntimeSpecialization = true;
            if (*value == "portable")
            {
                options.tsRuntimeSpecialization = llvmdsdl::TsRuntimeSpecialization::Portable;
            }
            else if (*value == "fast")
            {
                options.tsRuntimeSpecialization = llvmdsdl::TsRuntimeSpecialization::Fast;
            }
            else
            {
                return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                               "invalid --ts-runtime-specialization value: %s",
                                               value->c_str());
            }
            continue;
        }
        if (arg == "--py-package")
        {
            auto value = requireValue(i, arg);
            if (!value)
            {
                return value.takeError();
            }
            options.sawPyPackage  = true;
            options.pyPackageName = *value;
            continue;
        }
        if (arg == "--py-runtime-specialization")
        {
            auto value = requireValue(i, arg);
            if (!value)
            {
                return value.takeError();
            }
            options.sawPyRuntimeSpecialization = true;
            if (*value == "portable")
            {
                options.pyRuntimeSpecialization = llvmdsdl::PythonRuntimeSpecialization::Portable;
            }
            else if (*value == "fast")
            {
                options.pyRuntimeSpecialization = llvmdsdl::PythonRuntimeSpecialization::Fast;
            }
            else
            {
                return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                               "invalid --py-runtime-specialization value: %s",
                                               value->c_str());
            }
            continue;
        }
        if (arg == "--target-endianness")
        {
            auto value = requireValue(i, arg);
            if (!value)
            {
                return value.takeError();
            }
            options.sawObjTargetEndianness = true;
            options.objTargetEndianness    = *value;
            continue;
        }
        if (arg == "--target-triple")
        {
            auto value = requireValue(i, arg);
            if (!value)
            {
                return value.takeError();
            }
            options.sawObjTargetTriple = true;
            options.objTargetTriple    = *value;
            continue;
        }
        if (arg == "--obj-archive-name")
        {
            auto value = requireValue(i, arg);
            if (!value)
            {
                return value.takeError();
            }
            options.sawObjArchiveName = true;
            options.objArchiveName    = *value;
            continue;
        }
        if (arg == "--obj-abi-language")
        {
            auto value = requireValue(i, arg);
            if (!value)
            {
                return value.takeError();
            }
            options.sawObjAbiLanguage = true;
            options.objAbiLanguage    = *value;
            continue;
        }
        if (arg == "--obj-no-archive")
        {
            options.sawObjNoArchive = true;
            options.objNoArchive    = true;
            continue;
        }
        if (arg.starts_with('-'))
        {
            return llvm::createStringError(llvm::inconvertibleErrorCode(), "unknown argument: %s", arg.str().c_str());
        }

        addTargetToken(arg);
    }

    return options;
}

llvm::Expected<int> validateLanguageGatedOptions(const CliOptions& options)
{
    llvm::StringRef language(options.targetLanguage);

    auto failIf = [&](bool condition, llvm::StringRef optionName, llvm::StringRef expectedLang) -> llvm::Expected<int> {
        if (!condition)
        {
            return 0;
        }
        return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                       "%s is only valid when --target-language is '%s'",
                                       optionName.data(),
                                       expectedLang.data());
    };

    // Embedded targets are gated the same way the catalog itself is. Both of these must fail rather
    // than resolve to nothing: a build that silently generates no files is the failure mode the
    // whole '+' design exists to avoid.
    if (!options.builtinTargets.empty())
    {
        if (options.noEmbeddedUavcan)
        {
            return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                           "cannot select embedded target '+%s' with --no-embedded-uavcan",
                                           options.builtinTargets.front().c_str());
        }
        if (language != "mlir" && !isCodegenLanguage(language))
        {
            return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                           "embedded target '+%s' requires --target-language 'mlir' or a codegen "
                                           "language, not '%s'",
                                           options.builtinTargets.front().c_str(),
                                           options.targetLanguage.c_str());
        }
    }

    if (options.sawGenerateSupport && !emitsSourceTree(language))
    {
        return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                       "--generate-support requires a source-emitting --target-language "
                                       "(c, cpp, rust, go, ts, python), not '%s'",
                                       options.targetLanguage.c_str());
    }

    if (auto r = failIf(options.sawCppProfile && language != "cpp", "--cpp-profile", "cpp"); !r)
    {
        return r.takeError();
    }
    if (auto r = failIf((options.sawRustCrateName || options.sawRustProfile || options.sawRustRuntimeSpecialization ||
                         options.sawRustMemoryMode || options.sawRustInlineThreshold) &&
                            language != "rust",
                        "--rust-*",
                        "rust");
        !r)
    {
        return r.takeError();
    }
    if (auto r = failIf(options.sawGoModule && language != "go", "--go-module", "go"); !r)
    {
        return r.takeError();
    }
    if (auto r =
            failIf((options.sawTsModule || options.sawTsRuntimeSpecialization) && language != "ts", "--ts-*", "ts");
        !r)
    {
        return r.takeError();
    }
    if (auto r = failIf((options.sawPyPackage || options.sawPyRuntimeSpecialization) && language != "python",
                        "--py-*",
                        "python");
        !r)
    {
        return r.takeError();
    }
    if (auto r = failIf((options.sawObjTargetEndianness || options.sawObjTargetTriple || options.sawObjArchiveName ||
                         options.sawObjAbiLanguage || options.sawObjNoArchive) &&
                            language != "obj",
                        "--target-endianness/--target-triple/--obj-abi-language/--obj-*",
                        "obj");
        !r)
    {
        return r.takeError();
    }
    if (language == "obj" && !options.sawObjTargetEndianness)
    {
        return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                       "--target-endianness is required when --target-language is 'obj'");
    }
    if (language == "obj")
    {
        const auto endian = llvm::StringRef(options.objTargetEndianness);
        if (endian != "little" && endian != "big")
        {
            return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                           "invalid --target-endianness value: %s",
                                           options.objTargetEndianness.c_str());
        }
        const auto abiLanguage = llvm::StringRef(options.objAbiLanguage);
        if (abiLanguage != "c" && abiLanguage != "cpp")
        {
            return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                           "invalid --obj-abi-language value: %s",
                                           options.objAbiLanguage.c_str());
        }
    }
    if (options.emitDepfiles && !isCodegenLanguage(language))
    {
        return llvm::
            createStringError(llvm::inconvertibleErrorCode(),
                              "-MD is only valid when --target-language is one of: c, cpp, rust, go, ts, python, obj");
    }

    return 0;
}

std::unordered_set<std::string> collectExplicitKeys(const llvmdsdl::SemanticModule& semantic)
{
    std::unordered_set<std::string> out;
    for (const auto& def : semantic.definitions)
    {
        if (def.info.isExplicitTarget)
        {
            out.insert(llvmdsdl::definitionTypeKey(def.info));
        }
    }
    return out;
}

std::string typeKeyFromRef(const llvmdsdl::SemanticTypeRef& ref)
{
    return ref.fullName + ":" + std::to_string(ref.majorVersion) + ":" + std::to_string(ref.minorVersion);
}

std::unordered_set<std::string> computeDependencyClosure(const llvmdsdl::SemanticModule&        semantic,
                                                         const std::unordered_set<std::string>& explicitKeys)
{
    std::unordered_map<std::string, const llvmdsdl::SemanticDefinition*> byKey;
    byKey.reserve(semantic.definitions.size());
    for (const auto& def : semantic.definitions)
    {
        byKey.emplace(llvmdsdl::definitionTypeKey(def.info), &def);
    }

    std::unordered_set<std::string> closure;
    std::queue<std::string>         queue;

    // determinism-ok: seeds a breadth-first closure. Visit order varies, the
    // resulting set does not, and what is emitted comes from the sorted
    // selectedTypeKeys below rather than from this traversal.
    for (const auto& key : explicitKeys)
    {
        if (byKey.contains(key) && closure.insert(key).second)
        {
            queue.push(key);
        }
    }

    auto enqueueSectionDependencies = [&](const llvmdsdl::SemanticSection& section) {
        for (const auto& field : section.fields)
        {
            if (!field.resolvedType.compositeType)
            {
                continue;
            }
            const auto depKey = typeKeyFromRef(*field.resolvedType.compositeType);
            if (byKey.contains(depKey) && closure.insert(depKey).second)
            {
                queue.push(depKey);
            }
        }
    };

    while (!queue.empty())
    {
        const auto key = queue.front();
        queue.pop();

        const auto it = byKey.find(key);
        if (it == byKey.end())
        {
            continue;
        }

        enqueueSectionDependencies(it->second->request);
        if (it->second->response)
        {
            enqueueSectionDependencies(*it->second->response);
        }
    }

    return closure;
}

// Path recorded as the depfile prerequisite for outputs generated from the embedded uavcan catalog.
// The catalog is compiled into this binary and has no source file to name, so the binary is the
// honest stand-in: upgrading dsdlc genuinely changes those outputs' inputs.
//
// argv[0] alone is not a reliable path (PATH lookup, symlinks, some platforms), so this defers to
// getMainExecutable, which additionally needs the address of a symbol in this image to locate it.
std::string resolveToolchainStampPath(const char* argv0)
{
    static int  anchor         = 0;
    void* const anchorAddress  = static_cast<void*>(&anchor);
    std::string mainExecutable = llvm::sys::fs::getMainExecutable(argv0, anchorAddress);
    if (!mainExecutable.empty())
    {
        return mainExecutable;
    }
    return (argv0 != nullptr) ? std::string(argv0) : std::string{};
}

llvmdsdl::SemanticModule filterSemanticModule(const llvmdsdl::SemanticModule&        semantic,
                                              const std::unordered_set<std::string>& selectedKeys)
{
    llvmdsdl::SemanticModule out;
    out.definitions.reserve(semantic.definitions.size());

    for (const auto& def : semantic.definitions)
    {
        if (selectedKeys.contains(llvmdsdl::definitionTypeKey(def.info)))
        {
            out.definitions.push_back(def);
        }
    }

    return out;
}

llvmdsdl::ASTModule filterAstModule(const llvmdsdl::ASTModule& ast, const std::unordered_set<std::string>& selectedKeys)
{
    llvmdsdl::ASTModule out;
    out.definitions.reserve(ast.definitions.size());

    for (const auto& def : ast.definitions)
    {
        if (selectedKeys.contains(llvmdsdl::definitionTypeKey(def.info)))
        {
            out.definitions.push_back(def);
        }
    }

    return out;
}

std::vector<std::string> collectInputFilesForClosure(const llvmdsdl::SemanticModule&        semantic,
                                                     const std::unordered_set<std::string>& closureKeys)
{
    std::vector<std::string> out;
    out.reserve(closureKeys.size());

    for (const auto& def : semantic.definitions)
    {
        if (!closureKeys.contains(llvmdsdl::definitionTypeKey(def.info)))
        {
            continue;
        }
        if (llvmdsdl::isEmbeddedUavcanSyntheticPath(def.info.filePath))
        {
            continue;
        }
        out.push_back(normalizePathForCompare(def.info.filePath));
    }

    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

llvmdsdl::SemanticModule mergeSemanticModulesPreferPrimary(const llvmdsdl::SemanticModule& primary,
                                                           const llvmdsdl::SemanticModule& secondary)
{
    llvmdsdl::SemanticModule out;
    out.definitions.reserve(primary.definitions.size() + secondary.definitions.size());

    std::unordered_set<std::string> seen;
    seen.reserve(primary.definitions.size() + secondary.definitions.size());

    for (const auto& def : primary.definitions)
    {
        const auto key = llvmdsdl::definitionTypeKey(def.info);
        if (seen.insert(key).second)
        {
            out.definitions.push_back(def);
        }
    }
    for (const auto& def : secondary.definitions)
    {
        const auto key = llvmdsdl::definitionTypeKey(def.info);
        if (seen.insert(key).second)
        {
            out.definitions.push_back(def);
        }
    }

    return out;
}

std::vector<std::string> dedupSorted(std::vector<std::string> values)
{
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
    return values;
}

void emitScsvLists(const std::vector<std::string>& inputs,
                   const std::vector<std::string>& outputs,
                   const bool                      listInputs,
                   const bool                      listOutputs)
{
    std::vector<std::string> cells;
    if (listInputs)
    {
        cells.insert(cells.end(), inputs.begin(), inputs.end());
    }
    if (listInputs && listOutputs)
    {
        cells.push_back("");
    }
    if (listOutputs)
    {
        cells.insert(cells.end(), outputs.begin(), outputs.end());
    }

    for (std::size_t i = 0; i < cells.size(); ++i)
    {
        if (i > 0)
        {
            llvm::outs() << ';';
        }
        llvm::outs() << cells[i];
    }
}

}  // namespace

int main(int argc, char** argv)
{
    llvm::InitLLVM y(argc, argv);

    const auto startTime = std::chrono::steady_clock::now();

    auto parsed = parseCli(argc, argv);
    if (!parsed)
    {
        llvm::errs() << llvm::toString(parsed.takeError()) << "\n";
        printUsage();
        return 1;
    }
    CliOptions options = *parsed;

    if (argc == 1 || options.helpRequested)
    {
        printHelp();
        return argc == 1 ? 1 : 0;
    }
    if (options.versionRequested)
    {
        llvm::outs() << "dsdlc " << llvmdsdl::kVersionString << "\n";
        return 0;
    }

    if (options.targetLanguage.empty())
    {
        llvm::errs() << "--target-language is required\n";
        printUsage();
        return 1;
    }
    if (!isKnownLanguage(options.targetLanguage))
    {
        llvm::errs() << "unknown --target-language value: " << options.targetLanguage << "\n";
        printUsage();
        return 1;
    }

    if (auto gated = validateLanguageGatedOptions(options); !gated)
    {
        llvm::errs() << llvm::toString(gated.takeError()) << "\n";
        return 1;
    }

    if (options.listInputs || options.listOutputs)
    {
        options.dryRun = true;
    }

    llvmdsdl::DiagnosticEngine diagnostics;

    const auto logVerbose = [&](int level, llvm::StringRef message) {
        if (options.verbose >= level)
        {
            llvm::errs() << "[dsdlc] " << message << "\n";
        }
    };

    logVerbose(1, "resolving target paths");
    llvmdsdl::TargetResolveOptions resolveOptions;
    resolveOptions.noTargetNamespaces = options.noTargetNamespaces;
    resolveOptions.lookupDirs         = options.lookupDirs;

    auto resolved = llvmdsdl::resolveTargets(options.positionalTargets, resolveOptions, diagnostics);
    if (!resolved)
    {
        printDiagnostics(diagnostics);
        llvm::consumeError(resolved.takeError());
        return 1;
    }

    // Support code is rendered from the compiler, not from definitions, so `only` and `always` have
    // work to do with no targets at all. Embedded targets need no files on disk either, so an empty
    // filesystem target set is only "nothing to do" when neither applies.
    const bool supportIsSelfSufficient = options.supportGeneration == llvmdsdl::SupportGeneration::Only ||
                                         options.supportGeneration == llvmdsdl::SupportGeneration::Always;
    if (resolved->explicitTargetFiles.empty() && options.builtinTargets.empty() && !supportIsSelfSufficient)
    {
        const auto outputRoot =
            isCodegenLanguage(options.targetLanguage) ? resolveOutputRoot(options.outDir) : "stdout";
        if (options.listInputs || options.listOutputs)
        {
            emitScsvLists({}, {}, options.listInputs, options.listOutputs);
        }
        printRunSummary(options.targetLanguage, outputRoot, 0U, std::chrono::steady_clock::now() - startTime);
        return 0;
    }

    logVerbose(1, "discovering and parsing definitions");
    const auto outputLanguages = namingLanguagesForTarget(options.targetLanguage);
    auto       ast =
        llvmdsdl::parseDefinitions(resolved->rootNamespaceDirs, resolved->lookupDirs, diagnostics, outputLanguages);
    if (!ast)
    {
        llvm::consumeError(ast.takeError());
        printDiagnostics(diagnostics);
        return 1;
    }

    {
        std::unordered_set<std::string> explicitFileSet;
        explicitFileSet.reserve(resolved->explicitTargetFiles.size());
        for (const auto& file : resolved->explicitTargetFiles)
        {
            explicitFileSet.insert(normalizePathForCompare(file));
        }
        for (auto& def : ast->definitions)
        {
            def.info.isExplicitTarget = explicitFileSet.contains(normalizePathForCompare(def.info.filePath));
        }
    }

    const bool useEmbeddedUavcan =
        !options.noEmbeddedUavcan && (options.targetLanguage == "mlir" || isCodegenLanguage(options.targetLanguage));

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

    std::optional<llvmdsdl::UavcanEmbeddedCatalog> embeddedCatalog;
    if (useEmbeddedUavcan)
    {
        logVerbose(1, "loading embedded uavcan catalog");
        auto loadedCatalog = llvmdsdl::loadUavcanEmbeddedCatalog(context, diagnostics);
        if (!loadedCatalog)
        {
            // Every failure path in the loader files a diagnostic first, and that one carries the
            // remediation notes. Printing the Error's text too would only repeat its first line.
            auto loadError = loadedCatalog.takeError();
            if (diagnostics.hasErrors())
            {
                llvm::consumeError(std::move(loadError));
            }
            else
            {
                llvm::errs() << llvm::toString(std::move(loadError)) << "\n";
            }
            printDiagnostics(diagnostics);
            return 1;
        }
        embeddedCatalog.emplace(std::move(*loadedCatalog));
    }

    // Resolve '+' targets before analysis so a typo fails here, loudly, rather than surviving as a
    // successful build that quietly generated nothing.
    std::unordered_set<std::string> builtinExplicitKeys;
    if (!options.builtinTargets.empty() && !embeddedCatalog)
    {
        diagnostics.error({"<cli>", 1, 1}, "embedded targets were requested but no embedded catalog is loaded");
        printDiagnostics(diagnostics);
        return 1;
    }
    for (const auto& selector : options.builtinTargets)
    {
        const auto expansion = llvmdsdl::expandEmbeddedCatalogSelector(*embeddedCatalog, selector);
        if (expansion.typeKeys.empty())
        {
            std::string message = "no embedded type matches target '+" + selector + "'";
            if (!expansion.suggestions.empty())
            {
                message += "; did you mean ";
                for (std::size_t i = 0; i < expansion.suggestions.size(); ++i)
                {
                    message += (i > 0) ? ((i + 1U == expansion.suggestions.size()) ? " or " : ", ") : "";
                    message += "'+" + expansion.suggestions[i] + "'";
                }
                message += "?";
            }
            diagnostics.error({"<cli>", 1, 1}, message);
            printDiagnostics(diagnostics);
            return 1;
        }
        builtinExplicitKeys.insert(expansion.typeKeys.begin(), expansion.typeKeys.end());
    }
    if (!builtinExplicitKeys.empty())
    {
        logVerbose(1, "embedded targets selected " + std::to_string(builtinExplicitKeys.size()) + " type(s)");
    }

    logVerbose(1, "running semantic analysis");
    llvmdsdl::AnalyzeOptions analyzeOptions;
    analyzeOptions.allowUnregulatedFixedPortId = options.allowUnregulatedFixedPortId;
    if (embeddedCatalog)
    {
        analyzeOptions.externalSemanticCatalog = &embeddedCatalog->semantic;
    }

    auto semantic = llvmdsdl::analyze(*ast, diagnostics, analyzeOptions);
    if (!semantic)
    {
        llvm::consumeError(semantic.takeError());
        printDiagnostics(diagnostics);
        return 1;
    }

    const auto localSemantic = *semantic;
    const auto mergedSemantic =
        embeddedCatalog ? mergeSemanticModulesPreferPrimary(localSemantic, embeddedCatalog->semantic) : localSemantic;

    // '+' targets are explicit in exactly the sense filesystem targets are; they just cannot be
    // marked by path, since embedded definitions have none. Note that a local definition sharing a
    // key shadows the embedded one here, because `mergedSemantic` prefers local.
    auto explicitKeys = collectExplicitKeys(localSemantic);
    // determinism-ok: the destination is itself a set, so the order these
    // arrive in cannot survive the insertion.
    explicitKeys.insert(builtinExplicitKeys.begin(), builtinExplicitKeys.end());
    if (explicitKeys.empty() && !supportIsSelfSufficient)
    {
        diagnostics.error({"<cli>", 1, 1}, "no explicit targets were resolved in the analyzed semantic graph");
        printDiagnostics(diagnostics);
        return 1;
    }

    const auto closureKeys  = computeDependencyClosure(mergedSemantic, explicitKeys);
    const auto selectedKeys = options.omitDependencies ? explicitKeys : closureKeys;

    const auto closureSemantic      = filterSemanticModule(mergedSemantic, closureKeys);
    const auto localClosureSemantic = filterSemanticModule(localSemantic, closureKeys);
    const auto inputsForListing     = collectInputFilesForClosure(mergedSemantic, closureKeys);

    auto finish =
        [&](llvm::StringRef outputRoot, std::vector<std::string> generatedOutputs, const bool forceFailure = false) {
            generatedOutputs = dedupSorted(std::move(generatedOutputs));
            if (options.listInputs || options.listOutputs)
            {
                emitScsvLists(inputsForListing, generatedOutputs, options.listInputs, options.listOutputs);
            }
            // After the outputs are final and only when this run actually wrote them. The listing
            // modes imply --dry-run, and pruning on a dry run would delete files while claiming to
            // have touched nothing.
            bool pruneFailed = false;
            if (!options.pruneManifest.empty() && !options.dryRun && !options.listInputs && !options.listOutputs)
            {
                llvmdsdl::PruneReport pruneReport;
                if (auto err = llvmdsdl::pruneStaleOutputs(options.pruneManifest,
                                                           std::filesystem::path(outputRoot.str()),
                                                           generatedOutputs,
                                                           &pruneReport))
                {
                    diagnostics.error({"<cli>", 1, 1}, llvm::toString(std::move(err)));
                    pruneFailed = true;
                }
                for (const auto& removed : pruneReport.removedFiles)
                {
                    logVerbose(1, "pruned stale output: " + removed);
                }
                for (const auto& removed : pruneReport.removedDirectories)
                {
                    logVerbose(1, "pruned empty directory: " + removed);
                }
            }
            printDiagnostics(diagnostics);
            printRunSummary(options.targetLanguage,
                            outputRoot,
                            static_cast<std::uint64_t>(generatedOutputs.size()),
                            std::chrono::steady_clock::now() - startTime);
            return (forceFailure || pruneFailed || diagnostics.hasErrors()) ? 1 : 0;
        };

    if (options.targetLanguage == "ast")
    {
        const auto filteredAst = filterAstModule(*ast, selectedKeys);
        if (!options.listInputs && !options.listOutputs)
        {
            llvm::outs() << llvmdsdl::printAST(filteredAst);
        }
        return finish("stdout", {});
    }

    if (options.targetLanguage == "mlir")
    {
        const auto selectedSemantic = filterSemanticModule(localSemantic, selectedKeys);
        auto       mlirModule       = llvmdsdl::lowerToMLIR(selectedSemantic, context, diagnostics);
        if (!mlirModule)
        {
            printDiagnostics(diagnostics);
            return 1;
        }
        if (embeddedCatalog)
        {
            if (auto err = llvmdsdl::appendEmbeddedUavcanSchemasForKeys(*embeddedCatalog,
                                                                        *mlirModule,
                                                                        selectedKeys,
                                                                        diagnostics))
            {
                llvm::errs() << llvm::toString(std::move(err)) << "\n";
                return finish("stdout", {}, true);
            }
        }
        if (!options.listInputs && !options.listOutputs)
        {
            mlirModule->print(llvm::outs());
            llvm::outs() << "\n";
        }
        return finish("stdout", {});
    }

    logVerbose(1, "lowering semantic model to MLIR");
    auto mlirModule = llvmdsdl::lowerToMLIR(localClosureSemantic, context, diagnostics);
    if (!mlirModule)
    {
        printDiagnostics(diagnostics);
        return 1;
    }
    if (embeddedCatalog)
    {
        if (auto err =
                llvmdsdl::appendEmbeddedUavcanSchemasForKeys(*embeddedCatalog, *mlirModule, closureKeys, diagnostics))
        {
            llvm::errs() << llvm::toString(std::move(err)) << "\n";
            return finish(resolveOutputRoot(options.outDir), {}, true);
        }
    }

    // determinism-ok: sorted on the next line, before anything reads it.
    std::vector<std::string> selectedTypeKeys(selectedKeys.begin(), selectedKeys.end());
    std::sort(selectedTypeKeys.begin(), selectedTypeKeys.end());

    std::vector<std::string>                                  generatedOutputs;
    std::unordered_map<std::string, std::vector<std::string>> generatedOutputRequiredTypeKeys;

    llvmdsdl::EmitWritePolicy writePolicy;
    writePolicy.dryRun                         = options.dryRun;
    writePolicy.noOverwrite                    = options.noOverwrite;
    writePolicy.fileMode                       = options.fileMode;
    writePolicy.recordedOutputs                = &generatedOutputs;
    writePolicy.recordedOutputRequiredTypeKeys = &generatedOutputRequiredTypeKeys;

    std::unique_ptr<llvmdsdl::DepfilePlanner> depfilePlanner;
    if (options.emitDepfiles)
    {
        const auto plannerBuildStart = std::chrono::steady_clock::now();
        // Built from the closure over the *merged* module, not the local one: embedded definitions
        // must be present as nodes for the planner to tell "resolved from the compiled-in catalog"
        // apart from "unknown type". Both used to produce an empty dependency list, so generated
        // uavcan sources were pinned to a rule with no prerequisites and never rebuilt.
        depfilePlanner =
            std::make_unique<llvmdsdl::DepfilePlanner>(closureSemantic, resolveToolchainStampPath(argv[0]));
        if (options.verbose >= 2)
        {
            const std::string message =
                "dep planner build: " +
                formatDurationMilliseconds(std::chrono::steady_clock::now() - plannerBuildStart);
            logVerbose(2, message);
        }
    }

    const auto emitDepfilesForGeneratedOutputs = [&](const std::vector<std::string>& regularOutputs) -> llvm::Error {
        if (!options.emitDepfiles)
        {
            return llvm::Error::success();
        }

        std::chrono::steady_clock::duration depResolutionElapsed{0};
        std::chrono::steady_clock::duration depWriteElapsed{0};

        for (const auto& output : regularOutputs)
        {
            // An output with no required type keys was rendered from content compiled into this
            // binary rather than from any definition -- a runtime header, a manifest, scaffolding --
            // so the binary is its input.
            const std::vector<std::string>* deps = &depfilePlanner->depsForCompilerOnlyOutput();

            if (const auto metadataIt = generatedOutputRequiredTypeKeys.find(output);
                metadataIt != generatedOutputRequiredTypeKeys.end())
            {
                const auto depResolutionStart = std::chrono::steady_clock::now();
                deps                          = &depfilePlanner->depsForRequiredTypeKeys(metadataIt->second);
                depResolutionElapsed += std::chrono::steady_clock::now() - depResolutionStart;
            }

            const auto depWriteStart = std::chrono::steady_clock::now();
            if (auto err = llvmdsdl::writeDepfileForGeneratedOutputPrepared(output, *deps, writePolicy))
            {
                return err;
            }
            depWriteElapsed += std::chrono::steady_clock::now() - depWriteStart;
        }

        if (options.verbose >= 2)
        {
            const std::string resolutionMessage = "dep resolution: " + formatDurationMilliseconds(depResolutionElapsed);
            const std::string writeMessage      = "depfile writes: " + formatDurationMilliseconds(depWriteElapsed);
            logVerbose(2, resolutionMessage);
            logVerbose(2, writeMessage);
        }

        return llvm::Error::success();
    };

    logVerbose(1, "running backend emission");

    // Emit-order verifier: when LLVMDSDL_EMIT_TRACE names a file, attach a trace sink to the selected string
    // emitter and dump its abstract emit-order op trace there after emission (see writeEmitTrace).
    const char* const              emitTraceEnv = std::getenv("LLVMDSDL_EMIT_TRACE");
    llvmdsdl::EmitTraceSink        emitTraceSink;
    llvmdsdl::EmitTraceSink* const emitTraceSinkPtr = (emitTraceEnv != nullptr) ? &emitTraceSink : nullptr;

    if (options.targetLanguage == "c")
    {
        llvmdsdl::CEmitOptions emitOptions;
        emitOptions.outDir                    = options.outDir;
        emitOptions.optimizeLoweredSerDes     = options.optimizeLoweredSerDes;
        emitOptions.emitDeprecationAttributes = options.emitDeprecationAttributes;
        emitOptions.selectedTypeKeys          = selectedTypeKeys;
        emitOptions.supportGeneration         = options.supportGeneration;
        emitOptions.writePolicy               = writePolicy;

        if (auto err = llvmdsdl::emitC(closureSemantic, *mlirModule, emitOptions, diagnostics))
        {
            llvm::errs() << llvm::toString(std::move(err)) << "\n";
            return finish(resolveOutputRoot(options.outDir), std::move(generatedOutputs), true);
        }
        const std::vector<std::string> regularOutputs = generatedOutputs;
        if (auto err = emitDepfilesForGeneratedOutputs(regularOutputs))
        {
            llvm::errs() << llvm::toString(std::move(err)) << "\n";
            return finish(resolveOutputRoot(options.outDir), std::move(generatedOutputs), true);
        }
        return finish(resolveOutputRoot(options.outDir), std::move(generatedOutputs));
    }

    if (options.targetLanguage == "cpp")
    {
        llvmdsdl::CppEmitOptions emitOptions;
        emitOptions.outDir                    = options.outDir;
        emitOptions.profile                   = options.cppProfile;
        emitOptions.optimizeLoweredSerDes     = options.optimizeLoweredSerDes;
        emitOptions.emitDeprecationAttributes = options.emitDeprecationAttributes;
        emitOptions.selectedTypeKeys          = selectedTypeKeys;
        emitOptions.supportGeneration         = options.supportGeneration;
        emitOptions.writePolicy               = writePolicy;

        if (auto err = llvmdsdl::emitCpp(closureSemantic, *mlirModule, emitOptions, diagnostics, emitTraceSinkPtr))
        {
            llvm::errs() << llvm::toString(std::move(err)) << "\n";
            return finish(resolveOutputRoot(options.outDir), std::move(generatedOutputs), true);
        }
        if (emitTraceSinkPtr != nullptr)
        {
            writeEmitTrace(emitTraceEnv, emitTraceSink);
        }
        const std::vector<std::string> regularOutputs = generatedOutputs;
        if (auto err = emitDepfilesForGeneratedOutputs(regularOutputs))
        {
            llvm::errs() << llvm::toString(std::move(err)) << "\n";
            return finish(resolveOutputRoot(options.outDir), std::move(generatedOutputs), true);
        }
        return finish(resolveOutputRoot(options.outDir), std::move(generatedOutputs));
    }

    if (options.targetLanguage == "rust")
    {
        llvmdsdl::RustEmitOptions emitOptions;
        emitOptions.outDir                    = options.outDir;
        emitOptions.crateName                 = options.rustCrateName;
        emitOptions.profile                   = options.rustProfile;
        emitOptions.runtimeSpecialization     = options.rustRuntimeSpecialization;
        emitOptions.memoryMode                = options.rustMemoryMode;
        emitOptions.inlineThresholdBytes      = options.rustInlineThresholdBytes;
        emitOptions.optimizeLoweredSerDes     = options.optimizeLoweredSerDes;
        emitOptions.emitDeprecationAttributes = options.emitDeprecationAttributes;
        emitOptions.selectedTypeKeys          = selectedTypeKeys;
        emitOptions.supportGeneration         = options.supportGeneration;
        emitOptions.writePolicy               = writePolicy;

        if (auto err = llvmdsdl::emitRust(closureSemantic, *mlirModule, emitOptions, diagnostics, emitTraceSinkPtr))
        {
            llvm::errs() << llvm::toString(std::move(err)) << "\n";
            return finish(resolveOutputRoot(options.outDir), std::move(generatedOutputs), true);
        }
        if (emitTraceSinkPtr != nullptr)
        {
            writeEmitTrace(emitTraceEnv, emitTraceSink);
        }
        const std::vector<std::string> regularOutputs = generatedOutputs;
        if (auto err = emitDepfilesForGeneratedOutputs(regularOutputs))
        {
            llvm::errs() << llvm::toString(std::move(err)) << "\n";
            return finish(resolveOutputRoot(options.outDir), std::move(generatedOutputs), true);
        }
        return finish(resolveOutputRoot(options.outDir), std::move(generatedOutputs));
    }

    if (options.targetLanguage == "go")
    {
        llvmdsdl::GoEmitOptions emitOptions;
        emitOptions.outDir                = options.outDir;
        emitOptions.moduleName            = options.goModuleName;
        emitOptions.optimizeLoweredSerDes = options.optimizeLoweredSerDes;
        emitOptions.selectedTypeKeys      = selectedTypeKeys;
        emitOptions.supportGeneration     = options.supportGeneration;
        emitOptions.writePolicy           = writePolicy;

        if (auto err = llvmdsdl::emitGo(closureSemantic, *mlirModule, emitOptions, diagnostics, emitTraceSinkPtr))
        {
            llvm::errs() << llvm::toString(std::move(err)) << "\n";
            return finish(resolveOutputRoot(options.outDir), std::move(generatedOutputs), true);
        }
        if (emitTraceSinkPtr != nullptr)
        {
            writeEmitTrace(emitTraceEnv, emitTraceSink);
        }
        const std::vector<std::string> regularOutputs = generatedOutputs;
        if (auto err = emitDepfilesForGeneratedOutputs(regularOutputs))
        {
            llvm::errs() << llvm::toString(std::move(err)) << "\n";
            return finish(resolveOutputRoot(options.outDir), std::move(generatedOutputs), true);
        }
        return finish(resolveOutputRoot(options.outDir), std::move(generatedOutputs));
    }

    if (options.targetLanguage == "ts")
    {
        llvmdsdl::TsEmitOptions emitOptions;
        emitOptions.outDir                = options.outDir;
        emitOptions.moduleName            = options.tsModuleName;
        emitOptions.runtimeSpecialization = options.tsRuntimeSpecialization;
        emitOptions.optimizeLoweredSerDes = options.optimizeLoweredSerDes;
        emitOptions.selectedTypeKeys      = selectedTypeKeys;
        emitOptions.supportGeneration     = options.supportGeneration;
        emitOptions.writePolicy           = writePolicy;

        if (auto err = llvmdsdl::emitTs(closureSemantic, *mlirModule, emitOptions, diagnostics, emitTraceSinkPtr))
        {
            llvm::errs() << llvm::toString(std::move(err)) << "\n";
            return finish(resolveOutputRoot(options.outDir), std::move(generatedOutputs), true);
        }
        if (emitTraceSinkPtr != nullptr)
        {
            writeEmitTrace(emitTraceEnv, emitTraceSink);
        }
        const std::vector<std::string> regularOutputs = generatedOutputs;
        if (auto err = emitDepfilesForGeneratedOutputs(regularOutputs))
        {
            llvm::errs() << llvm::toString(std::move(err)) << "\n";
            return finish(resolveOutputRoot(options.outDir), std::move(generatedOutputs), true);
        }
        return finish(resolveOutputRoot(options.outDir), std::move(generatedOutputs));
    }

    if (options.targetLanguage == "python")
    {
        llvmdsdl::PythonEmitOptions emitOptions;
        emitOptions.outDir                = options.outDir;
        emitOptions.packageName           = options.pyPackageName;
        emitOptions.runtimeSpecialization = options.pyRuntimeSpecialization;
        emitOptions.optimizeLoweredSerDes = options.optimizeLoweredSerDes;
        emitOptions.selectedTypeKeys      = selectedTypeKeys;
        emitOptions.supportGeneration     = options.supportGeneration;
        emitOptions.writePolicy           = writePolicy;

        if (auto err = llvmdsdl::emitPython(closureSemantic, *mlirModule, emitOptions, diagnostics, emitTraceSinkPtr))
        {
            llvm::errs() << llvm::toString(std::move(err)) << "\n";
            return finish(resolveOutputRoot(options.outDir), std::move(generatedOutputs), true);
        }
        if (emitTraceSinkPtr != nullptr)
        {
            writeEmitTrace(emitTraceEnv, emitTraceSink);
        }
        const std::vector<std::string> regularOutputs = generatedOutputs;
        if (auto err = emitDepfilesForGeneratedOutputs(regularOutputs))
        {
            llvm::errs() << llvm::toString(std::move(err)) << "\n";
            return finish(resolveOutputRoot(options.outDir), std::move(generatedOutputs), true);
        }
        return finish(resolveOutputRoot(options.outDir), std::move(generatedOutputs));
    }
    if (options.targetLanguage == "obj")
    {
        llvmdsdl::ObjectEmitOptions emitOptions;
        emitOptions.outDir           = options.outDir;
        emitOptions.targetEndianness = options.objTargetEndianness;
        emitOptions.targetTriple     = options.objTargetTriple;
        emitOptions.archiveName      = options.objArchiveName;
        emitOptions.noArchive        = options.objNoArchive;
        emitOptions.abiLanguage =
            (options.objAbiLanguage == "cpp") ? llvmdsdl::ObjectAbiLanguage::Cpp : llvmdsdl::ObjectAbiLanguage::C;
        emitOptions.compileJobs           = options.jobs;
        emitOptions.optimizeLoweredSerDes = options.optimizeLoweredSerDes;
        emitOptions.selectedTypeKeys      = selectedTypeKeys;
        emitOptions.writePolicy           = writePolicy;

        if (auto err = llvmdsdl::emitObject(closureSemantic, *mlirModule, emitOptions, diagnostics))
        {
            llvm::errs() << llvm::toString(std::move(err)) << "\n";
            return finish(resolveOutputRoot(options.outDir), std::move(generatedOutputs), true);
        }
        const std::vector<std::string> regularOutputs = generatedOutputs;
        if (auto err = emitDepfilesForGeneratedOutputs(regularOutputs))
        {
            llvm::errs() << llvm::toString(std::move(err)) << "\n";
            return finish(resolveOutputRoot(options.outDir), std::move(generatedOutputs), true);
        }
        return finish(resolveOutputRoot(options.outDir), std::move(generatedOutputs));
    }

    llvm::errs() << "Unhandled language path: " << options.targetLanguage << "\n";
    return 1;
}
