#===----------------------------------------------------------------------===#
#
# Part of the OpenCyphal project, under the MIT licence
# SPDX-License-Identifier: MIT
#
#===----------------------------------------------------------------------===#
#
# dsdlc_generate() -- run dsdlc as part of a CMake build, with exact dependencies.
#
# Installed alongside llvm-dsdlConfig.cmake and included by it. This is supported API: a downstream
# project calls find_package(llvm-dsdl) and then this function, and both are versioned with the tool.
#
# WHY THIS IS NOT JUST add_custom_command
#
# The hard part of generating code in CMake is telling the build what will appear before it appears.
# The usual answers are all bad: globbing after the fact misses the first build, listing outputs by
# hand rots the moment somebody adds a type, and a stamp file makes every consumer depend on all of
# the output whether or not it uses any of it.
#
# dsdlc answers the question directly. `--list-outputs` prints exactly what a run would produce and
# `--list-inputs` prints exactly what it would read, both implying --dry-run and both writing the
# list to stdout and nothing else. So this function asks at configure time and hands CMake a precise
# OUTPUT list -- no stamps, no hand-maintained lists.
#
# THE TWO KINDS OF STALENESS, WHICH NEED TWO DIFFERENT MECHANISMS
#
# Editing a definition must re-run dsdlc. That is what the custom command's DEPENDS is for, and
# `--list-inputs` makes it exact, including transitive references: touching Mode.1.0.dsdl rebuilds
# because SensorFrame.1.0.dsdl embeds a type that embeds it.
#
# *Adding* a definition must re-run configure, because the OUTPUT list computed above is now short by
# two files and nothing in the build knows it. This is the case CMAKE_CONFIGURE_DEPENDS alone does
# not cover -- it watches the files it is given for modification, and a brand-new file has modified
# none of them. The mechanism that does cover it is a CONFIGURE_DEPENDS glob, which CMake re-evaluates
# at build time and compares against the previous result. So both are used, for different reasons: the
# glob notices the set changing, `--list-inputs` describes what depends on what.
#
# The glob costs a directory walk per build. That is the price of adding a type without having to
# remember to reconfigure, and it is the right trade for a schema directory.

# `--list-inputs --list-outputs` in one call emits inputs, then an empty element, then outputs. That
# is fine for a shell but awkward in CMake, where the list separator IS the semicolon and an empty
# element does not survive being split. Two calls cost about twenty milliseconds each at configure
# time and need no parsing at all, so that is what this does.
function(_dsdlc_query out_var)
  cmake_parse_arguments(Q "" "MODE" "ARGV" ${ARGN})

  execute_process(
    COMMAND "${DSDLC_EXECUTABLE}" ${Q_ARGV} "${Q_MODE}"
    OUTPUT_VARIABLE _stdout
    ERROR_VARIABLE _stderr
    RESULT_VARIABLE _status
    OUTPUT_STRIP_TRAILING_WHITESPACE)

  if(NOT _status EQUAL 0)
    message(FATAL_ERROR
      "dsdlc ${Q_MODE} failed (${_status}).\n"
      "Command: ${DSDLC_EXECUTABLE} ${Q_ARGV} ${Q_MODE}\n"
      "${_stderr}")
  endif()

  set(${out_var} "${_stdout}" PARENT_SCOPE)
endfunction()

#[==[
dsdlc_generate(<name>
  LANGUAGE   <c|cpp|rust|go|ts|python|obj>
  [NAMESPACE  <dir>...]   # root namespace directories (dsdlc's positional arguments)
  [LOOKUP_DIR <dir>...]   # additional lookup roots, passed as -I
  [OUTDIR     <dir>]      # default: ${CMAKE_CURRENT_BINARY_DIR}/<name>
  [OPTIONS    <arg>...]   # backend flags, passed through verbatim
)

For LANGUAGE c and cpp, creates a static library target <name> compiled from the generated sources,
with the output directory on its INTERFACE include path -- link it and you are done.

