//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Object-code emission flow built on top of generated C artifacts.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/CodeGen/ObjectEmitter.h"

#include "llvmdsdl/CodeGen/CppObjectAbiEmitter.h"
#include <llvm/ADT/SmallVector.h>
#include <llvm/ADT/StringExtras.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/Program.h>
#include <llvm/Support/StringSaver.h>
#include <mlir/IR/Builders.h>
#include <mlir/IR/BuiltinOps.h>
#include <mlir/Pass/Pass.h>
#include <mlir/Pass/PassManager.h>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include "llvmdsdl/CodeGen/CEmitter.h"
#include "llvmdsdl/CodeGen/MlirLoweredFacts.h"
#include "llvmdsdl/Support/Diagnostics.h"
#include "llvmdsdl/Transforms/Passes.h"

namespace llvmdsdl
{
namespace
{

std::string absoluteNormalizedPath(const std::filesystem::path& path)
{
    std::error_code ec;
    const auto      absolute = std::filesystem::absolute(path, ec);
    if (ec)
    {
        return path.lexically_normal().string();
    }
    return absolute.lexically_normal().string();
}

/// @brief Accepts only a conservative target-triple charset: hyphen-separated alphanumeric
///        components (`[A-Za-z0-9._-]`, not starting with `-`). LLVM triples fit this; anything with
///        whitespace, path/shell metacharacters, or a leading dash is rejected so a hostile value
///        can never reach the compiler command line as anything but an inert `--target=` payload.
bool isSafeTargetTriple(llvm::StringRef triple)
{
    if (triple.empty() || triple.size() > 128U || !llvm::isAlnum(triple.front()))
    {
        return false;
    }
    for (const char c : triple)
    {
        if (!(llvm::isAlnum(c) || c == '-' || c == '_' || c == '.'))
        {
            return false;
        }
    }
    return true;
}

/// @brief Accepts only a single safe filename component (used for the archive name): non-empty,
///        `[A-Za-z0-9._-]`, and not a path separator or parent/self reference.
bool isSafePathComponent(llvm::StringRef name)
{
    if (name.empty() || name.size() > 128U || name == "." || name == "..")
    {
        return false;
    }
    for (const char c : name)
    {
        if (!(llvm::isAlnum(c) || c == '-' || c == '_' || c == '.'))
        {
            return false;
        }
    }
    return true;
}

/// @brief True when `candidate` normalizes to a location at or under `root` (no `..` traversal escape).
bool isPathWithinRoot(const std::filesystem::path& root, const std::filesystem::path& candidate)
{
    const std::filesystem::path normalizedRoot(absoluteNormalizedPath(root));
    const std::filesystem::path normalizedCandidate(absoluteNormalizedPath(candidate));
    const std::filesystem::path relative = normalizedCandidate.lexically_relative(normalizedRoot);
    if (relative.empty())
    {
        return false;  // unrelated to root
    }
    return *relative.begin() != std::filesystem::path("..");
}

void recordOutput(const EmitWritePolicy&          policy,
                  const std::filesystem::path&    path,
                  const std::vector<std::string>& requiredTypeKeys)
{
    const std::string normalizedPath = absoluteNormalizedPath(path);
    if (policy.recordedOutputs)
    {
        policy.recordedOutputs->push_back(normalizedPath);
    }
    if (policy.recordedOutputRequiredTypeKeys && !requiredTypeKeys.empty())
    {
        policy.recordedOutputRequiredTypeKeys->insert_or_assign(normalizedPath, requiredTypeKeys);
    }
}

std::optional<std::filesystem::perms> permsFromMode(const std::uint32_t mode)
{
    using Perm = std::filesystem::perms;
    Perm out   = Perm::none;

    if ((mode & 0400U) != 0U)
    {
        out |= Perm::owner_read;
    }
    if ((mode & 0200U) != 0U)
    {
        out |= Perm::owner_write;
    }
    if ((mode & 0100U) != 0U)
    {
        out |= Perm::owner_exec;
    }
    if ((mode & 0040U) != 0U)
    {
        out |= Perm::group_read;
    }
    if ((mode & 0020U) != 0U)
    {
        out |= Perm::group_write;
    }
    if ((mode & 0010U) != 0U)
    {
        out |= Perm::group_exec;
    }
    if ((mode & 0004U) != 0U)
    {
        out |= Perm::others_read;
    }
    if ((mode & 0002U) != 0U)
    {
        out |= Perm::others_write;
    }
    if ((mode & 0001U) != 0U)
    {
        out |= Perm::others_exec;
    }
    return out;
}

llvm::Error setPathMode(const std::filesystem::path& path, const std::uint32_t mode)
{
    const auto perms = permsFromMode(mode);
    if (!perms)
    {
        return llvm::Error::success();
    }
    std::error_code ec;
    std::filesystem::permissions(path, *perms, std::filesystem::perm_options::replace, ec);
    if (ec)
    {
        return llvm::createStringError(ec, "failed to set mode on %s", path.string().c_str());
    }
    return llvm::Error::success();
}

/// @brief Publishes the headers of a staging tree into the output directory as declared outputs.
///
/// @details
/// This backend compiles the sources it generates, so the sources are an intermediate the caller
/// never sees -- but the *headers* are the only way to call what ends up in the archive, and an
/// archive with no declared header interface is half a deliverable. Staging them under a dot-prefixed
/// directory and leaving them out of `--list-outputs` meant a build system could not find them at
/// all, so a consumer had to run the `c` backend a second time purely for its headers, and keep two
/// invocations' options in agreement or get headers describing something the archive did not
/// implement.
///
/// The published layout mirrors the staging tree, which is the same layout the `c` backend produces,
/// so `-I<outdir>` is all a consumer needs.
///
/// Recording happens whether or not anything is written, matching `writeGeneratedFile`: under
/// `--dry-run` (which `--list-outputs` implies) the staged files do not exist on disk, and a listing
/// that omitted them would describe an output tree the real run does not produce.
llvm::Error publishStagedHeaders(const std::vector<std::string>&    staged,
                                 const std::filesystem::path&       stageRoot,
                                 const std::filesystem::path&       outRoot,
                                 const EmitWritePolicy&             policy,
                                 const std::vector<std::string>&    selectedTypeKeys)
{
    for (const auto& entry : staged)
    {
        const std::filesystem::path source(entry);
        const auto                  extension = source.extension();
        if (extension != ".h" && extension != ".hpp")
        {
            continue;
        }

        std::error_code ec;
        auto            relative = std::filesystem::relative(source, stageRoot, ec);
        if (ec || relative.empty())
        {
            relative = source.filename();
        }
        const std::filesystem::path destination = outRoot / relative;
        if (!isPathWithinRoot(outRoot, destination))
        {
            return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                           "refusing to publish header outside the output directory: %s",
                                           destination.string().c_str());
        }

        if (!policy.dryRun)
        {
            std::filesystem::create_directories(destination.parent_path(), ec);
            if (ec)
            {
                return llvm::createStringError(ec,
                                               "failed to create header output directory %s",
                                               destination.parent_path().string().c_str());
            }
            // Generated files are written read-only by default, and copy_file will not overwrite
            // one, so a rerun has to remove the previous copy first.
            std::filesystem::remove(destination, ec);
            std::filesystem::copy_file(source,
                                       destination,
                                       std::filesystem::copy_options::overwrite_existing,
                                       ec);
            if (ec)
            {
                return llvm::createStringError(ec,
                                               "failed to publish header %s to %s",
                                               source.string().c_str(),
                                               destination.string().c_str());
            }
            if (auto err = setPathMode(destination, policy.fileMode))
            {
                return err;
            }
        }

        recordOutput(policy, destination, selectedTypeKeys);
    }
    return llvm::Error::success();
}

std::optional<std::string> environmentValue(const char* name)
{
    if (name == nullptr)
    {
        return std::nullopt;
    }
    if (const char* value = std::getenv(name))
    {
        if (*value != '\0')
        {
            return std::string(value);
        }
    }
    return std::nullopt;
}

llvm::Expected<std::string> resolveProgram(const char* envVar, const char* fallbackProgramName)
{
    if (const auto env = environmentValue(envVar))
    {
        return *env;
    }
    auto found = llvm::sys::findProgramByName(fallbackProgramName);
    if (!found)
    {
        return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                       "failed to find required tool '%s'",
                                       fallbackProgramName);
    }
    return *found;
}

