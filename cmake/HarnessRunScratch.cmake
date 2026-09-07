#===----------------------------------------------------------------------===#
#
# Part of the OpenCyphal project, under the MIT licence
# SPDX-License-Identifier: MIT
#
#===----------------------------------------------------------------------===#
#
# The scratch tree a cross-language harness builds in, and whether it survives the run.
#
# A parity lane generates two languages, compiles both, links a harness against them and runs it.
# That is a lot of tree, and on a pass none of it is wanted -- so it goes in a directory named for
# the run and is deleted at the end. On a failure it stays.
#
# Keeping it on a *pass* is opt-in. The harness sources are checked in with `@V1_0@`-style tokens
# that resolve to a version suffix or to nothing depending on the scheme under test (see
# HarnessTypeNameTokens.cmake), so the source tree does not show what the harness was compiled
# against; the kept output does.

# @brief Prepares a run-scoped scratch directory under @p out_dir.
#
# Resolves the keep-output opt-in, clears anything left by previous runs, and creates a fresh
# directory named for this one. The opt-in is settable either way; the environment variable reaches
# the script through ctest without editing a registration:
#
#   LLVMDSDL_KEEP_RUN_OUTPUT=1 ctest --test-dir <build> -R <lane>
#
# @param out_dir  The lane's output directory.
# @param out_var  Name of the variable to receive the new run directory's path.
#
# Also sets KEEP_RUN_OUTPUT in the caller's scope, which @ref llvmdsdl_harness_scratch_finish reads.
function(llvmdsdl_harness_scratch_begin out_dir out_var)
  if(NOT DEFINED KEEP_RUN_OUTPUT)
    set(KEEP_RUN_OUTPUT "$ENV{LLVMDSDL_KEEP_RUN_OUTPUT}")
  endif()
  set(KEEP_RUN_OUTPUT "${KEEP_RUN_OUTPUT}" PARENT_SCOPE)

  file(MAKE_DIRECTORY "${out_dir}")

  # Previous run directories go. A failed run leaves its tree behind, and without this they
  # accumulate silently, each looking as current as the last.
  file(GLOB _stale_runs "${out_dir}/run-*")
  foreach(_stale_run IN LISTS _stale_runs)
    if(IS_DIRECTORY "${_stale_run}")
      file(REMOVE_RECURSE "${_stale_run}")
    endif()
  endforeach()

  # The nonce is what keeps two runs started in the same second apart.
  string(TIMESTAMP _stamp "%Y%m%d%H%M%S")
  string(RANDOM LENGTH 8 ALPHABET 0123456789abcdef _nonce)
  set(_run "${out_dir}/run-${_stamp}-${_nonce}")
  file(MAKE_DIRECTORY "${_run}")

  set(${out_var} "${_run}" PARENT_SCOPE)
endfunction()

# @brief Removes @p run_dir, or keeps it and says where it is.
# @param run_dir The directory returned by @ref llvmdsdl_harness_scratch_begin.
# @param label   The lane's name, for the message when removal fails.
function(llvmdsdl_harness_scratch_finish run_dir label)
  if(KEEP_RUN_OUTPUT)
    message(STATUS
      "KEEP_RUN_OUTPUT: ${label} scratch tree kept at ${run_dir}\n"
      "  ${run_dir}/c        generated C\n"
      "  ${run_dir}/go       generated Go\n"
      "  ${run_dir}/harness  the harness sources, after token substitution\n"
      "  ${run_dir}/build    compiled artefacts")
    return()
  endif()

  file(REMOVE_RECURSE "${run_dir}")
  if(EXISTS "${run_dir}")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E rm -rf "${run_dir}")
  endif()
  if(EXISTS "${run_dir}")
    message(WARNING "unable to remove ${label} scratch directory: ${run_dir}")
  endif()
endfunction()
