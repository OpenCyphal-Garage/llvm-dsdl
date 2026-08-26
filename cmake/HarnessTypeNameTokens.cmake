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
# There are two families because a cross-language harness names types in both of its halves and the
# two halves need not agree. `@CV*@` decorates C names, `@V*@` the other five. They default the same
# way today; they are separate so that a lane can cover one scheme against the other, and so that
# changing one scheme's default does not silently move the other.
#
# Only the identifiers move. File and module names come from `IdentifierRole::FileStem`, which no
# versioning scheme touches, so `#include "uavcan/node/Heartbeat_1_0.h"` is written literally and
# stays correct either way.
#
# Usage, from a harness script that already receives SOURCE_ROOT:
#
#   include("${SOURCE_ROOT}/cmake/HarnessTypeNameTokens.cmake")
#   llvmdsdl_harness_naming_scheme(C_DEFAULT "unversioned" OTHER_DEFAULT "unversioned")
#   execute_process(COMMAND "${DSDLC}" --target-language c ${c_scheme_args} ...)
#   configure_file("${template}" "${out}" @ONLY)

# The version pairs the harnesses actually name. A new one here is a one-line addition; leaving it
# out shows up as an unsubstituted `@V9_9@` in the generated source, which fails to compile rather
# than passing quietly.
set(LLVMDSDL_HARNESS_VERSION_PAIRS 0_1 0_2 1_0 1_1 1_2 1_3 2_0)

# @brief Settles the naming scheme for one harness script: defaults, tokens, and generator flags.
#
# The three have to agree or the harness is testing nothing -- a script that expands its tokens
# versioned while invoking dsdlc unversioned fails to compile, and the reverse fails only sometimes.
# They are derived from one variable here so that they cannot be set separately, which is a mistake
# that was made more than once before this existed.
#
# Each scheme variable is settable from the caller's `add_test` with a `-D`, which is how one harness
# is registered twice to cover both schemes.
#
# @param C_DEFAULT     Scheme for C names when C_TYPE_NAME_SCHEME is unset. Default "unversioned".
# @param OTHER_DEFAULT Scheme for the other five when TYPE_NAME_SCHEME is unset. Default "unversioned".
#
# Sets in the caller's scope: the CV*/V* token families, plus `c_scheme_args` and `other_scheme_args`
# to splice into the matching dsdlc invocation.
function(llvmdsdl_harness_naming_scheme)
  cmake_parse_arguments(ARG "" "C_DEFAULT;OTHER_DEFAULT" "" ${ARGN})
  if(NOT ARG_C_DEFAULT)
    set(ARG_C_DEFAULT "unversioned")
  endif()
  if(NOT ARG_OTHER_DEFAULT)
    set(ARG_OTHER_DEFAULT "unversioned")
  endif()

  if(NOT DEFINED C_TYPE_NAME_SCHEME OR "${C_TYPE_NAME_SCHEME}" STREQUAL "")
    set(C_TYPE_NAME_SCHEME "${ARG_C_DEFAULT}")
  endif()
  if(NOT DEFINED TYPE_NAME_SCHEME OR "${TYPE_NAME_SCHEME}" STREQUAL "")
    set(TYPE_NAME_SCHEME "${ARG_OTHER_DEFAULT}")
  endif()

  # A typo here would otherwise read as "unversioned" and pass, since only the exact string
  # "versioned" turns anything on.
  foreach(_pair "C_TYPE_NAME_SCHEME;${C_TYPE_NAME_SCHEME}" "TYPE_NAME_SCHEME;${TYPE_NAME_SCHEME}")
    list(GET _pair 0 _name)
    list(GET _pair 1 _value)
    if(NOT _value STREQUAL "versioned" AND NOT _value STREQUAL "unversioned")
      message(FATAL_ERROR "${_name} must be \"versioned\" or \"unversioned\", got \"${_value}\"")
    endif()
  endforeach()

  foreach(_pair IN LISTS LLVMDSDL_HARNESS_VERSION_PAIRS)
    if(C_TYPE_NAME_SCHEME STREQUAL "versioned")
      set("CV${_pair}" "_${_pair}" PARENT_SCOPE)
    else()
      set("CV${_pair}" "" PARENT_SCOPE)
    endif()

    if(TYPE_NAME_SCHEME STREQUAL "versioned")
      set("V${_pair}" "_${_pair}" PARENT_SCOPE)
    else()
      set("V${_pair}" "" PARENT_SCOPE)
    endif()
  endforeach()

  set(_c_args "")
  if(C_TYPE_NAME_SCHEME STREQUAL "versioned")
    set(_c_args --versioned-type-names)
  endif()
  set(_other_args "")
  if(TYPE_NAME_SCHEME STREQUAL "versioned")
    set(_other_args --versioned-type-names)
  endif()

  set(C_TYPE_NAME_SCHEME "${C_TYPE_NAME_SCHEME}" PARENT_SCOPE)
  set(TYPE_NAME_SCHEME "${TYPE_NAME_SCHEME}" PARENT_SCOPE)
  set(c_scheme_args "${_c_args}" PARENT_SCOPE)
  set(other_scheme_args "${_other_args}" PARENT_SCOPE)
endfunction()
