#===----------------------------------------------------------------------===#
##
## @file
## CPack configuration for release artifacts.
##
## Two outputs, from one install tree:
##
##   TGZ  -- the per-platform distribution tarball. A single archive holding every
##           component, uploaded by the release build job and repacked by each
##           downstream package format (Homebrew bottle, snap, Windows zip).
##   DEB  -- two component packages, `llvm-dsdl` (tools) and `llvm-dsdl-dev`
##           (static libraries + headers). Linux only.
##
## `cpack` needs the build tree -- it re-runs the install rules -- but never a
## compiler, so the release pipeline invokes it in the build job. Adding a format
## later stays cheap: RPM is another generator here, and archive-based formats
## consume the TGZ without touching this file.
##
## See docs/design/release-packaging.md §4.
##
#===----------------------------------------------------------------------===#

set(CPACK_PACKAGE_NAME "llvm-dsdl")
set(CPACK_PACKAGE_VENDOR "OpenCyphal")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/OpenCyphal-Garage/llvm-dsdl")

# CPACK_PACKAGE_DESCRIPTION_SUMMARY is deliberately NOT set, and neither is
# project()'s DESCRIPTION. Debian wants each binary package to carry its own
# one-line synopsis, but CPack has no per-component synopsis variable: it
# prepends the single global summary to every component's description, so
# setting one gives llvm-dsdl-dev the *tools* synopsis and duplicates the line on
# llvm-dsdl. Left unset, the summary equals CPack's own default, the prepend is
# skipped, and each CPACK_DEBIAN_<COMPONENT>_DESCRIPTION below is authoritative
# -- first line synopsis, rest extended description, per Debian policy.
# Setting either variable re-enables the prepend and reintroduces both faults.
set(CPACK_PACKAGE_CONTACT "OpenCyphal Maintainers <maintainers@opencyphal.org>")

