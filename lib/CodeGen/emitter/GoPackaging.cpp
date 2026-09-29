//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Writes the module file of a generated Go module.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/CodeGen/emitter/GoPackaging.h"

#include <sstream>
#include <string>

#include <llvm/ADT/StringRef.h>

#include "llvmdsdl/CodeGen/emitter/Go.h"

namespace llvmdsdl::emitter::go
{

std::string renderGoMod(const Options& options, const llvm::StringRef generatedComment)
{
    std::ostringstream out;
    out << generatedComment.str() << "\n";
    out << "module " << options.moduleName << "\n\n";
    out << "go 1.22\n";
    return out.str();
}

}  // namespace llvmdsdl::emitter::go
