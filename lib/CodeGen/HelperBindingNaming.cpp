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
#include "llvmdsdl/CodeGen/BodyTranslator.h"
#include "llvmdsdl/IR/DSDLOps.h"
#include "llvmdsdl/Support/DefinitionNaming.h"
#include "llvmdsdl/Support/Language.h"
#include "llvmdsdl/Support/LanguageTraits.h"
#include "llvmdsdl/Support/NamingPolicy.h"
#include <llvm/ADT/StringRef.h>
#include <llvm/ADT/StringMap.h>
#include <mlir/Dialect/Func/IR/FuncOps.h>
#include <mlir/IR/BuiltinOps.h>
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

std::string renderHelperBindingIdentifier(const Language language, const llvm::StringRef helperSymbol)
{
    // The MLIR symbol separates its fields with `__`. C++ reserves any identifier containing a double
    // underscore, so for that language the runs are collapsed -- after the prefix is joined, since
    // the symbol itself begins with one and the seam would otherwise make a fresh pair. The symbol
    // carries the schema, so the collapsed form stays unique across the whole of a file's scope;
    // two helpers of one definition differ by kind, section and operand index, none of which the
    // collapse touches. No other language reserves an interior double underscore, and none of them
    // carry a leading one here, so they keep the symbol as it is.
    std::string joined = "mlir_" + codegenSanitizeIdentifier(language, helperSymbol);
    if (languageTraits(language).classification.reservedUnderscores != ReservedUnderscores::LeadingAndInterior)
    {
        return joined;
    }

    return collapseUnderscoreRuns(joined);
}

std::string renderScopeLocalHelperName(const Language        language,
                                       const llvm::StringRef helperSymbol,
                                       const llvm::StringRef schemaSymbol,
                                       const llvm::StringRef qualifier)
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
    if (!qualifier.empty())
    {
        trimmed = qualifier.str() + "_" + trimmed;
    }

    return codegenProjectIdentifier(language, IdentifierRole::InternalFunctionName, trimmed);
}

llvm::StringMap<std::string> renderSchemaHelperNames(const Language        language,
                                                     const mlir::ModuleOp  module,
                                                     mlir::dsdl::SchemaOp  schema,
                                                     NamingScope&          scope,
                                                     const llvm::StringRef qualifier)
{
    llvm::StringMap<std::string> names;
    for (mlir::func::FuncOp fn : schemaFunctions(module, schema.getSymName()))
    {
        if (planBodyDirection(fn) || fn->hasAttr("llvmdsdl.unreferenced"))
        {
            continue;
        }
        const llvm::StringRef symbol = fn.getSymName();

        // The marker goes on after the scope has allocated the name, because the projection a scope
        // applies folds a leading underscore away. Every helper takes the same marker, so two that
        // the scope kept apart stay apart.
        const std::string declared =
            scope.declare(IdentifierRole::InternalFunctionName,
                          renderScopeLocalHelperName(language, symbol, schema.getSymName(), qualifier));
        names[symbol] = (languageTraits(language).classification.internalLinkage == InternalLinkage::UnderscorePrefix)
                            ? "_" + declared
                            : declared;
    }
    return names;
}

}  // namespace llvmdsdl
