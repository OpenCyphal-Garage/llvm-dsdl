#===----------------------------------------------------------------------===#
#
# Part of the OpenCyphal project, under the MIT licence
# SPDX-License-Identifier: MIT
#
#===----------------------------------------------------------------------===#
#
# The showroom: a namespace of plausible vendor-specific drone datatypes, generated into every
# supported language and profile so that a prospective user can read the output before committing to
# the tool.
#
# It is generation-only on purpose. Nothing here is compiled, linked, or executed -- correctness of
# the generated code is the job of test/lit and the integration suites. Adding a compile step would
# make the showroom slow, would drag in six language toolchains, and would turn a browsing aid into
# another thing that can fail for reasons unrelated to what it is showing.
#
# Unlike the other two examples, this one has no submodule dependency: the lanyard namespace refers
# only to uavcan types, which dsdlc carries in its embedded catalog.

set(LLVMDSDL_SHOWROOM_DSDL_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/dsdl/lanyard")
set(LLVMDSDL_SHOWROOM_OUT_ROOT "${LLVMDSDL_BINARY_DIR}/showroom")
set(LLVMDSDL_SHOWROOM_STAMP_DIR "${CMAKE_CURRENT_BINARY_DIR}/stamps")

# CONFIGURE_DEPENDS so that adding a .dsdl file re-runs the glob instead of silently being ignored.
file(GLOB_RECURSE LLVMDSDL_SHOWROOM_DSDL_SOURCES CONFIGURE_DEPENDS
     "${LLVMDSDL_SHOWROOM_DSDL_ROOT}/*.dsdl")

if(NOT LLVMDSDL_SHOWROOM_DSDL_SOURCES)
  message(WARNING "Skipping showroom targets: no DSDL sources under ${LLVMDSDL_SHOWROOM_DSDL_ROOT}")
  return()
endif()

set(LLVMDSDL_SHOWROOM_VARIANT_TARGETS "")
set(LLVMDSDL_SHOWROOM_VARIANT_STAMPS "")

# Each variant is one dsdlc invocation into its own output directory.
#
# The custom command's OUTPUT is a stamp file rather than the ~70 generated files it actually
# produces. Enumerating them in CMake would mean either hand-maintaining a list that grows with every
# new type (what the other examples do, and what does not scale past a handful) or running dsdlc at
# configure time to ask -- which is impossible, because the tool is a target of this very build and
# does not exist yet when configure runs. Since nothing downstream consumes these files as build
# inputs, a stamp expresses the dependency correctly and cheaply.
function(llvmdsdl_add_showroom_variant variant_name)
  cmake_parse_arguments(ARG "" "LANGUAGE" "OPTIONS" ${ARGN})
  if(NOT ARG_LANGUAGE)
    message(FATAL_ERROR "llvmdsdl_add_showroom_variant(${variant_name}) requires LANGUAGE")
  endif()

  set(_variant_out "${LLVMDSDL_SHOWROOM_OUT_ROOT}/${variant_name}")
  set(_variant_stamp "${LLVMDSDL_SHOWROOM_STAMP_DIR}/${variant_name}.stamp")

  add_custom_command(
    OUTPUT "${_variant_stamp}"
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${_variant_out}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_variant_out}"
    COMMAND
      $<TARGET_FILE:dsdlc>
        --target-language ${ARG_LANGUAGE}
        "${LLVMDSDL_SHOWROOM_DSDL_ROOT}"
        ${ARG_OPTIONS}
        --outdir "${_variant_out}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${LLVMDSDL_SHOWROOM_STAMP_DIR}"
    COMMAND ${CMAKE_COMMAND} -E touch "${_variant_stamp}"
    DEPENDS dsdlc ${LLVMDSDL_SHOWROOM_DSDL_SOURCES}
    VERBATIM
    COMMENT "Showroom: generating ${variant_name}")

  add_custom_target(showroom-${variant_name} DEPENDS "${_variant_stamp}")

  set(LLVMDSDL_SHOWROOM_VARIANT_TARGETS
      ${LLVMDSDL_SHOWROOM_VARIANT_TARGETS} showroom-${variant_name} PARENT_SCOPE)
  set(LLVMDSDL_SHOWROOM_VARIANT_STAMPS
      ${LLVMDSDL_SHOWROOM_VARIANT_STAMPS} "${_variant_stamp}" PARENT_SCOPE)
endfunction()

llvmdsdl_add_showroom_variant(c
  LANGUAGE c)

# The three C++ profiles differ in how they allocate: std uses std::vector, pmr routes through a
# polymorphic allocator, and autosar targets the AUTOSAR C++14 subset. Same DSDL, three surfaces.
llvmdsdl_add_showroom_variant(cpp-std
  LANGUAGE cpp
  OPTIONS --cpp-profile std)

llvmdsdl_add_showroom_variant(cpp-pmr
  LANGUAGE cpp
  OPTIONS --cpp-profile pmr)

llvmdsdl_add_showroom_variant(cpp-autosar
  LANGUAGE cpp
  OPTIONS --cpp-profile autosar)