# The VERSION file is the single source of truth (project() carries no VERSION).
# The release pipeline asserts the git tag matches it, so what a package claims
# and what `dsdlc --version` reports cannot diverge.
set(CPACK_PACKAGE_VERSION "${LLVMDSDL_TOOL_VERSION}")
set(CPACK_PACKAGE_VERSION_MAJOR "${LLVMDSDL_VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR "${LLVMDSDL_VERSION_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH "${LLVMDSDL_VERSION_PATCH}")

set(CPACK_RESOURCE_FILE_LICENSE "${LLVMDSDL_SOURCE_DIR}/LICENSE.md")
set(CPACK_RESOURCE_FILE_README "${LLVMDSDL_SOURCE_DIR}/README.md")

set(CPACK_PACKAGE_CHECKSUM "SHA256")

#===----------------------------------------------------------------------===#
# Platform triple
#
# Names the distribution tarball so the release matrix can address artifacts by
# cell (llvm-dsdl-0.1.0-linux-amd64.tar.gz). Normalised to the plan's vocabulary
# rather than the raw uname spelling, which varies by host.
#===----------------------------------------------------------------------===#

string(TOLOWER "${CMAKE_SYSTEM_NAME}" LLVMDSDL_PACKAGE_OS)
if(LLVMDSDL_PACKAGE_OS STREQUAL "darwin")
  set(LLVMDSDL_PACKAGE_OS "darwin")
endif()

set(LLVMDSDL_PACKAGE_ARCH "${CMAKE_SYSTEM_PROCESSOR}")
if(CMAKE_OSX_ARCHITECTURES)
  # A macOS build can be told to target an architecture other than the host's;
  # honour that over uname so a cross-built artifact is not mislabelled.
  list(LENGTH CMAKE_OSX_ARCHITECTURES LLVMDSDL_OSX_ARCH_COUNT)
  if(LLVMDSDL_OSX_ARCH_COUNT EQUAL 1)
    set(LLVMDSDL_PACKAGE_ARCH "${CMAKE_OSX_ARCHITECTURES}")
  else()
    set(LLVMDSDL_PACKAGE_ARCH "universal")
  endif()
endif()
string(TOLOWER "${LLVMDSDL_PACKAGE_ARCH}" LLVMDSDL_PACKAGE_ARCH)
if(LLVMDSDL_PACKAGE_ARCH MATCHES "^(x86_64|amd64)$")
  set(LLVMDSDL_PACKAGE_ARCH "amd64")
elseif(LLVMDSDL_PACKAGE_ARCH MATCHES "^(aarch64|arm64)$")
  set(LLVMDSDL_PACKAGE_ARCH "arm64")
endif()

set(LLVMDSDL_PACKAGE_TRIPLE "${LLVMDSDL_PACKAGE_OS}-${LLVMDSDL_PACKAGE_ARCH}")
set(CPACK_PACKAGE_FILE_NAME
    "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-${LLVMDSDL_PACKAGE_TRIPLE}")
message(STATUS "Packaging triple: ${LLVMDSDL_PACKAGE_TRIPLE}")

#===----------------------------------------------------------------------===#
# Components
#===----------------------------------------------------------------------===#

set(CPACK_COMPONENTS_ALL
    ${LLVMDSDL_INSTALL_COMPONENT_BIN}
    ${LLVMDSDL_INSTALL_COMPONENT_DEV})

set(CPACK_COMPONENT_BIN_DISPLAY_NAME "Tools")
set(CPACK_COMPONENT_BIN_DESCRIPTION
    "The dsdlc code generator, the dsdl-opt MLIR driver, and the dsdld language server.")

set(CPACK_COMPONENT_DEV_DISPLAY_NAME "Development files")
set(CPACK_COMPONENT_DEV_DESCRIPTION
    "Static libraries and headers for embedding the llvm-dsdl frontend, dialect, and emitters.")
set(CPACK_COMPONENT_DEV_DEPENDS ${LLVMDSDL_INSTALL_COMPONENT_BIN})

#===----------------------------------------------------------------------===#
# Generators
#
# The archive is deliberately NOT component-split: the pipeline wants exactly one
# tarball per platform. DEB is component-split, which is what yields the
# llvm-dsdl / llvm-dsdl-dev pair. The two knobs are independent.
#===----------------------------------------------------------------------===#

set(CPACK_GENERATOR "TGZ")
set(CPACK_ARCHIVE_COMPONENT_INSTALL OFF)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  list(APPEND CPACK_GENERATOR "DEB")
endif()

# Multi-config generators (the presets use Ninja Multi-Config) build every config
# into one tree, so cpack must be told which one to package. `cpack -C <cfg>`
# overrides this; without a default, an unqualified `cpack` can silently package
# the wrong config or nothing at all.
if(CMAKE_CONFIGURATION_TYPES)
  if("RelWithDebInfo" IN_LIST CMAKE_CONFIGURATION_TYPES)
    set(CPACK_BUILD_CONFIG "RelWithDebInfo")
  else()
    list(GET CMAKE_CONFIGURATION_TYPES 0 CPACK_BUILD_CONFIG)
  endif()
endif()

#===----------------------------------------------------------------------===#
# Debian
#===----------------------------------------------------------------------===#

set(CPACK_DEB_COMPONENT_INSTALL ON)
set(CPACK_DEBIAN_FILE_NAME "DEB-DEFAULT")

# xz over CPack's default gzip. The package is dominated by a vendored libLLVM
# that compresses well, and this is a file people download directly rather than
# fetch from a mirror: 74 MB -> 46 MB measured on the arm64 package. Costs a
# little packaging time and nothing at install time.
set(CPACK_DEBIAN_COMPRESSION_TYPE "xz")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "${CPACK_PACKAGE_CONTACT}")
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "${CPACK_PACKAGE_HOMEPAGE_URL}")
set(CPACK_DEBIAN_PACKAGE_SECTION "devel")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")

# Dependency resolution is deliberately manual. dpkg-shlibdeps resolves each
# linked library against the local dpkg database; release .debs vendor libLLVM
# into a private libdir, which it would either fail on or map to a wrong
# system package. The correct list is derived mechanically on the target
# distribution (objdump -p over the staged binaries, minus the private libdir,
# through dpkg -S) and passed in by the release job -- it cannot be derived on a
# developer's machine, and a guess here would be worse than an honest baseline.
# The clean-container install in the release verify lane is what proves it right.
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS OFF)
set(LLVMDSDL_DEB_DEPENDS "" CACHE STRING
    "Debian Depends: for the llvm-dsdl package. Empty means derive it from the built \
binaries at package time, which is what you want; set it only to pin a list deliberately.")
set(CPACK_DEBIAN_BIN_PACKAGE_DEPENDS "${LLVMDSDL_DEB_DEPENDS}")

