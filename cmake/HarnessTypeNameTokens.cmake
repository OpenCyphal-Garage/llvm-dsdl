#===----------------------------------------------------------------------===#
#
# Part of the OpenCyphal project, under the MIT licence
# SPDX-License-Identifier: MIT
#
#===----------------------------------------------------------------------===#
#
# Type-name version tokens for the integration harnesses.
#
# A harness that names a generated type has to spell it the way the backend emitted it, and whether
# that spelling carries the type's version is a generator choice. Rather than hard-code one answer,
# the harness templates write `Heartbeat@V1_0@` and this file decides what the token expands to --
# which is what lets one harness run under either scheme.
#
# There are two families because the two halves of a cross-language harness do not agree today. C
# names carry no version (`uavcan__diagnostic__Record`); Rust, Go, TypeScript and Python names do
# (`Record_1_1`). `@CV*@` decorates the first, `@V*@` the second, and they are set independently so
# that a later decision to change one scheme need not touch the harnesses again.
#
# Only the identifiers move. File and module names come from `IdentifierRole::FileStem`, which no
# versioning scheme touches, so `#include "uavcan/node/Heartbeat_1_0.h"` is written literally and
# stays correct either way.
#
# Usage, from a harness script that already receives SOURCE_ROOT:
#
#   include("${SOURCE_ROOT}/cmake/HarnessTypeNameTokens.cmake")
#   llvmdsdl_harness_type_name_tokens("${C_TYPE_NAMES}" "${TYPE_NAMES}")
#   configure_file("${template}" "${out}" @ONLY)

# The version pairs the harnesses actually name. A new one here is a one-line addition; leaving it
# out shows up as an unsubstituted `@V9_9@` in the generated source, which fails to compile rather
# than passing quietly.
set(LLVMDSDL_HARNESS_VERSION_PAIRS 0_1 0_2 1_0 1_1 1_2 1_3 2_0)

# @brief Defines the @CV*@ and @V*@ token families in the caller's scope.
# @param c_scheme    "versioned" or "unversioned" -- governs C type names.
# @param other_scheme "versioned" or "unversioned" -- governs Rust/Go/TypeScript/Python type names.
function(llvmdsdl_harness_type_name_tokens c_scheme other_scheme)
  foreach(_pair IN LISTS LLVMDSDL_HARNESS_VERSION_PAIRS)
    if(c_scheme STREQUAL "versioned")
      set("CV${_pair}" "_${_pair}" PARENT_SCOPE)
    else()
      set("CV${_pair}" "" PARENT_SCOPE)
    endif()

    if(other_scheme STREQUAL "versioned")
      set("V${_pair}" "_${_pair}" PARENT_SCOPE)
    else()
      set("V${_pair}" "" PARENT_SCOPE)
    endif()
  endforeach()
endfunction()
