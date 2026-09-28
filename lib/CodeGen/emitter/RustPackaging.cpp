//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Writes the manifest of a generated Rust crate.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/CodeGen/emitter/RustPackaging.h"

#include <cstddef>
#include <sstream>
#include <string>
#include <vector>

#include "llvmdsdl/CodeGen/emitter/Rust.h"
#include "llvmdsdl/Version.h"

namespace llvmdsdl::emitter::rust
{

std::string renderCargoToml(const Options& options)
{
    const auto rustProfileName = [&options]() -> const char* {
        return options.profile == Profile::Std ? "std" : "no-std-alloc";
    };
    const auto rustRuntimeSpecializationName = [&options]() -> const char* {
        return options.runtimeSpecialization == RuntimeSpecialization::Fast ? "fast" : "portable";
    };
    const auto rustMemoryModeName = [&options]() -> const char* {
        return options.memoryMode == MemoryMode::InlineThenPool ? "inline-then-pool" : "max-inline";
    };

    std::ostringstream out;
    out << "[package]\n";
    out << "name = \"" << options.crateName << "\"\n";
    out << "version = \"" << llvmdsdl::kVersionString << "\"\n";
    out << "edition = \"2021\"\n\n";
    out << "[package.metadata.llvmdsdl]\n";
    out << "generator-version = \"" << llvmdsdl::kVersionString << "\"\n";
    out << "rust-profile = \"" << rustProfileName() << "\"\n";
    out << "rust-runtime-specialization = \"" << rustRuntimeSpecializationName() << "\"\n";
    out << "rust-memory-mode = \"" << rustMemoryModeName() << "\"\n";
    out << "rust-inline-threshold-bytes = " << options.inlineThresholdBytes << "\n\n";

    out << "[lib]\n";
    out << "path = \"src/lib.rs\"\n\n";

    out << "[features]\n";
    std::vector<std::string> defaultFeatures;
    if (options.profile == Profile::Std)
    {
        defaultFeatures.emplace_back("std");
    }
    if (options.runtimeSpecialization == RuntimeSpecialization::Fast)
    {
        defaultFeatures.emplace_back("runtime-fast");
    }
    out << "default = [";
    for (std::size_t i = 0; i < defaultFeatures.size(); ++i)
    {
        if (i != 0)
        {
            out << ", ";
        }
        out << "\"" << defaultFeatures[i] << "\"";
    }
    out << "]\n";
    out << "std = []\n";
    out << "runtime-fast = []\n";
    return out.str();
}

}  // namespace llvmdsdl::emitter::rust