# Deriving the list needs the binaries, which do not exist at configure time, so
# it happens in a CPack project-config script evaluated after the build.
#
# The binary paths must come from $<TARGET_FILE:...>, and under a multi-config
# generator those differ per configuration. So the *paths* are written to one
# small file per configuration -- $<CONFIG> in the OUTPUT path, which is what
# file(GENERATE) supports -- and the project-config script, which is a single
# config-agnostic file, reads whichever one matches the configuration being
# packaged. Putting the paths directly in the script instead makes CMake try to
# write one path with several different contents, which it rejects outright:
#
#   Evaluation file to be written multiple times with different content.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND LLVMDSDL_PYTHON3_EXE)
  set(LLVMDSDL_DEPENDS_ANALYSIS_TARGETS
      "$<TARGET_FILE:dsdlc>;$<TARGET_FILE:dsdl-opt>;$<TARGET_FILE:dsdld>")
  set(LLVMDSDL_DEPENDS_VENDORED_ARGS "")
  if(LLVMDSDL_VENDOR_LLVM AND LLVMDSDL_LLVM_SHARED_LIB_REAL)
    # The vendored library's own dependencies are equally load-bearing: omitting
    # them is precisely what makes a package install cleanly and then fail.
    string(APPEND LLVMDSDL_DEPENDS_ANALYSIS_TARGETS ";${LLVMDSDL_LLVM_SHARED_LIB_REAL}")
    # ...and the library itself must not become a dependency on itself.
    set(LLVMDSDL_DEPENDS_VENDORED_ARGS "--vendored;libLLVM.so")
  endif()

  # CMAKE_BINARY_DIR rather than LLVMDSDL_BINARY_DIR: this file is also included
  # by the packaging test harness, which does not define the project's own vars.
  set(LLVMDSDL_DEPENDS_TARGETS_STEM "${CMAKE_BINARY_DIR}/packaging/analysis-targets")
  file(GENERATE
    OUTPUT "${LLVMDSDL_DEPENDS_TARGETS_STEM}-$<CONFIG>.txt"
    CONTENT "${LLVMDSDL_DEPENDS_ANALYSIS_TARGETS}")

  # Single-config generators leave CPACK_BUILD_CONFIG empty, so the script needs
  # the build type to fall back on; multi-config sets it above.
  set(LLVMDSDL_DEPENDS_FALLBACK_CONFIG "${CMAKE_BUILD_TYPE}")

  set(LLVMDSDL_CPACK_PROJECT_CONFIG "${CMAKE_BINARY_DIR}/PackagingProjectConfig.cmake")
  configure_file(
    "${LLVMDSDL_SOURCE_DIR}/cmake/PackagingProjectConfig.cmake.in"
    "${LLVMDSDL_CPACK_PROJECT_CONFIG}"
    @ONLY)
  set(CPACK_PROJECT_CONFIG_FILE "${LLVMDSDL_CPACK_PROJECT_CONFIG}")
endif()

# Each description is ONE string whose first line is the synopsis and whose
# remaining lines are the extended description. CPack indents the continuation
# lines to Debian's control-file format itself, so they are written plain here.
# Passing several quoted arguments instead would make a CMake list, which would
# reach the control file semicolon-joined. Keep body lines under ~76 columns:
# CPack adds the leading space, and lintian rejects a control line over 80.
set(CPACK_DEBIAN_BIN_PACKAGE_NAME "llvm-dsdl")
set(CPACK_DEBIAN_BIN_DESCRIPTION
    "DSDL frontend, MLIR lowerer, and multi-language code generator
Compiles Cyphal DSDL definitions to C, C++, Rust, Go, TypeScript, and
Python through an MLIR dialect shared by every backend. Also provides
dsdl-opt, a driver for the dsdl dialect's pass pipeline, and dsdld, a
language server for DSDL.")

set(CPACK_DEBIAN_DEV_PACKAGE_NAME "llvm-dsdl-dev")
set(CPACK_DEBIAN_DEV_DESCRIPTION
    "Development files for llvm-dsdl
Static libraries and headers for embedding the llvm-dsdl frontend, MLIR
dialect, and language emitters in another tool.")
# Lockstep with the tools package: the static libraries and headers are only
# coherent against the exact build they shipped with.
set(CPACK_DEBIAN_DEV_PACKAGE_DEPENDS "llvm-dsdl (= ${CPACK_PACKAGE_VERSION})")

include(CPack)