llvm::Error executeCommand(llvm::StringRef              program,
                           const std::vector<std::string>& args,
                           llvm::StringRef              failContext)
{
    llvm::BumpPtrAllocator  allocator;
    llvm::StringSaver       saver(allocator);
    llvm::SmallVector<llvm::StringRef, 16> argv;
    argv.reserve(args.size() + 1U);
    argv.push_back(saver.save(program));
    for (const auto& arg : args)
    {
        argv.push_back(saver.save(arg));
    }

    std::string errMsg;
    const int rc = llvm::sys::ExecuteAndWait(program, argv, std::nullopt, {}, 0, 0, &errMsg);
    if (rc != 0)
    {
        return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                       "%s failed (exit %d): %s",
                                       failContext.str().c_str(),
                                       rc,
                                       errMsg.c_str());
    }
    return llvm::Error::success();
}

bool hasClangStyleTargetFlag(llvm::StringRef compilerProgram)
{
    const std::string basename = std::filesystem::path(compilerProgram.str()).filename().string();
    return llvm::StringRef(basename).contains_insensitive("clang");
}

struct CompileTask final
{
    std::string                 compiler;
    std::vector<std::string>    args;
    std::string                 failContext;
    std::filesystem::path       objectPath;
};

std::size_t resolveCompileJobCount(const ObjectEmitOptions& options, const std::size_t taskCount)
{
    if (taskCount == 0U)
    {
        return 0U;
    }
    std::uint32_t jobCount = options.compileJobs;
    if (jobCount == 0U)
    {
        jobCount = std::thread::hardware_concurrency();
    }
    if (jobCount == 0U)
    {
        jobCount = 1U;
    }
    const auto requested = static_cast<std::size_t>(jobCount);
    return (requested < taskCount) ? requested : taskCount;
}

