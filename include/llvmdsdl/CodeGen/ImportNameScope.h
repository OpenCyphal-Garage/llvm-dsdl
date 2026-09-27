//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// The local names one generated file imports the definitions it names under.
///
/// An import shares one namespace with the items the file declares, so a definition whose dependency
/// has its own short name, or two dependencies of one short name, meet in it. The file's own
/// declarations are reserved before any import is claimed, so the import is what moves: a
/// declaration's name is the file's public API, and an import's is private to the file.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_CODEGEN_IMPORT_NAME_SCOPE_H
#define LLVMDSDL_CODEGEN_IMPORT_NAME_SCOPE_H

#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <llvm/ADT/StringRef.h>

#include "llvmdsdl/Semantics/Model.h"
#include "llvmdsdl/Support/Language.h"

namespace llvmdsdl
{

/// @brief The local names one generated file imports definitions under, apart from one another and
///        from what the file declares itself.
///
/// A clash takes as much of the definition's namespace, from the nearest component outwards, as
/// tells it apart, composed with the short name and projected as a type name of the language:
/// `angular_velocity::Vector3` and `velocity::Vector3` meet, and the second is `VelocityVector3`.
/// The namespace runs out before the candidates do where a file holds several versions of one type
/// of a one-component namespace, so an ordinal follows, and a name is always reached.
class ImportNameScope final
{
public:
    /// @brief The names a local type name brings into the file: the name itself, and in a language
    ///        whose import of a type brings functions named after it, those.
    using Brought = std::function<std::vector<std::string>(const std::string& local)>;

    /// @param[in] language The language whose type-name projection composes a qualified name.
    /// @param[in] brought The names a local type name brings; the name alone where it is unset.
    explicit ImportNameScope(Language language, Brought brought = {});

    /// @brief Reserves @p name, which the file declares itself.
    void reserve(llvm::StringRef name);

    /// @brief Claims a local name for the definition @p ref, which it exports as @p exported, and
    ///        answers it. A definition claims one name however often it is asked.
    /// @param[in] deprecated Whether the definition is deprecated, which a composed name carries
    ///            as the exported name does.
    std::string claim(const SemanticTypeRef& ref, const std::string& exported, bool deprecated);

    /// @brief The local name claimed for @p ref, or @p exported where none was.
    [[nodiscard]] std::string localName(const SemanticTypeRef& ref, const std::string& exported) const;

private:
    [[nodiscard]] std::vector<std::string> broughtBy(const std::string& local) const;
    [[nodiscard]] bool                     taken(const std::string& local) const;

    Language                           language_;
    Brought                            brought_;
    std::set<std::string>              declared_;
    std::set<std::string>              imported_;
    std::map<std::string, std::string> claims_;
};

}  // namespace llvmdsdl

#endif  // LLVMDSDL_CODEGEN_IMPORT_NAME_SCOPE_H
