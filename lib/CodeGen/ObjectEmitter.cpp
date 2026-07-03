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
        cppStageOptions.writePolicy.recordedOutputs                = nullptr;
        cppStageOptions.writePolicy.recordedOutputRequiredTypeKeys = nullptr;

        std::vector<std::filesystem::path> cppSources;
        if (auto err = emitCppObjectAbiStage(semantic, loweredFacts, cppStageOptions, &cppSources))
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
