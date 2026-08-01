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
# docs/showroom/ is generated, not committed. The docs workflow runs inside the toolshed container,
# builds dsdlc, and invokes this target as part of publishing, so the site is produced from the
# compiler rather than from a checked-in copy that could drift from it. Locally, run this target
# before previewing the site: mkdocs has a nav entry for the showroom overview and --strict fails
# without a page behind it.

set(LLVMDSDL_SHOWROOM_DOCS_DIR "${LLVMDSDL_SOURCE_DIR}/docs/showroom")
set(LLVMDSDL_SHOWROOM_INDEX_SCRIPT "${LLVMDSDL_SOURCE_DIR}/tools/showroom/build_showroom_index.py")

# docs/showroom/index.md is this README rendered for the site, so that the file a contributor edits
# is the file the site shows.
set(LLVMDSDL_SHOWROOM_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")

if(LLVMDSDL_PYTHON3_EXE)
  add_custom_target(showroom-docs
    COMMAND
      ${LLVMDSDL_PYTHON3_EXE} "${LLVMDSDL_SHOWROOM_INDEX_SCRIPT}"
        --dsdl-root "${LLVMDSDL_SHOWROOM_DSDL_ROOT}"
        --generated-root "${LLVMDSDL_SHOWROOM_OUT_ROOT}"
        --mlir-file "${LLVMDSDL_SHOWROOM_MLIR_FILE}"
        --output-dir "${LLVMDSDL_SHOWROOM_DOCS_DIR}"
        --readme "${LLVMDSDL_SHOWROOM_README}"
        --recipes-root "${CMAKE_CURRENT_SOURCE_DIR}/recipes"
        --recipes-readme "${CMAKE_CURRENT_SOURCE_DIR}/RECIPES.md"
    DEPENDS showroom "${LLVMDSDL_SHOWROOM_INDEX_SCRIPT}" "${LLVMDSDL_SHOWROOM_README}"
            "${CMAKE_CURRENT_SOURCE_DIR}/RECIPES.md"
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


# --------------------------------------------------------------------------------------------------
# Build recipes.
#
# The other half of the showroom, and it makes the opposite bet from the half above: these compile
# and run. Browsing must never fail for a reason unrelated to what it is showing, so it generates and
# stops; a recipe that did not build would prove nothing, so every recipe builds and executes a
# round-trip program. Same namespace, same documentation, two policies -- deliberately.
#
# CMake's only job here is registration. It does not know how any individual recipe builds, and it
# must not: a recipe is meant to be copied out of this repository and run on its own, so the moment
# CMake starts supplying include paths or toolchain flags the recipe has stopped being the thing the
# documentation claims it is. Everything a recipe needs is in its own build files, and run_recipe.py
# executes them exactly as a reader would.

set(LLVMDSDL_RECIPES_RUNNER "${CMAKE_CURRENT_SOURCE_DIR}/run_recipe.py")

# CONFIGURE_DEPENDS so that adding a recipe re-runs the glob rather than being silently ignored until
# someone reconfigures by hand.
file(GLOB LLVMDSDL_RECIPE_MANIFESTS CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/recipes/*/recipe.json")

# Scratch root for staged recipes. Under the build tree, not the source tree: staging copies the
# recipe plus dsdl/ and src/ and runs there, and a build that dirtied the source directory it is
# demonstrating would be a poor advertisement.
set(LLVMDSDL_RECIPES_WORK "${CMAKE_CURRENT_BINARY_DIR}/recipe-work")

# A private install tree for the recipes that call find_package(llvm-dsdl).
#
# This is also the selftest for the package config, and it has to be an install rather than a build
# tree: llvm-dsdlConfig.cmake finds dsdlc relative to its own installed location, so configuring
# against the build directory would exercise a code path no user ever takes. Whatever breaks the
# config for a user breaks this fixture first.
set(LLVMDSDL_RECIPES_PREFIX "${CMAKE_CURRENT_BINARY_DIR}/prefix")

add_test(
  NAME llvmdsdl-showroom-recipe-install
  COMMAND "${CMAKE_COMMAND}" --install "${LLVMDSDL_BINARY_DIR}"
          --config $<CONFIG>
          --component ${LLVMDSDL_INSTALL_COMPONENT_BIN}
          --prefix "${LLVMDSDL_RECIPES_PREFIX}")
set_tests_properties(llvmdsdl-showroom-recipe-install PROPERTIES
  FIXTURES_SETUP showroom-recipe-prefix
  LABELS "showroom-recipes")

foreach(_manifest IN LISTS LLVMDSDL_RECIPE_MANIFESTS)
  get_filename_component(_recipe_dir "${_manifest}" DIRECTORY)
  get_filename_component(_recipe_name "${_recipe_dir}" NAME)

  add_test(
    NAME llvmdsdl-showroom-recipe-${_recipe_name}
    COMMAND "${LLVMDSDL_PYTHON3_EXE}" "${LLVMDSDL_RECIPES_RUNNER}"
            --dsdlc $<TARGET_FILE:dsdlc>
            --work "${LLVMDSDL_RECIPES_WORK}"
            --prefix "${LLVMDSDL_RECIPES_PREFIX}"
            ${_recipe_name})

  set_tests_properties(llvmdsdl-showroom-recipe-${_recipe_name} PROPERTIES
    SKIP_RETURN_CODE 77
    # Recipes fetch build backends and package registries; a stuck network must not hang the suite.
    TIMEOUT 900
    FIXTURES_REQUIRED showroom-recipe-prefix
    LABELS "showroom-recipes")
endforeach()

list(LENGTH LLVMDSDL_RECIPE_MANIFESTS LLVMDSDL_RECIPE_COUNT)
message(STATUS "Showroom recipes registered: ${LLVMDSDL_RECIPE_COUNT}")

# The invariant that makes the matrix readable: a recipe carries build wiring and nothing else, so
# two recipes in a row differ only by the thing a reader is comparing them for. Cheap enough to be a
# test rather than a lint target, and it needs no toolchain at all.
add_test(
  NAME llvmdsdl-showroom-recipe-invariant
  COMMAND "${LLVMDSDL_PYTHON3_EXE}" "${LLVMDSDL_RECIPES_RUNNER}" --check-invariant)
set_tests_properties(llvmdsdl-showroom-recipe-invariant PROPERTIES
  LABELS "showroom-recipes")

# Joined to the documentation chain from here rather than beside the other docs targets, because this
# directory is processed after they are defined and add_dependencies needs both ends to exist.
# docs-llms lists every published page, so it must run after these pages are on disk.
add_dependencies(docs-llms showroom-docs)