For the other languages CMake is only orchestrating a build it does not own, so this creates a custom
target <name> plus an INTERFACE library <name>-tree carrying the output directory in its
DSDLC_OUTPUT_DIR property, for a downstream step (cargo, go, npm, pip) to consume.
]==]
function(dsdlc_generate name)
  set(_options "")
  set(_one_value LANGUAGE OUTDIR)
  set(_multi_value NAMESPACE LOOKUP_DIR OPTIONS)
  cmake_parse_arguments(ARG "${_options}" "${_one_value}" "${_multi_value}" ${ARGN})

  if(NOT ARG_LANGUAGE)
    message(FATAL_ERROR "dsdlc_generate(${name}): LANGUAGE is required")
  endif()
  if(NOT ARG_NAMESPACE)
    message(FATAL_ERROR "dsdlc_generate(${name}): at least one NAMESPACE is required")
  endif()
  if(ARG_UNPARSED_ARGUMENTS)
    message(FATAL_ERROR
      "dsdlc_generate(${name}): unrecognised argument(s): ${ARG_UNPARSED_ARGUMENTS}. "
      "Backend flags go after OPTIONS.")
  endif()
  if(NOT DSDLC_EXECUTABLE)
    message(FATAL_ERROR
      "dsdlc_generate(${name}): DSDLC_EXECUTABLE is not set. "
      "Call find_package(llvm-dsdl) first.")
  endif()

  # Deliberately not $<CONFIG>-dependent. Generated code is a function of the schema and the backend
  # flags, and of nothing else -- no optimisation level, no NDEBUG, no configuration. Under a
  # multi-config generator a per-config output directory would run identical codegen once per
  # configuration and produce byte-identical trees, which is pure cost. Generate once; compile the
  # result however many times the generator wants to.
  if(ARG_OUTDIR)
    set(_outdir "${ARG_OUTDIR}")
  else()
    set(_outdir "${CMAKE_CURRENT_BINARY_DIR}/${name}")
  endif()
  get_filename_component(_outdir "${_outdir}" ABSOLUTE)

  if(_outdir MATCHES "\\$<")
    message(FATAL_ERROR
      "dsdlc_generate(${name}): OUTDIR must not contain a generator expression. "
      "Generated output does not vary by configuration, and this function queries the file list at "
      "configure time, where a generator expression has no value yet.")
  endif()

  set(_argv --target-language "${ARG_LANGUAGE}")
  foreach(_lookup IN LISTS ARG_LOOKUP_DIR)
    list(APPEND _argv -I "${_lookup}")
  endforeach()
  list(APPEND _argv ${ARG_NAMESPACE} ${ARG_OPTIONS} --outdir "${_outdir}")

  _dsdlc_query(_inputs MODE --list-inputs ARGV ${_argv})
  _dsdlc_query(_outputs MODE --list-outputs ARGV ${_argv})

  if(NOT _outputs)
    message(FATAL_ERROR
      "dsdlc_generate(${name}): dsdlc reported no outputs for ${ARG_NAMESPACE}. "
      "An empty namespace is almost never what was meant.")
  endif()

  # Modification of a definition already in the build: re-run configure so the OUTPUT list is
  # recomputed even if only a bit-length changed.
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_inputs})

  # Addition or removal of a definition: the list above cannot see it, because a new file has not
  # modified any file already in the list. CMake re-evaluates a CONFIGURE_DEPENDS glob at build time
  # and reconfigures when the result differs, which is the only mechanism that catches this.
  foreach(_ns IN LISTS ARG_NAMESPACE)
    if(IS_DIRECTORY "${_ns}")
      file(GLOB_RECURSE _ns_definitions CONFIGURE_DEPENDS "${_ns}/*.dsdl")
      # Result deliberately unused: registering the glob is the whole point, and the authoritative
      # dependency list is _inputs, which dsdlc resolved rather than guessed.
      unset(_ns_definitions)
    endif()
  endforeach()

  # Delete generated files that the current schema no longer accounts for.
  #
  # dsdlc writes what it is asked for and does not prune what it wrote last time, so deleting a
  # definition leaves its .h behind -- still on the include path, still compiling for anyone who
  # includes it. A type that has been deleted must stop existing, and this is where that happens:
  # configure has just been re-run (the glob above saw to that) and _outputs is the authoritative
  # list of what should be there.
  #
  # This does not reach into the build directory to delete stale object files. A target whose source
  # list shrank keeps an archive that is newer than all its remaining inputs, so the archive is not
  # re-created until something else forces a relink and it keeps the removed member until then.
  # That is ordinary CMake behaviour for any shrinking target, it resolves itself on the next change
  # to any source, and reaching into the generator's private directories to pre-empt it would be
  # worse than the wart.
  if(IS_DIRECTORY "${_outdir}")
    file(GLOB_RECURSE _existing LIST_DIRECTORIES false "${_outdir}/*")
    foreach(_file IN LISTS _existing)
      if(NOT _file IN_LIST _outputs)
        # A -MD depfile belongs to the output it sits beside; keep it if that output survives.
        string(REGEX REPLACE "\\.d$" "" _depfile_owner "${_file}")
        if(_file MATCHES "\\.d$" AND _depfile_owner IN_LIST _outputs)
          continue()
        endif()
        message(STATUS "dsdlc_generate(${name}): pruning stale ${_file}")
        file(REMOVE "${_file}")
      endif()
    endforeach()
  endif()

  add_custom_command(
    OUTPUT ${_outputs}
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${_outdir}"
    COMMAND "${DSDLC_EXECUTABLE}" ${_argv}
    DEPENDS ${_inputs} "${DSDLC_EXECUTABLE}"
    COMMENT "dsdlc: generating ${ARG_LANGUAGE} for ${name}"
    VERBATIM)

  if(ARG_LANGUAGE STREQUAL "c" OR ARG_LANGUAGE STREQUAL "cpp")
    set(_sources "")
    foreach(_file IN LISTS _outputs)
      if(_file MATCHES "\\.(c|cpp|cc|cxx)$")
        list(APPEND _sources "${_file}")
      endif()
    endforeach()

    if(NOT _sources)
      message(FATAL_ERROR
        "dsdlc_generate(${name}): the ${ARG_LANGUAGE} backend produced no compilable sources.")
    endif()

    add_library(${name} STATIC ${_sources})
    # The headers are outputs of the same custom command, so naming them here is what makes the
    # library wait for generation rather than racing it.
    set_source_files_properties(${_outputs} PROPERTIES GENERATED TRUE)
    target_include_directories(${name} PUBLIC "${_outdir}")
    set_property(TARGET ${name} PROPERTY DSDLC_OUTPUT_DIR "${_outdir}")
  else()
    add_custom_target(${name} ALL DEPENDS ${_outputs})
    add_library(${name}-tree INTERFACE)
    set_property(TARGET ${name}-tree PROPERTY DSDLC_OUTPUT_DIR "${_outdir}")
    add_dependencies(${name}-tree ${name})
  endif()

  set(${name}_OUTPUT_DIR "${_outdir}" PARENT_SCOPE)
  set(${name}_OUTPUTS "${_outputs}" PARENT_SCOPE)
  set(${name}_INPUTS "${_inputs}" PARENT_SCOPE)
endfunction()
