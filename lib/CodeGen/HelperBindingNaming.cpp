//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Implements shared naming helpers for lowered helper-binding symbols.
///
/// This utility preserves consistent helper binding naming across backends.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/CodeGen/HelperBindingNaming.h"
#include "llvmdsdl/Support/NamingPolicy.h"
#include <llvm/ADT/StringRef.h>
#include <string>

namespace llvmdsdl
{

std::string renderHelperBindingIdentifier(const CodegenNamingLanguage language, const llvm::StringRef helperSymbol)
{
    // The MLIR symbol separates its fields with `__`. C++ reserves any identifier containing a double
    // underscore, so for that language the runs are collapsed -- after the prefix is joined, since
    // the symbol itself begins with one and the seam would otherwise make a fresh pair. The binding
    // names a function-local lambda, so it has to be unique only among the bindings of one section;
    // two symbols there differ by helper kind and operand index, neither of which the collapse
    // touches. No other language reserves an interior double underscore, and none of them carry a
    // leading one here, so they keep the symbol as it is.
    std::string joined = "mlir_" + codegenSanitizeIdentifier(language, helperSymbol);
    if (language != CodegenNamingLanguage::Cpp)
    {
        return joined;
    }

    std::string collapsed;
    collapsed.reserve(joined.size());
    for (const char c : joined)
    {
        if (c == '_' && !collapsed.empty() && collapsed.back() == '_')
        {
            continue;
        }
        collapsed.push_back(c);
    }
    return collapsed;
}

}  // namespace llvmdsdl
