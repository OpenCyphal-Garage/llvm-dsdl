//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Writes the manifest of a generated TypeScript package.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/CodeGen/emitter/TsPackaging.h"

#include <sstream>
#include <string>

#include "llvmdsdl/CodeGen/emitter/Ts.h"
#include "llvmdsdl/Version.h"

namespace llvmdsdl::emitter::ts
{

std::string renderPackageJson(const Options& options)
{
    const auto* const runtimeSpecialization =
        options.runtimeSpecialization == RuntimeSpecialization::Fast ? "fast" : "portable";
    std::ostringstream out;
    out << "{\n";
    out << R"(  "name": ")" << options.moduleName << "\",\n";
    out << R"(  "version": ")" << llvmdsdl::kVersionString << "\",\n";
    out << "  \"type\": \"module\",\n";
    out << "  \"llvmdsdl\": {\n";
    out << R"(    "generatorVersion": ")" << llvmdsdl::kVersionString << "\",\n";
    out << R"(    "tsRuntimeSpecialization": ")" << runtimeSpecialization << "\"\n";
    out << "  }\n";
    out << "}\n";
    return out.str();
}

}  // namespace llvmdsdl::emitter::ts
