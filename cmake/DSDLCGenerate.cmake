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
# WHERE THE OUTPUT LIST COMES FROM
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
# WHY ONE NAMESPACE IS OFTEN SEVERAL CALLS
#
# Generation decomposes, and past a single namespace it has to. Two calls that both default to
# `--generate-support as-needed` into the same output directory both emit the runtime header, and
# two rules producing one file is an error Ninja refuses to build at all -- so any project with more
# than one dsdlc_generate() must split support out. The same is true of shared dependencies: two
# namespaces that both refer to uavcan.time each generate it unless one call is given the job.
#
# The seams are SUPPORT, OMIT_DEPENDENCIES, and BUILTIN. A project with more than one namespace
# usually wants:
#
#   dsdlc_generate(dsdl_support LANGUAGE c SUPPORT only  OUTDIR ${gen})
#   dsdlc_generate(dsdl_builtin LANGUAGE c SUPPORT never OUTDIR ${gen} BUILTIN +uavcan.node)
#   dsdlc_generate(alpha  LANGUAGE c SUPPORT never OMIT_DEPENDENCIES OUTDIR ${gen} NAMESPACE ${a})
#   dsdlc_generate(beta   LANGUAGE c SUPPORT never OMIT_DEPENDENCIES OUTDIR ${gen} NAMESPACE ${b})
#
# Getting that wrong is easy and the generator's own error message is obscure, so this function
# checks for overlap itself and fails at configure time naming both claimants.

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

# Two rules producing one file is a hard error in Ninja and a silent race in Make. Both are worse
# than a configure-time refusal that names the other claimant and says which seam to split on.
function(_dsdlc_claim_outputs name outputs)
  get_property(_claimed GLOBAL PROPERTY DSDLC_CLAIMED_OUTPUTS)
  get_property(_owners GLOBAL PROPERTY DSDLC_CLAIMED_OWNERS)

  foreach(_file IN LISTS outputs)
    list(FIND _claimed "${_file}" _index)
    if(NOT _index EQUAL -1)
      list(GET _owners ${_index} _owner)
      message(FATAL_ERROR
        "dsdlc_generate(${name}) would produce ${_file}, which dsdlc_generate(${_owner}) already "
        "produces.\n"
        "Two rules producing one file is an error Ninja refuses to build. Give each call a "
        "disjoint share of the output:\n"
        "  - SUPPORT only   for one call, SUPPORT never for the rest, so the runtime header has "
        "one owner;\n"
        "  - OMIT_DEPENDENCIES on namespace calls, plus one BUILTIN call owning the shared "
        "standard types;\n"
        "  - or a separate OUTDIR per call, if the trees really are independent.")
    endif()
    list(APPEND _claimed "${_file}")
    list(APPEND _owners "${name}")
  endforeach()

  set_property(GLOBAL PROPERTY DSDLC_CLAIMED_OUTPUTS "${_claimed}")
  set_property(GLOBAL PROPERTY DSDLC_CLAIMED_OWNERS "${_owners}")
endfunction()

#[==[
dsdlc_generate(<name>
  LANGUAGE   <c|cpp|rust|go|ts|python|obj>
  [NAMESPACE  <dir>...]     # root namespace directories (dsdlc's positional arguments)
  [BUILTIN    <selector>...] # embedded-catalog selectors, e.g. +uavcan.node or +uavcan.node.Heartbeat.1.0
  [LOOKUP_DIR <dir>...]     # additional lookup roots, passed as -I
  [OUTDIR     <dir>]        # default: ${CMAKE_CURRENT_BINARY_DIR}/<name>
  [SUPPORT    <as-needed|always|never|only>]
  [OMIT_DEPENDENCIES]       # emit only the definitions named, not the ones they refer to
  [OPTIONS    <arg>...]     # backend flags, passed through verbatim
)

