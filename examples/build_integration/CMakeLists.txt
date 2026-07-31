#===----------------------------------------------------------------------===#
#
# Part of the OpenCyphal project, under the MIT licence
# SPDX-License-Identifier: MIT
#
#===----------------------------------------------------------------------===#
#
# Build system integration cells: one (language, build system) pair each, generated, compiled, and
# run. The opposite bet from examples/showroom, which never compiles anything -- here a cell that
# does not build proves nothing, so every cell builds and executes a round-trip program.
#
# CMake's only job here is registration. It does not know how any individual cell builds, and it must
# not: a cell is meant to be copied out of this repository and run on its own, so the moment CMake
# starts supplying include paths or toolchain flags to a cell, the cell has stopped being the thing
# the documentation claims it is. Everything a cell needs is in its own build files, and run_cell.py
# executes them exactly as a reader would.
#
# Each cell becomes one ctest. A cell whose toolchain is absent reports as a ctest skip rather than a
# failure (see EXIT_ALL_SKIPPED in run_cell.py) -- thirteen cells span six ecosystems and no single
# developer machine has all of them. CI is where "skipped" is not allowed to pass silently: the
# workflow names a core subset via --require, which turns a skip into a failure there.

set(LLVMDSDL_BUILDINT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(LLVMDSDL_BUILDINT_RUNNER "${LLVMDSDL_BUILDINT_ROOT}/run_cell.py")
set(LLVMDSDL_BUILDINT_INDEX_SCRIPT
    "${LLVMDSDL_SOURCE_DIR}/tools/build_integration/build_integration_index.py")

if(NOT LLVMDSDL_PYTHON3_EXE)
  message(STATUS "python3 not found; skipping build-integration cells")
  return()
endif()

# CONFIGURE_DEPENDS so that adding a cell re-runs the glob rather than being silently ignored until
# someone reconfigures by hand.
file(GLOB LLVMDSDL_BUILDINT_MANIFESTS CONFIGURE_DEPENDS
     "${LLVMDSDL_BUILDINT_ROOT}/cells/*/cell.json")

# The scratch root for staged cells. Under the build tree, not the source tree: staging copies the
# cell plus dsdl/ and src/ and runs there, and a build that dirtied the source directory it is
# demonstrating would be a poor advertisement.
set(LLVMDSDL_BUILDINT_WORK "${CMAKE_CURRENT_BINARY_DIR}/work")

# A private install tree for the cells that call find_package(llvm-dsdl).
#
# This is also the selftest for the package config, and it has to be an install rather than a build
# tree: llvm-dsdlConfig.cmake finds dsdlc relative to its own installed location, so configuring
# against the build directory would exercise a code path no user ever takes. Whatever breaks the
# config for a user breaks this fixture first.
set(LLVMDSDL_BUILDINT_PREFIX "${CMAKE_CURRENT_BINARY_DIR}/prefix")

add_test(
  NAME llvmdsdl-build-integration-install
  COMMAND "${CMAKE_COMMAND}" --install "${LLVMDSDL_BINARY_DIR}"
          --config $<CONFIG>
          --component ${LLVMDSDL_INSTALL_COMPONENT_BIN}
          --prefix "${LLVMDSDL_BUILDINT_PREFIX}")
set_tests_properties(llvmdsdl-build-integration-install PROPERTIES
  FIXTURES_SETUP build-integration-prefix
  LABELS "build-integration")

foreach(_manifest IN LISTS LLVMDSDL_BUILDINT_MANIFESTS)
  get_filename_component(_cell_dir "${_manifest}" DIRECTORY)
  get_filename_component(_cell_name "${_cell_dir}" NAME)

  add_test(
    NAME llvmdsdl-build-integration-${_cell_name}
    COMMAND "${LLVMDSDL_PYTHON3_EXE}" "${LLVMDSDL_BUILDINT_RUNNER}"
            --dsdlc $<TARGET_FILE:dsdlc>
            --work "${LLVMDSDL_BUILDINT_WORK}"
            --prefix "${LLVMDSDL_BUILDINT_PREFIX}"
            ${_cell_name})

  set_tests_properties(llvmdsdl-build-integration-${_cell_name} PROPERTIES
    SKIP_RETURN_CODE 77
    # Cells fetch build backends and package registries; a stuck network must not hang the suite.
    TIMEOUT 900
    FIXTURES_REQUIRED build-integration-prefix
    LABELS "build-integration")
endforeach()

list(LENGTH LLVMDSDL_BUILDINT_MANIFESTS LLVMDSDL_BUILDINT_COUNT)
message(STATUS "Build-integration cells registered: ${LLVMDSDL_BUILDINT_COUNT}")

# The invariant that makes the matrix readable: a cell carries build wiring and nothing else, so two
# cells in a row differ only by the thing a reader is comparing them for. Cheap enough to be a test
# rather than a lint target, and it needs no toolchain at all.
add_test(
  NAME llvmdsdl-build-integration-invariant
  COMMAND "${LLVMDSDL_PYTHON3_EXE}" "${LLVMDSDL_BUILDINT_RUNNER}" --check-invariant)
set_tests_properties(llvmdsdl-build-integration-invariant PROPERTIES
  LABELS "build-integration")

# --------------------------------------------------------------------------------------------------
# Browsable documentation.
#
# docs/build-integration/ is generated, not committed, as the showroom's pages are. Unlike the
# showroom's generator, this one reads only committed files -- cell manifests and build files -- so
# it needs neither dsdlc nor a generated tree and runs on any checkout. That is why this target has
# no DEPENDS on the compiler.

add_custom_target(build-integration-docs
  COMMAND "${LLVMDSDL_PYTHON3_EXE}" "${LLVMDSDL_BUILDINT_INDEX_SCRIPT}"
          --example-root "${LLVMDSDL_BUILDINT_ROOT}"
          --output-dir "${LLVMDSDL_SOURCE_DIR}/docs/build-integration"
  DEPENDS "${LLVMDSDL_BUILDINT_INDEX_SCRIPT}" "${LLVMDSDL_BUILDINT_ROOT}/README.md"
  USES_TERMINAL
  VERBATIM
  COMMENT "Build integration: regenerating docs/build-integration")

# Joined to the documentation chain from here rather than beside the other docs targets, because
# this directory is processed after they are defined and add_dependencies needs both ends to exist.
# docs-llms lists every published page, so it must run after these pages are on disk.
add_dependencies(docs-llms build-integration-docs)