llvmdsdl_add_showroom_variant(rust-std
  LANGUAGE rust
  OPTIONS --rust-profile std --rust-crate-name lanyard_showroom)

# The no-std-alloc profile is what a flight-controller firmware build would use.
llvmdsdl_add_showroom_variant(rust-no-std-alloc
  LANGUAGE rust
  OPTIONS --rust-profile no-std-alloc --rust-crate-name lanyard_showroom)

llvmdsdl_add_showroom_variant(go
  LANGUAGE go
  OPTIONS --go-module github.com/lanyard/showroom)

llvmdsdl_add_showroom_variant(ts
  LANGUAGE ts
  OPTIONS --ts-module lanyard-showroom)

llvmdsdl_add_showroom_variant(python
  LANGUAGE python
  OPTIONS --py-package lanyard_showroom)

# The MLIR module is not a target language, but it is where the wire-layout facts and the schema
# attributes live -- including `deprecated`, which no language backend currently renders. It is worth
# showing for exactly that reason.
set(LLVMDSDL_SHOWROOM_MLIR_FILE "${LLVMDSDL_SHOWROOM_OUT_ROOT}/mlir/lanyard.mlir")
set(LLVMDSDL_SHOWROOM_MLIR_STAMP "${LLVMDSDL_SHOWROOM_STAMP_DIR}/mlir.stamp")

add_custom_command(
  OUTPUT "${LLVMDSDL_SHOWROOM_MLIR_STAMP}"
  COMMAND
    ${CMAKE_COMMAND}
      -DDSDLC=$<TARGET_FILE:dsdlc>
      -DDSDL_ROOT=${LLVMDSDL_SHOWROOM_DSDL_ROOT}
      -DOUTPUT_FILE=${LLVMDSDL_SHOWROOM_MLIR_FILE}
      -P ${LLVMDSDL_SOURCE_DIR}/cmake/RunShowroomMlir.cmake
  COMMAND ${CMAKE_COMMAND} -E make_directory "${LLVMDSDL_SHOWROOM_STAMP_DIR}"
  COMMAND ${CMAKE_COMMAND} -E touch "${LLVMDSDL_SHOWROOM_MLIR_STAMP}"
  DEPENDS dsdlc ${LLVMDSDL_SHOWROOM_DSDL_SOURCES}
          "${LLVMDSDL_SOURCE_DIR}/cmake/RunShowroomMlir.cmake"
  VERBATIM
  COMMENT "Showroom: generating mlir")

add_custom_target(showroom-mlir DEPENDS "${LLVMDSDL_SHOWROOM_MLIR_STAMP}")

list(APPEND LLVMDSDL_SHOWROOM_VARIANT_TARGETS showroom-mlir)
list(APPEND LLVMDSDL_SHOWROOM_VARIANT_STAMPS "${LLVMDSDL_SHOWROOM_MLIR_STAMP}")

# Not added to ALL: a developer building the compiler should not pay for eleven codegen runs. Build
# it explicitly with `cmake --build <dir> --target showroom`.
add_custom_target(showroom DEPENDS ${LLVMDSDL_SHOWROOM_VARIANT_TARGETS})

# --------------------------------------------------------------------------------------------------
# Browsable documentation.
#
# docs/showroom/ is checked into the repository because the docs workflow builds mkdocs on a runner
# that never compiles dsdlc and therefore cannot generate it. `showroom-docs` regenerates it; CI
# re-runs the target and fails on a dirty tree, which is the same freshness contract the convergence
# and parity reports use.

set(LLVMDSDL_SHOWROOM_DOCS_DIR "${LLVMDSDL_SOURCE_DIR}/docs/showroom")
set(LLVMDSDL_SHOWROOM_INDEX_SCRIPT "${LLVMDSDL_SOURCE_DIR}/tools/showroom/build_showroom_index.py")

if(LLVMDSDL_PYTHON3_EXE)
  add_custom_target(showroom-docs
    COMMAND
      ${LLVMDSDL_PYTHON3_EXE} "${LLVMDSDL_SHOWROOM_INDEX_SCRIPT}"
        --dsdl-root "${LLVMDSDL_SHOWROOM_DSDL_ROOT}"
        --generated-root "${LLVMDSDL_SHOWROOM_OUT_ROOT}"
        --mlir-file "${LLVMDSDL_SHOWROOM_MLIR_FILE}"
        --output-dir "${LLVMDSDL_SHOWROOM_DOCS_DIR}"
    DEPENDS showroom "${LLVMDSDL_SHOWROOM_INDEX_SCRIPT}"
    USES_TERMINAL
    VERBATIM
    COMMENT "Showroom: regenerating docs/showroom from the generated tree")
else()
  add_custom_target(showroom-docs
    COMMAND ${CMAKE_COMMAND} -E echo "python3 not found; cannot run showroom-docs"
    COMMAND ${CMAKE_COMMAND} -E false
    USES_TERMINAL
    VERBATIM
    COMMENT "showroom-docs requires python3")
endif()
