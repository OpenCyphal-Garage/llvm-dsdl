//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Spelling a reference from a site in a surface plan.
///
/// A reference is written the way a person writing at that spot would write it: as the shortest
/// spelling the language's own name lookup, starting at the reference, resolves to the declaration
/// it means. `spellReference` repeats the language's lookup, as its row's `Lookup` states it, over
/// the plan, and answers each spelling from the tree; nothing stores one per reference.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_SUPPORT_SURFACE_LOOKUP_H
#define LLVMDSDL_SUPPORT_SURFACE_LOOKUP_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "llvmdsdl/Support/LanguageTraits.h"
#include "llvmdsdl/Support/SurfacePlan.h"

namespace llvmdsdl
{

/// @brief What the body a reference is written in is to the type it is written in.
enum class SiteRelation : std::uint8_t
{
    /// @brief Not a member of a type: a free function, or code at namespace or module scope.
    Free,

    /// @brief A method handed an instance.
    Instance,

    /// @brief A member handed neither an instance nor its class.
    Static,

    /// @brief A method handed its class.
    Class,
};

/// @brief Where a reference is written.
struct SurfaceSite final
{
    /// @brief The scope the body is written in.
    std::size_t scope{};

    /// @brief What the body is to the type it is written in.
    SiteRelation relation{};
};

/// @brief Spells a reference to @p target, written at @p site.
///
/// The candidates are tried shortest first, and the first the language's lookup resolves to
/// @p target is the answer:
///
/// 1. the bare name;
/// 2. the receiver form, where the site reaches a member of its own type through one: `Self::X`,
///    `self.X`, `cls.X`, or the type's name where the type is a value too;
/// 3. the name qualified by its enclosing scopes, outward, while the scope that declares the
///    outermost qualifier is within the horizon;
/// 4. an import's local name, and the path from what it binds;
/// 5. the path from the root: without the root's prefix where the language writes one only when
///    the root's first component is shadowed and it is not, and with the prefix otherwise.
///
/// The horizon is the scopes whose every name the plan holds: the site's own file or module, and the
/// namespace a definition shares with the run's other definitions of its DSDL namespace. A
/// qualifier found beyond it could be captured by a declaration the plan does not hold.
///
/// The site's own type is spelt as the language names it from within, where it names it -- Rust's
/// `Self`, and `cls` in a Python class method -- ahead of the bare name.
///
/// Where the row's composition qualifies from the root, a declaration outside the site's own type
/// is spelt from the root, with the root's prefix.
/// @param[in] row The language's row.
/// @param[in] plan The plan.
/// @param[in] site Where the reference is written.
/// @param[in] target The declaration or scope it names.
/// @return The spelling, or none where no candidate resolves, as where the declaration is in
///         another module and the plan holds no import of it.
[[nodiscard]] std::optional<std::string> spellReference(const LanguageTraits& row,
                                                        const SurfacePlan&    plan,
                                                        const SurfaceSite&    site,
                                                        const SurfaceItem&    target);

}  // namespace llvmdsdl

#endif  // LLVMDSDL_SUPPORT_SURFACE_LOOKUP_H