llvm::Error runCompileTasks(const std::vector<CompileTask>& tasks, const ObjectEmitOptions& options)
{
    if (tasks.empty() || options.writePolicy.dryRun)
    {
        return llvm::Error::success();
    }

    const std::size_t workerCount = resolveCompileJobCount(options, tasks.size());
    if (workerCount == 0U)
    {
        return llvm::Error::success();
    }

    std::mutex                stateMutex;
    std::size_t               nextTaskIndex = 0U;
    bool                      stopScheduling{false};
    std::optional<std::string> firstFailure;

    auto worker = [&]() {
        while (true)
        {
            std::size_t taskIndex = 0U;
            {
                std::lock_guard<std::mutex> lock(stateMutex);
                if (stopScheduling || nextTaskIndex >= tasks.size())
                {
                    return;
                }
                taskIndex = nextTaskIndex++;
            }

            const auto& task = tasks[taskIndex];
            if (auto err = executeCommand(task.compiler, task.args, task.failContext))
            {
                const std::string message = llvm::toString(std::move(err));
                std::lock_guard<std::mutex> lock(stateMutex);
                if (!firstFailure)
                {
                    firstFailure = message;
                }
                stopScheduling = true;
                return;
            }
            if (auto err = setPathMode(task.objectPath, options.writePolicy.fileMode))
            {
                const std::string message = llvm::toString(std::move(err));
                std::lock_guard<std::mutex> lock(stateMutex);
                if (!firstFailure)
                {
                    firstFailure = message;
                }
                stopScheduling = true;
                return;
            }
        }
    };

    std::vector<std::thread> workers;
    workers.reserve(workerCount);
    for (std::size_t i = 0U; i < workerCount; ++i)
    {
        workers.emplace_back(worker);
    }
    for (auto& thread : workers)
    {
        thread.join();
    }

    if (firstFailure)
    {
        return llvm::createStringError(llvm::inconvertibleErrorCode(), "%s", firstFailure->c_str());
    }
    return llvm::Error::success();
}

}  // namespace

