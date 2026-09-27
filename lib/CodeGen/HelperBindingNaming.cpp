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
#include "llvmdsdl/Support/Language.h"
#include "llvmdsdl/Support/LanguageTraits.h"
#include "llvmdsdl/Support/NamingPolicy.h"
#include "llvmdsdl/Support/PlanSymbol.h"
#include <algorithm>
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

/// @brief The helper's kind, then the section, step and direction that tell it from its siblings.
std::string helperParts(const PlanSymbol& helper, const llvm::StringRef separator)
{
    std::string out = helper.helperKind;
    if (!helper.section.empty())
    {
        out += separator.str() + helper.section;
    }
    if (helper.step)
    {
        out += separator.str() + std::to_string(*helper.step);
    }
    if (helper.direction != PlanHelperDirection::None)
    {
        out += separator.str() + ((helper.direction == PlanHelperDirection::Serialize) ? "ser" : "deser");
    }
    return out;
}

}  // namespace

std::string renderHelperBindingIdentifier(const Language language, const llvm::StringRef name)
{
    // C++ reserves any identifier containing a double underscore, so for that language the runs are
    // collapsed -- after the prefix is joined, since a name beginning with an underscore would make
    // a fresh pair at the seam. No other language reserves an interior double underscore.
    std::string joined = "mlir_" + codegenSanitizeIdentifier(language, name);
    if (languageTraits(language).classification.reservedUnderscores != ReservedUnderscores::LeadingAndInterior)
    {
        return joined;
    }

    return collapseUnderscoreRuns(joined);
}

std::string renderHelperBindingIdentifier(const Language language, const PlanSymbol& helper)
{
    std::string fullName = helper.schema.fullName;
    std::ranges::replace(fullName, '.', '_');
    const std::string version = std::to_string(helper.schema.major) + "_" + std::to_string(helper.schema.minor);
    std::string       parts   = helperParts(helper, "__");
    const std::size_t kindEnd = helper.helperKind.size();
    // The definition sits between the kind and what tells the helper from its siblings.
    parts.insert(kindEnd, "__" + fullName + "_" + version);
    return renderHelperBindingIdentifier(language, "llvmdsdl_plan_" + parts);
}

std::string renderScopeLocalHelperName(const Language        language,
                                       const PlanSymbol&     helper,
                                       const llvm::StringRef qualifier)
{
    std::string name = collapseUnderscoreRuns(helperParts(helper, "_"));
    while (!name.empty() && name.front() == '_')
    {
        name.erase(name.begin());
    }
    while (!name.empty() && name.back() == '_')
    {
        name.pop_back();
    }
    if (!qualifier.empty())
    {
        name = qualifier.str() + "_" + name;
    }

    return codegenProjectIdentifier(language, IdentifierRole::InternalFunctionName, name);
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
        const auto            helper = parsePlanSymbol(symbol);
        if (!helper || (helper->function != PlanFunction::Helper))
        {
            continue;
        }

        // The marker goes on after the scope has allocated the name, because the projection a scope
        // applies folds a leading underscore away. Every helper takes the same marker, so two that
        // the scope kept apart stay apart.
        const std::string declared = scope.declare(IdentifierRole::InternalFunctionName,
                                                   renderScopeLocalHelperName(language, *helper, qualifier));
        names[symbol] = (languageTraits(language).classification.internalLinkage == InternalLinkage::UnderscorePrefix)
                            ? "_" + declared
                            : declared;
    }
    return names;
}

}  // namespace llvmdsdl
