//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Reserved identifier patterns from Cyphal Specification v1.0 section 3.2.5.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_SUPPORT_RESERVEDIDENTIFIERS_H
#define LLVMDSDL_SUPPORT_RESERVEDIDENTIFIERS_H

#include <string>

namespace llvmdsdl
{

/// @brief Tests whether @p identifier matches a reserved identifier pattern.
/// @details Per Cyphal Specification v1.0 section 3.2.5 / table 3.5, the listed
/// patterns are case-insensitive and may not be used to name new entities
/// (data type name components, and field/constant attribute names — see
/// section 3.5.1). This predicate recognizes those patterns; it does not by
/// itself decide where the restriction applies.
/// @param[in] identifier Candidate name component.
/// @return True when the name matches any reserved pattern.
[[nodiscard]] bool isReservedIdentifier(const std::string& identifier);

}  // namespace llvmdsdl

#endif  // LLVMDSDL_SUPPORT_RESERVEDIDENTIFIERS_H