For LANGUAGE c and cpp, creates a static library target <name> compiled from the generated sources,
with the output directory on its INTERFACE include path -- link it and you are done. A call that
emits no compilable sources (SUPPORT only, for instance) creates an INTERFACE library instead, which
still carries the include path.

For LANGUAGE obj, creates an INTERFACE target linking the prebuilt objects and carrying the include
path of the headers published beside it -- link it and you are done, the same way.

For the other languages CMake is only orchestrating a build it does not own, so this creates a custom
target <name> plus an INTERFACE library <name>-tree carrying the output directory in its
DSDLC_OUTPUT_DIR property, for a downstream step (cargo, go, npm, pip) to consume.
]==]
function(dsdlc_generate name)
  set(_options OMIT_DEPENDENCIES)
  set(_one_value LANGUAGE OUTDIR SUPPORT)
  set(_multi_value NAMESPACE BUILTIN LOOKUP_DIR OPTIONS)
  cmake_parse_arguments(ARG "${_options}" "${_one_value}" "${_multi_value}" ${ARGN})

  if(NOT ARG_LANGUAGE)
    message(FATAL_ERROR "dsdlc_generate(${name}): LANGUAGE is required")
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

  # The obj backend assembles what it lowers and emits no support code of its own, so dsdlc rejects
  # --generate-support for it outright. Catch that here, where the message can say which argument to
  # drop, rather than passing it through and surfacing the generator's complaint about a flag the
  # caller never wrote.
  if(ARG_LANGUAGE STREQUAL "obj" AND ARG_SUPPORT)
    message(FATAL_ERROR
      "dsdlc_generate(${name}): SUPPORT does not apply to the obj backend, which compiles what it "
      "generates and emits no support code of its own.")
  endif()
  if(NOT ARG_SUPPORT)
    set(ARG_SUPPORT "as-needed")
  endif()
  set(_support_modes as-needed always never only)
  if(NOT ARG_SUPPORT IN_LIST _support_modes)
    message(FATAL_ERROR
      "dsdlc_generate(${name}): SUPPORT must be one of ${_support_modes}, got '${ARG_SUPPORT}'")
  endif()

  # `only` and `always` are self-sufficient: support code is not derived from any definition, so
  # asking for it needs no target. Every other mode does.
  if(NOT ARG_NAMESPACE AND NOT ARG_BUILTIN AND NOT ARG_SUPPORT STREQUAL "only"
     AND NOT ARG_SUPPORT STREQUAL "always")
    message(FATAL_ERROR
      "dsdlc_generate(${name}): needs NAMESPACE or BUILTIN, unless SUPPORT is 'only' or 'always'")
  endif()

  foreach(_selector IN LISTS ARG_BUILTIN)
    if(NOT _selector MATCHES "^\\+")
      message(FATAL_ERROR
        "dsdlc_generate(${name}): BUILTIN selectors name the compiled-in catalog and begin with "
        "'+', for example +uavcan.node; got '${_selector}'")
    endif()
  endforeach()

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
  if(NOT ARG_LANGUAGE STREQUAL "obj")
    list(APPEND _argv --generate-support "${ARG_SUPPORT}")
  endif()
  if(ARG_OMIT_DEPENDENCIES)
    list(APPEND _argv --omit-dependencies)
  endif()
  foreach(_lookup IN LISTS ARG_LOOKUP_DIR)
    list(APPEND _argv -I "${_lookup}")
  endforeach()
  list(APPEND _argv ${ARG_NAMESPACE} ${ARG_BUILTIN} ${ARG_OPTIONS} --outdir "${_outdir}")

  _dsdlc_query(_inputs MODE --list-inputs ARGV ${_argv})
  _dsdlc_query(_outputs MODE --list-outputs ARGV ${_argv})

  if(NOT _outputs)
    message(FATAL_ERROR
      "dsdlc_generate(${name}): dsdlc reported no outputs. An empty selection is almost never what "
      "was meant -- check NAMESPACE, BUILTIN, and SUPPORT.")
  endif()

  _dsdlc_claim_outputs("${name}" "${_outputs}")

  # Modification of a definition already in the build: re-run configure so the OUTPUT list is
  # recomputed even if only a bit-length changed.
  if(_inputs)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_inputs})
  endif()

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

  # Deleting a definition leaves its generated header behind -- still on the include path, still
  # compiling for anyone who includes it -- unless something removes it. dsdlc does, given a
  # manifest of what this call produced last time. The manifest is per call, because a call owns
  # only the files it emits and a tranche that swept ${_outdir} would delete its siblings' work.
  set(_manifest "${CMAKE_CURRENT_BINARY_DIR}/${name}.dsdlc-manifest")

  # Pruning happens when dsdlc runs, so deleting a definition has to make it run. Removing an input
  # does not by itself: every surviving output is still newer than every surviving input. This file
  # closes that gap -- its content is the input list, configure_file rewrites it only when that list
  # actually changes, and the command depends on it.
  set(_signature "${CMAKE_CURRENT_BINARY_DIR}/${name}.dsdlc-inputs")
  string(REPLACE ";" "\n" _signature_body "${_inputs}")
  file(WRITE "${_signature}.in" "${_signature_body}\n")
  configure_file("${_signature}.in" "${_signature}" COPYONLY)

  add_custom_command(
    OUTPUT ${_outputs}
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${_outdir}"
    COMMAND "${DSDLC_EXECUTABLE}" ${_argv} --prune-manifest "${_manifest}"
    DEPENDS ${_inputs} "${_signature}" "${DSDLC_EXECUTABLE}"
    COMMENT "dsdlc: generating ${ARG_LANGUAGE} for ${name}"
    VERBATIM)

  if(ARG_LANGUAGE STREQUAL "obj")
    # The obj backend hands back objects rather than sources: dsdlc assembled them itself, for a
    # target triple that need not be this one. The headers are published beside them in the layout
    # the `c` backend uses, so one call gives a complete interface -- link the target and the
    # include path comes with it.
    set(_objects "")
    foreach(_file IN LISTS _outputs)
      if(_file MATCHES "\\.o$")
        list(APPEND _objects "${_file}")
      endif()
    endforeach()
    if(NOT _objects)
      message(FATAL_ERROR "dsdlc_generate(${name}): the obj backend produced no objects.")
    endif()

    set_source_files_properties(${_outputs} PROPERTIES GENERATED TRUE)
    add_custom_target(${name}-generate DEPENDS ${_outputs})

    # An INTERFACE library carries both the objects and the dependency on the rule that writes
    # them, so linking ${name} pulls them in and waits for them to exist.
    add_library(${name} INTERFACE)
    target_link_libraries(${name} INTERFACE ${_objects})
    target_include_directories(${name} INTERFACE "${_outdir}")
    add_dependencies(${name} ${name}-generate)
    set_property(TARGET ${name} PROPERTY DSDLC_OUTPUT_DIR "${_outdir}")
    set_property(TARGET ${name} PROPERTY DSDLC_OBJECTS "${_objects}")
  elseif(ARG_LANGUAGE STREQUAL "c" OR ARG_LANGUAGE STREQUAL "cpp")
    set(_sources "")
    foreach(_file IN LISTS _outputs)
      if(_file MATCHES "\\.(c|cpp|cc|cxx)$")
        list(APPEND _sources "${_file}")
      endif()
    endforeach()

    set_source_files_properties(${_outputs} PROPERTIES GENERATED TRUE)

    if(_sources)
      add_library(${name} STATIC ${_sources})
      target_include_directories(${name} PUBLIC "${_outdir}")
    else()
      # A support-only or header-only call still has to be buildable and linkable -- it carries the
      # include path the other calls' output needs, and something must drive the command.
      add_custom_target(${name}-generate DEPENDS ${_outputs})
      add_library(${name} INTERFACE)
      target_include_directories(${name} INTERFACE "${_outdir}")
      add_dependencies(${name} ${name}-generate)
    endif()
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