llvm::Error emitObject(const SemanticModule&    semantic,
                       mlir::ModuleOp           module,
                       const ObjectEmitOptions& options,
                       DiagnosticEngine&        diagnostics)
{
    const auto targetEndianness = llvm::StringRef(options.targetEndianness);
    if (targetEndianness != "little" && targetEndianness != "big")
    {
        return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                       "unsupported --target-endianness value '%s' (expected little or big)",
                                       options.targetEndianness.c_str());
    }

    if (!options.targetTriple.empty() && !isSafeTargetTriple(options.targetTriple))
    {
        return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                       "invalid --target-triple '%s'; expected hyphen-separated alphanumeric "
                                       "components ([A-Za-z0-9._-])",
                                       options.targetTriple.c_str());
    }
    if (!options.archiveName.empty() && !isSafePathComponent(options.archiveName))
    {
        return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                       "invalid --obj-archive-name '%s'; expected a single filename component "
                                       "([A-Za-z0-9._-], no path separators or parent references)",
                                       options.archiveName.c_str());
    }

    const std::filesystem::path outRoot(options.outDir);
    const std::filesystem::path cppStageRoot = outRoot / ".obj_stage_cpp";
    const std::filesystem::path cStageRoot =
        (options.abiLanguage == ObjectAbiLanguage::Cpp) ? (cppStageRoot / "c") : (outRoot / ".obj_stage_c");
    std::error_code             ec;
    std::filesystem::create_directories(outRoot, ec);
    if (ec)
    {
        return llvm::createStringError(ec, "failed to create object output directory %s", outRoot.string().c_str());
    }

    auto workingModule = mlir::OwningOpRef<mlir::ModuleOp>(mlir::cast<mlir::ModuleOp>(module->clone()));
    {
        mlir::OpBuilder attrBuilder(module.getContext());
        (*workingModule)->setAttr("llvmdsdl.target_endianness", attrBuilder.getStringAttr(options.targetEndianness));
    }
    mlir::PassManager pm(module.getContext());
    pm.addPass(createLowerDSDLExecPass());
    pm.addPass(createDSDLAnnotateAliasabilityPass());
    pm.addPass(createDSDLEndianLegalizePass());
    if (options.optimizeLoweredSerDes)
    {
        addOptimizeLoweredSerDesPipeline(pm);
    }
    if (mlir::failed(pm.run(*workingModule)))
    {
        diagnostics.error({"<mlir>", 1, 1}, "object backend pass pipeline failed");
        return llvm::createStringError(llvm::inconvertibleErrorCode(), "object backend pass pipeline failed");
    }

    CEmitOptions cOptions;
    cOptions.outDir                = cStageRoot.string();
    cOptions.optimizeLoweredSerDes = options.optimizeLoweredSerDes;
    // Opted out of the default deliberately. The C emitted here is an intermediate that this backend
    // compiles itself and the user never sees, so a deprecation diagnostic on it has no audience.
    cOptions.emitDeprecationAttributes = false;
    cOptions.selectedTypeKeys      = options.selectedTypeKeys;
    cOptions.writePolicy           = options.writePolicy;
    cOptions.writePolicy.recordedOutputs                = nullptr;
    cOptions.writePolicy.recordedOutputRequiredTypeKeys = nullptr;

    std::vector<std::string> cGenerated;
    cOptions.writePolicy.recordedOutputs = &cGenerated;

    if (auto err = emitC(semantic, *workingModule, cOptions, diagnostics))
    {
        return err;
    }

    // The C headers are the interface to the archive in the C ABI lane, and the interface to the
    // C-callable shim symbols in the C++ one, so they are published either way.
    if (auto err = publishStagedHeaders(cGenerated,
                                        cStageRoot,
                                        outRoot,
                                        options.writePolicy,
                                        options.selectedTypeKeys))
    {
        return err;
    }

    std::vector<std::filesystem::path> sources;
    for (const auto& output : cGenerated)
    {
        const std::filesystem::path p(output);
        if (p.extension() == ".c")
        {
            sources.push_back(p);
        }
    }
    if (sources.empty())
    {
        return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                       "object emission found no generated C translation units");
    }

    auto cCompilerOrErr = resolveProgram("CC", "cc");
    if (!cCompilerOrErr)
    {
        return cCompilerOrErr.takeError();
    }
    const std::string cCompiler = *cCompilerOrErr;

    if (!options.targetTriple.empty() && !hasClangStyleTargetFlag(cCompiler))
    {
        return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                       "compiler '%s' does not support explicit target triples in object backend; "
                                       "set CC to clang/clang++ or omit --target-triple",
                                       cCompiler.c_str());
    }

    std::vector<std::filesystem::path> objectOutputs;
    objectOutputs.reserve(sources.size());
    std::vector<CompileTask> compileTasks;
    compileTasks.reserve(sources.size());

    for (const auto& source : sources)
    {
        std::filesystem::path relative = source.filename();
        std::error_code       relEc;
        const auto maybeRel = std::filesystem::relative(source, cStageRoot, relEc);
        if (!relEc && !maybeRel.empty())
        {
            relative = maybeRel;
        }
        std::filesystem::path objectPath = outRoot / relative;
        objectPath.replace_extension(".o");
        if (!isPathWithinRoot(outRoot, objectPath))
        {
            return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                           "refusing to write object file outside the output directory: %s",
                                           objectPath.string().c_str());
        }
        std::filesystem::create_directories(objectPath.parent_path(), ec);
        if (ec)
        {
            return llvm::createStringError(ec, "failed to create object output directory %s", objectPath.string().c_str());
        }

        std::vector<std::string> args;
        args.push_back("-c");
        args.push_back("-O2");
        args.push_back("-I" + cStageRoot.string());
        args.push_back("-DLLVMDSDL_TARGET_ENDIANNESS_" +
                       (targetEndianness == "big" ? std::string("BIG=1") : std::string("LITTLE=1")));
        if (!options.targetTriple.empty())
        {
            args.push_back("--target=" + options.targetTriple);
        }
        args.push_back(source.string());
        args.push_back("-o");
        args.push_back(objectPath.string());

        compileTasks.push_back(CompileTask{cCompiler, std::move(args), "C compiler invocation", objectPath});
    }

    if (auto err = runCompileTasks(compileTasks, options))
    {
        return err;
    }
    for (const auto& task : compileTasks)
    {
        recordOutput(options.writePolicy, task.objectPath, options.selectedTypeKeys);
        objectOutputs.push_back(task.objectPath);
    }

    if (options.abiLanguage == ObjectAbiLanguage::Cpp)
    {
        LoweredFactsMap loweredFacts;
        if (!collectLoweredFactsFromMlir(semantic,
                                         module,
                                         diagnostics,
                                         "obj-cpp",
                                         &loweredFacts,
                                         options.optimizeLoweredSerDes))
        {
            return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                           "failed to collect lowered facts for obj-cpp lane");
        }

        CppObjectAbiEmitOptions cppStageOptions;
        cppStageOptions.stageRoot         = cppStageRoot;
        cppStageOptions.cStageRoot        = cStageRoot;
        cppStageOptions.selectedTypeKeys  = options.selectedTypeKeys;
        cppStageOptions.writePolicy       = options.writePolicy;
        std::vector<std::string> cppGenerated;
        cppStageOptions.writePolicy.recordedOutputs                = &cppGenerated;
        cppStageOptions.writePolicy.recordedOutputRequiredTypeKeys = nullptr;

        std::vector<std::filesystem::path> cppSources;
        if (auto err = emitCppObjectAbiStage(semantic, loweredFacts, cppStageOptions, &cppSources))
        {
            return err;
        }

        // The C++ ABI headers are what a C++ consumer calls through; the C ones published above
        // cover the shim. Relative to the C++ stage root, so the published tree mirrors it.
        if (auto err = publishStagedHeaders(cppGenerated,
                                            cppStageRoot,
                                            outRoot,
                                            options.writePolicy,
                                            options.selectedTypeKeys))
        {
            return err;
        }

        auto cxxCompilerOrErr = resolveProgram("CXX", "c++");
        if (!cxxCompilerOrErr)
        {
            return cxxCompilerOrErr.takeError();
        }
        const std::string cxxCompiler = *cxxCompilerOrErr;
        if (!options.targetTriple.empty() && !hasClangStyleTargetFlag(cxxCompiler))
        {
            return llvm::createStringError(
                llvm::inconvertibleErrorCode(),
                "compiler '%s' does not support explicit target triples in object backend; "
                "set CXX to clang++ or omit --target-triple",
                cxxCompiler.c_str());
        }

        objectOutputs.reserve(objectOutputs.size() + cppSources.size());
        std::vector<CompileTask> cppCompileTasks;
        cppCompileTasks.reserve(cppSources.size());
        for (const auto& source : cppSources)
        {
            std::filesystem::path relative = source.filename();
            std::error_code       relEc;
            const auto maybeRel = std::filesystem::relative(source, cppStageRoot, relEc);
            if (!relEc && !maybeRel.empty())
            {
                relative = maybeRel;
            }

            std::filesystem::path objectPath = outRoot / relative;
            objectPath.replace_extension(".o");
            if (!isPathWithinRoot(outRoot, objectPath))
            {
                return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                               "refusing to write object file outside the output directory: %s",
                                               objectPath.string().c_str());
            }
            std::filesystem::create_directories(objectPath.parent_path(), ec);
            if (ec)
            {
                return llvm::createStringError(ec,
                                               "failed to create object output directory %s",
                                               objectPath.string().c_str());
            }

            std::vector<std::string> args;
            args.push_back("-c");
            args.push_back("-O2");
            args.push_back("-std=c++17");
            args.push_back("-I" + cppStageRoot.string());
            args.push_back("-I" + cStageRoot.string());
            args.push_back("-I" + outRoot.string());
            args.push_back("-DLLVMDSDL_TARGET_ENDIANNESS_" +
                           (targetEndianness == "big" ? std::string("BIG=1") : std::string("LITTLE=1")));
            if (!options.targetTriple.empty())
            {
                args.push_back("--target=" + options.targetTriple);
            }
            args.push_back(source.string());
            args.push_back("-o");
            args.push_back(objectPath.string());

            cppCompileTasks.push_back(
                CompileTask{cxxCompiler, std::move(args), "C++ compiler invocation", objectPath});
        }

        if (auto err = runCompileTasks(cppCompileTasks, options))
        {
            return err;
        }
        for (const auto& task : cppCompileTasks)
        {
            recordOutput(options.writePolicy, task.objectPath, options.selectedTypeKeys);
            objectOutputs.push_back(task.objectPath);
        }
    }

    if (!options.noArchive)
    {
        auto arOrErr = resolveProgram("AR", "ar");
        if (!arOrErr)
        {
            return arOrErr.takeError();
        }
        std::filesystem::path archivePath = outRoot / (options.archiveName + ".a");
        if (!isPathWithinRoot(outRoot, archivePath))
        {
            return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                           "refusing to write archive outside the output directory: %s",
                                           archivePath.string().c_str());
        }

        std::vector<std::string> args;
        args.push_back("rcs");
        args.push_back(archivePath.string());
        for (const auto& objectPath : objectOutputs)
        {
            args.push_back(objectPath.string());
        }

        if (!options.writePolicy.dryRun)
        {
            if (auto err = executeCommand(*arOrErr, args, "archive invocation"))
            {
                return err;
            }
            if (auto err = setPathMode(archivePath, options.writePolicy.fileMode))
            {
                return err;
            }
        }
        recordOutput(options.writePolicy, archivePath, options.selectedTypeKeys);
    }

    return llvm::Error::success();
}

}  // namespace llvmdsdl
