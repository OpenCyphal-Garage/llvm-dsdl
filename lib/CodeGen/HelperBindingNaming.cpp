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
#include "llvmdsdl/Support/DefinitionNaming.h"
#include "llvmdsdl/Support/NamingPolicy.h"
#include <llvm/ADT/StringRef.h>
#include <cstddef>
#include <string>

namespace llvmdsdl
{

namespace
{

/// @brief Returns @p name with every run of underscores reduced to one.
std::string collapseUnderscoreRuns(const llvm::StringRef name)
{
    std::string out;
    out.reserve(name.size());
    for (const char c : name)
    {
        if (c == '_' && !out.empty() && out.back() == '_')
        {
            continue;
        }
        out.push_back(c);
    }
    return out;
}

}  // namespace

std::string renderHelperBindingIdentifier(const CodegenNamingLanguage language, const llvm::StringRef helperSymbol)
{
    // The MLIR symbol separates its fields with `__`. C++ reserves any identifier containing a double
    // underscore, so for that language the runs are collapsed -- after the prefix is joined, since
    // the symbol itself begins with one and the seam would otherwise make a fresh pair. The symbol
    // carries the schema, so the collapsed form stays unique across the whole of a file's scope;
    // two helpers of one definition differ by kind, section and operand index, none of which the
    // collapse touches. No other language reserves an interior double underscore, and none of them
    // carry a leading one here, so they keep the symbol as it is.
    std::string joined = "mlir_" + codegenSanitizeIdentifier(language, helperSymbol);
    if (language != CodegenNamingLanguage::Cpp)
    {
        return joined;
    }

    return collapseUnderscoreRuns(joined);
}

std::string renderScopeLocalHelperName(const CodegenNamingLanguage language,
                                       const llvm::StringRef       helperSymbol,
                                       const llvm::StringRef       schemaSymbol)
{
    llvm::StringRef rest = helperSymbol;
    rest.consume_front(kPlanHelperSymbolPrefix);

    // The schema component sits between the kind and the role suffix, so removing it joins two
    // separators into a run that the collapse below takes back down to one. Removing the first
    // occurrence alone is deliberate: a field or section named after its own schema would
    // otherwise lose the part that distinguishes it.
    std::string  stripped = rest.str();
    const size_t at       = schemaSymbol.empty() ? std::string::npos : stripped.find(schemaSymbol);
    if (at != std::string::npos)
    {
        stripped.erase(at, schemaSymbol.size());
    }

    std::string trimmed = collapseUnderscoreRuns(stripped);
    while (!trimmed.empty() && trimmed.front() == '_')
    {
        trimmed.erase(trimmed.begin());
    }
    while (!trimmed.empty() && trimmed.back() == '_')
    {
        trimmed.pop_back();
    }

    // A symbol that strips to nothing keeps the whole of what it had, so that a name is always
    // emitted and the scope is what reports the clash.
    if (trimmed.empty())
    {
        trimmed = helperSymbol.str();
    }
    return codegenProjectIdentifier(language, IdentifierRole::FunctionName, trimmed);
}

}  // namespace llvmdsdl
