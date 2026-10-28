cmake_minimum_required(VERSION 3.24)
project(llvm-dsdl LANGUAGES C CXX)
include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(LLVMDSDL_VERSION_FILE "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")
if(NOT EXISTS "${LLVMDSDL_VERSION_FILE}")
  message(FATAL_ERROR "Missing VERSION file: ${LLVMDSDL_VERSION_FILE}")
endif()

file(READ "${LLVMDSDL_VERSION_FILE}" LLVMDSDL_TOOL_VERSION_RAW)
string(STRIP "${LLVMDSDL_TOOL_VERSION_RAW}" LLVMDSDL_TOOL_VERSION)
if(NOT LLVMDSDL_TOOL_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
  message(FATAL_ERROR
    "Invalid VERSION string '${LLVMDSDL_TOOL_VERSION}'. Expected semantic version: MAJOR.MINOR.PATCH")
endif()

string(REPLACE "." ";" LLVMDSDL_VERSION_PARTS "${LLVMDSDL_TOOL_VERSION}")
list(GET LLVMDSDL_VERSION_PARTS 0 LLVMDSDL_VERSION_MAJOR)
list(GET LLVMDSDL_VERSION_PARTS 1 LLVMDSDL_VERSION_MINOR)
list(GET LLVMDSDL_VERSION_PARTS 2 LLVMDSDL_VERSION_PATCH)

configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/include/llvmdsdl/Version.h.in"
  "${CMAKE_CURRENT_BINARY_DIR}/include/llvmdsdl/Version.h"
  @ONLY
)
message(STATUS "llvm-dsdl tool version: ${LLVMDSDL_TOOL_VERSION}")

# Some distro LLVM packages export LLVMSupport with zstd::libzstd_* targets but
# do not bring zstd in transitively. Preload zstd to avoid unresolved imported
# target references during find_package(LLVM).
find_package(zstd CONFIG QUIET)
if(TARGET ZSTD::ZSTD)
  if(NOT TARGET zstd::libzstd_shared)
    add_library(zstd::libzstd_shared INTERFACE IMPORTED)
    target_link_libraries(zstd::libzstd_shared INTERFACE ZSTD::ZSTD)
  endif()
  if(NOT TARGET zstd::libzstd_static)
    add_library(zstd::libzstd_static INTERFACE IMPORTED)
    target_link_libraries(zstd::libzstd_static INTERFACE ZSTD::ZSTD)
  endif()
endif()

find_package(LLVM REQUIRED CONFIG)
find_package(MLIR REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")
message(STATUS "Using MLIRConfig.cmake in: ${MLIR_DIR}")

# LLVM major-version lock. The linked major is a semantic input to the generated code: MLIR's
# canonicaliser runs over every serialisation body and LLVM's code generator assembles the object
# lane -- see docs/reference/guarantees/supply-chain.md. Asserting it at configure time makes the
# lock authoritative everywhere, so a build against another major fails loudly instead of silently
# producing divergent output.
#
# Bumping the major is a one-line change here plus updating the CI pins together. A maintainer
# evaluating another major can opt out with -DLLVMDSDL_ALLOW_LLVM_MAJOR_MISMATCH=ON; MLIR ships with
# LLVM at the same version, so asserting the LLVM major covers MLIR too.
set(LLVMDSDL_REQUIRED_LLVM_MAJOR "22" CACHE STRING
    "LLVM/MLIR major version this project is locked to (see docs/reference/guarantees/supply-chain.md).")
option(LLVMDSDL_ALLOW_LLVM_MAJOR_MISMATCH
       "Permit configuring against an LLVM major other than LLVMDSDL_REQUIRED_LLVM_MAJOR." OFF)
if(NOT LLVM_VERSION_MAJOR STREQUAL LLVMDSDL_REQUIRED_LLVM_MAJOR)
  if(LLVMDSDL_ALLOW_LLVM_MAJOR_MISMATCH)
    message(WARNING
      "LLVM major ${LLVM_VERSION_MAJOR} != locked major ${LLVMDSDL_REQUIRED_LLVM_MAJOR}; "
      "byte-reproducibility of the generated code is not guaranteed against this toolchain.")
  else()
    message(FATAL_ERROR
      "LLVM/MLIR major ${LLVM_VERSION_MAJOR} does not match the locked major "
      "${LLVMDSDL_REQUIRED_LLVM_MAJOR} (found LLVM ${LLVM_PACKAGE_VERSION}). Point LLVM_DIR/MLIR_DIR at "
      "an LLVM ${LLVMDSDL_REQUIRED_LLVM_MAJOR} install, or, to evaluate another major deliberately, "
      "reconfigure with -DLLVMDSDL_ALLOW_LLVM_MAJOR_MISMATCH=ON. See docs/reference/guarantees/supply-chain.md.")
  endif()
endif()
message(STATUS "LLVM major-version lock: ${LLVMDSDL_REQUIRED_LLVM_MAJOR} (linked ${LLVM_VERSION_MAJOR})")

if(APPLE)
  find_program(LLVMDSDL_OTOOL_EXE NAMES otool otool-classic)
  set(LLVMDSDL_LLVM_DYLIB "")
  foreach(llvm_lib_dir IN LISTS LLVM_LIBRARY_DIRS)
    if(EXISTS "${llvm_lib_dir}/libLLVM.dylib")
      set(LLVMDSDL_LLVM_DYLIB "${llvm_lib_dir}/libLLVM.dylib")
      break()
    endif()
  endforeach()

  if(LLVMDSDL_OTOOL_EXE AND LLVMDSDL_LLVM_DYLIB)
    execute_process(
      COMMAND "${LLVMDSDL_OTOOL_EXE}" -l "${LLVMDSDL_LLVM_DYLIB}"
      OUTPUT_VARIABLE LLVMDSDL_OTOOL_OUTPUT
      ERROR_QUIET
      OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    string(REGEX MATCH "minos[ \t]+([0-9]+\\.[0-9]+)" LLVMDSDL_LLVM_MINOS_MATCH "${LLVMDSDL_OTOOL_OUTPUT}")
    if(CMAKE_MATCH_1)
      set(LLVMDSDL_LLVM_MINOS "${CMAKE_MATCH_1}")
      if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "${LLVMDSDL_LLVM_MINOS}" CACHE STRING "Minimum macOS deployment target" FORCE)
        message(STATUS "Setting CMAKE_OSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET} (from LLVM minos)")
      elseif(CMAKE_OSX_DEPLOYMENT_TARGET VERSION_LESS LLVMDSDL_LLVM_MINOS)
        message(STATUS
          "Raising CMAKE_OSX_DEPLOYMENT_TARGET from ${CMAKE_OSX_DEPLOYMENT_TARGET} "
          "to ${LLVMDSDL_LLVM_MINOS} to match linked LLVM/MLIR binaries")
        set(CMAKE_OSX_DEPLOYMENT_TARGET "${LLVMDSDL_LLVM_MINOS}" CACHE STRING "Minimum macOS deployment target" FORCE)
      endif()
    endif()
  endif()
endif()

list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")
list(APPEND CMAKE_MODULE_PATH "${MLIR_CMAKE_DIR}")

include(TableGen)
include(AddLLVM)
include(AddMLIR)
include(HandleLLVMOptions)

include_directories(SYSTEM ${LLVM_INCLUDE_DIRS})
include_directories(SYSTEM ${MLIR_INCLUDE_DIRS})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
include_directories(${CMAKE_CURRENT_BINARY_DIR}/include)
# Packaged LLVM records the ABI it was built against by re-emitting
# -D_GLIBCXX_USE_CXX11_ABI in LLVM_DEFINITIONS. The toolshed's GCC driver
# already injects that same macro on <command-line>, so applying LLVM's copy
# makes every translation unit warn about a command-line macro redefinition.
# Drop LLVM's duplicate and keep the compiler's own definition; all the other
# LLVM defines (__STDC_CONSTANT_MACROS, etc.) are preserved.
string(REPLACE " " ";" _llvmdsdl_llvm_definitions "${LLVM_DEFINITIONS}")
list(FILTER _llvmdsdl_llvm_definitions EXCLUDE REGEX "_GLIBCXX_USE_CXX11_ABI")
add_definitions(${_llvmdsdl_llvm_definitions})
unset(_llvmdsdl_llvm_definitions)

if(APPLE AND LLVM_LINKER_SUPPORTS_NO_WARN_DUPLICATE_LIBRARIES)
  add_link_options("-Wl,-no_warn_duplicate_libraries")
endif()

# --- Warnings are errors ----------------------------------------------------
# HandleLLVMOptions already gives every target LLVM's warning set (-Wall -Wextra
# plus the -W flags LLVM curates), but it only appends -Werror when
# LLVM_ENABLE_WERROR is set, and that option lives in LLVM's own top-level
# CMakeLists -- it is undefined in a standalone project like this one.
#
# Deliberately not behind a project option: a warning here is a defect report,
# and the answer to one is to fix the code. The warning set is not uniform
# across compilers -- CI builds the primary lane with GCC and the sanitizer lane
# with Clang -- so this fires on diagnostics a single local toolchain never
# shows.
#
# CMAKE_COMPILE_WARNING_AS_ERROR (CMake 3.24+, which this project already
# requires) rather than appending -Werror to CMAKE_CXX_FLAGS: it spells itself
# /WX on MSVC without a second branch here, and leaves
# try_compile/check_*_compiler_flag probes alone, so a probe for an unsupported
# flag still fails for the right reason.
set(CMAKE_COMPILE_WARNING_AS_ERROR ON)

set(LLVMDSDL_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(LLVMDSDL_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR})

find_program(LLVMDSDL_CLANG_FORMAT_EXE
  NAMES clang-format clang-format-22 clang-format-21 clang-format-20 clang-format-19
)

find_program(LLVMDSDL_CLANG_TIDY_EXE
  NAMES clang-tidy clang-tidy-22 clang-tidy-21 clang-tidy-20 clang-tidy-19
)

find_program(LLVMDSDL_PYTHON3_EXE
  NAMES python3 python
)

option(LLVMDSDL_ENABLE_LLVM_COVERAGE
       "Enable LLVM source-based coverage instrumentation and reporting targets"
       OFF)

option(LLVMDSDL_DISTCLEAN_REMOVE_VENV
       "Remove repository .venv when running distclean"
       OFF)

# Fully static tools.
#
# Intended for the musl toolchain, where it produces static-pie executables with
# no loader dependency at all; the .deb then carries no Depends rather than the
# two it carries when linked against glibc.
#
# Deliberately not the default: against glibc this links but is a trap, because
# NSS and dlopen need the shared libc at runtime. See D6 in
# docs/development/distribution-channels.md.
option(LLVMDSDL_STATIC_BINARIES
       "Link the tools fully statically. Linux only; intended for the musl toolchain."
       OFF)

if(LLVMDSDL_STATIC_BINARIES)
  if(APPLE)
    # Apple does not ship a static libSystem and does not support linking one;
    # there is no supported static crt0. The macOS equivalent of this flag is
    # LLVMDSDL_STATIC_LIBCXX.
    message(FATAL_ERROR
      "LLVMDSDL_STATIC_BINARIES is not available on macOS: Apple does not support "
      "statically linking libSystem. Use LLVMDSDL_STATIC_LIBCXX instead.")
  endif()
  if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "LLVMDSDL_STATIC_BINARIES is implemented for Linux only.")
  endif()
  add_link_options(-static -static-libstdc++ -static-libgcc)
  message(STATUS "Linking tools fully statically")
endif()

# Static libc++, from the toolchain prefix. macOS only.
#
# Requires a toolchain built with `build_llvm.py --with-libcxx`; Apple ships
# libc++ as a dylib only, so there is otherwise nothing to link. With it the
# tools link /usr/lib/libSystem.B.dylib and nothing else.
#
# Linux does not need it: LLVMDSDL_STATIC_BINARIES covers the C++ runtime via
# -static-libstdc++.
option(LLVMDSDL_STATIC_LIBCXX
       "Link libc++ statically from the LLVM toolchain prefix (macOS)."
       OFF)

if(LLVMDSDL_STATIC_LIBCXX)
  if(NOT APPLE)
    message(FATAL_ERROR
      "LLVMDSDL_STATIC_LIBCXX is for macOS. On Linux use LLVMDSDL_STATIC_BINARIES, "
      "which links the C++ runtime statically through -static-libstdc++.")
  endif()

  # The archives sit beside the LLVM ones because build_llvm.py installs the
  # runtimes into the same prefix.
  get_filename_component(_llvmdsdl_tc_prefix "${LLVM_LIBRARY_DIRS}" DIRECTORY)
  set(_llvmdsdl_libcxx_a "${LLVM_LIBRARY_DIRS}/libc++.a")
  set(_llvmdsdl_libcxxabi_a "${LLVM_LIBRARY_DIRS}/libc++abi.a")
  set(_llvmdsdl_libcxx_inc "${_llvmdsdl_tc_prefix}/include/c++/v1")

  foreach(_needed "${_llvmdsdl_libcxx_a}" "${_llvmdsdl_libcxxabi_a}" "${_llvmdsdl_libcxx_inc}")
    if(NOT EXISTS "${_needed}")
      message(FATAL_ERROR
        "LLVMDSDL_STATIC_LIBCXX is ON but ${_needed} is missing. The toolchain was "
        "built without a static libc++; rebuild it with "
        "packaging/toolchain/build_llvm.py --with-libcxx.")
    endif()
  endforeach()

  # -nostdinc++ so libc++'s own headers win, and are able to #include_next the
  # SDK's C headers; -isystem alone leaves the default C++ include path ahead of
  # them and the build fails on errno.
  add_compile_options(
    "$<$<COMPILE_LANGUAGE:CXX>:-nostdinc++>"
    "$<$<COMPILE_LANGUAGE:CXX>:-isystem;${_llvmdsdl_libcxx_inc}>")
  add_link_options(-nostdlib++ "${_llvmdsdl_libcxx_a}" "${_llvmdsdl_libcxxabi_a}")
  message(STATUS "Linking libc++ statically from ${_llvmdsdl_tc_prefix}")
endif()

set(LLVMDSDL_COVERAGE_CTEST_ARGS
    "--output-on-failure"
    CACHE STRING
    "Additional arguments forwarded to ctest by coverage-run (space-separated).")

if(LLVMDSDL_ENABLE_LLVM_COVERAGE)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang")
    message(FATAL_ERROR
      "LLVM source coverage requires Clang/AppleClang. "
      "Current C++ compiler: ${CMAKE_CXX_COMPILER_ID}")
  endif()

  add_compile_options(-fprofile-instr-generate -fcoverage-mapping)
  add_link_options(-fprofile-instr-generate -fcoverage-mapping)

  find_program(LLVMDSDL_LLVM_PROFDATA_EXE
    NAMES llvm-profdata llvm-profdata-22 llvm-profdata-21 llvm-profdata-20 llvm-profdata-19
    HINTS ${LLVM_TOOLS_BINARY_DIR} /opt/homebrew/opt/llvm/bin
  )

  find_program(LLVMDSDL_LLVM_COV_EXE
    NAMES llvm-cov llvm-cov-22 llvm-cov-21 llvm-cov-20 llvm-cov-19
    HINTS ${LLVM_TOOLS_BINARY_DIR} /opt/homebrew/opt/llvm/bin
  )

  if(NOT LLVMDSDL_LLVM_PROFDATA_EXE OR NOT LLVMDSDL_LLVM_COV_EXE)
    message(FATAL_ERROR
      "LLVM source coverage is enabled but llvm-profdata/llvm-cov could not be found. "
      "Set LLVM_TOOLS_BINARY_DIR or ensure tools are on PATH.")
  endif()

  message(STATUS "LLVM source coverage enabled")
  message(STATUS "Using llvm-profdata: ${LLVMDSDL_LLVM_PROFDATA_EXE}")
  message(STATUS "Using llvm-cov: ${LLVMDSDL_LLVM_COV_EXE}")
endif()

# --- Sanitizers -------------------------------------------------------------
# Instruments every target in the build, the unit tests and their in-process fuzzers included.
# Use the Debug configuration so NDEBUG is absent and assert() guards fire beside the sanitizer.
set(LLVMDSDL_ENABLE_SANITIZERS
    ""
    CACHE STRING
    "Comma-separated -fsanitize list (e.g. \"address,undefined\"). Empty = off. Requires Clang/AppleClang.")

if(LLVMDSDL_ENABLE_SANITIZERS)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang")
    message(FATAL_ERROR
      "LLVMDSDL_ENABLE_SANITIZERS requires Clang/AppleClang. "
      "Current C++ compiler: ${CMAKE_CXX_COMPILER_ID}")
  endif()
  add_compile_options(
    -fsanitize=${LLVMDSDL_ENABLE_SANITIZERS}
    -fno-sanitize-recover=all
    -fno-omit-frame-pointer
    -g)
  add_link_options(-fsanitize=${LLVMDSDL_ENABLE_SANITIZERS})
  message(STATUS "Sanitizers enabled: ${LLVMDSDL_ENABLE_SANITIZERS}")
endif()

if(NOT LLVMDSDL_CLANG_FORMAT_EXE)
  message(WARNING
    "clang-format not found; check-format/format-source targets will fail "
    "until it is installed.")
endif()

if(NOT LLVMDSDL_CLANG_TIDY_EXE)
  message(WARNING
    "clang-tidy not found; check-clang-tidy target will fail "
    "until it is installed.")
endif()

if(NOT LLVMDSDL_PYTHON3_EXE)
  message(WARNING
    "python3 not found; the docs targets and the Python tests will fail "
    "until it is installed.")
endif()

add_custom_target(check-clang-format
  COMMAND
    ${CMAKE_COMMAND}
      -DCLANG_FORMAT=${LLVMDSDL_CLANG_FORMAT_EXE}
      -DLLVMDSDL_SOURCE_DIR=${LLVMDSDL_SOURCE_DIR}
      -P ${LLVMDSDL_SOURCE_DIR}/cmake/CheckClangFormat.cmake
  USES_TERMINAL
  VERBATIM
  COMMENT "Checking source formatting with clang-format"
)

add_custom_target(check-format
  DEPENDS check-clang-format
)

add_custom_target(format-source
  COMMAND
    ${CMAKE_COMMAND}
      -DCLANG_FORMAT=${LLVMDSDL_CLANG_FORMAT_EXE}
      -DLLVMDSDL_SOURCE_DIR=${LLVMDSDL_SOURCE_DIR}
      -P ${LLVMDSDL_SOURCE_DIR}/cmake/RunClangFormat.cmake
  USES_TERMINAL
  VERBATIM
  COMMENT "Reformatting project source with clang-format"
)

add_custom_target(check-clang-tidy
  COMMAND
    ${CMAKE_COMMAND}
      -DCLANG_TIDY=${LLVMDSDL_CLANG_TIDY_EXE}
      -DLLVMDSDL_SOURCE_DIR=${LLVMDSDL_SOURCE_DIR}
      -DLLVMDSDL_BINARY_DIR=${LLVMDSDL_BINARY_DIR}
      -P ${LLVMDSDL_SOURCE_DIR}/cmake/CheckClangTidy.cmake
  USES_TERMINAL
  VERBATIM
  COMMENT "Checking source diagnostics with clang-tidy"
)

add_custom_target(distclean
  COMMAND
    ${CMAKE_COMMAND}
      -DSOURCE_DIR=${LLVMDSDL_SOURCE_DIR}
      -DBINARY_DIR=${LLVMDSDL_BINARY_DIR}
      -DREMOVE_VENV=${LLVMDSDL_DISTCLEAN_REMOVE_VENV}
      -P ${LLVMDSDL_SOURCE_DIR}/cmake/DistClean.cmake
  USES_TERMINAL
  VERBATIM
  COMMENT "Removing generated build and local development artifacts"
)

add_subdirectory(lib)

# --------------------------------------------------------------------------------------------------
# Generated documentation.
#
# llms.txt and llms-full.txt are the machine-readable index that agents and documentation crawlers
# read. `docs-generate` is the single target the Docs workflow builds; the pages a tool produces
# join it with the tool.
set(LLVMDSDL_LLMS_INDEX_SCRIPT "${LLVMDSDL_SOURCE_DIR}/tools/docs/build_llms_index.py")

if(LLVMDSDL_PYTHON3_EXE)
  add_custom_target(docs-llms
    COMMAND
      ${LLVMDSDL_PYTHON3_EXE} "${LLVMDSDL_LLMS_INDEX_SCRIPT}"
        --mkdocs-yml "${LLVMDSDL_SOURCE_DIR}/mkdocs.yml"
        --docs-dir "${LLVMDSDL_SOURCE_DIR}/docs"
    DEPENDS "${LLVMDSDL_LLMS_INDEX_SCRIPT}"
    USES_TERMINAL
    VERBATIM
    COMMENT "Docs: regenerating llms.txt and llms-full.txt"
  )
else()
  add_custom_target(docs-llms
    COMMAND ${CMAKE_COMMAND} -E echo "python3 not found; cannot run docs-llms"
    COMMAND ${CMAKE_COMMAND} -E false
    USES_TERMINAL
    VERBATIM
    COMMENT "docs-llms requires python3"
  )
endif()

add_custom_target(docs-generate)
add_dependencies(docs-generate docs-llms)

set(LLVMDSDL_INSTALLABLE_LIBRARIES
    llvmdsdlSupport
    llvmdsdlFrontend
    llvmdsdlSemantics)

set(LLVMDSDL_INSTALL_COMPONENT_DEV "dev")

install(
  TARGETS ${LLVMDSDL_INSTALLABLE_LIBRARIES}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  COMPONENT ${LLVMDSDL_INSTALL_COMPONENT_DEV}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  COMPONENT ${LLVMDSDL_INSTALL_COMPONENT_DEV})

install(
  DIRECTORY ${LLVMDSDL_SOURCE_DIR}/include/llvmdsdl
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  COMPONENT ${LLVMDSDL_INSTALL_COMPONENT_DEV}
  FILES_MATCHING
    PATTERN "*.h")

install(
  FILES ${LLVMDSDL_BINARY_DIR}/include/llvmdsdl/Version.h
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/llvmdsdl
  COMPONENT ${LLVMDSDL_INSTALL_COMPONENT_DEV})

# Licence terms travel with the libraries. Distribution policy (Debian in particular) requires every
# binary package to ship its licence under /usr/share/doc/<package>/.
install(
  FILES
    ${LLVMDSDL_SOURCE_DIR}/LICENSE.md
    ${LLVMDSDL_SOURCE_DIR}/THIRD_PARTY_NOTICES.md
    ${LLVMDSDL_SOURCE_DIR}/packaging/deb/copyright
  DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/doc/llvm-dsdl-dev
  COMPONENT ${LLVMDSDL_INSTALL_COMPONENT_DEV})

install(
  DIRECTORY ${LLVMDSDL_SOURCE_DIR}/LICENSES
  DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/doc/llvm-dsdl-dev
  COMPONENT ${LLVMDSDL_INSTALL_COMPONENT_DEV})

# Debian requires a compressed changelog per binary package. CPack has no
# changelog support of any kind, so it is compressed and installed here.
set(LLVMDSDL_DEB_CHANGELOG_SRC "${LLVMDSDL_SOURCE_DIR}/packaging/deb/changelog")

# The changelog's top entry carries its own copy of the version, and a package
# whose changelog disagrees with its control file is malformed. Catch the drift
# at configure time rather than in a lintian run after the release is cut.
file(STRINGS "${LLVMDSDL_DEB_CHANGELOG_SRC}" LLVMDSDL_DEB_CHANGELOG_FIRST_LINE LIMIT_COUNT 1)
if(NOT LLVMDSDL_DEB_CHANGELOG_FIRST_LINE MATCHES "^llvm-dsdl \\(([^)]+)\\)")
  message(FATAL_ERROR
    "Malformed first line in ${LLVMDSDL_DEB_CHANGELOG_SRC}; expected 'llvm-dsdl (<version>) ...'")
endif()
if(NOT CMAKE_MATCH_1 STREQUAL LLVMDSDL_TOOL_VERSION)
  message(FATAL_ERROR
    "Debian changelog version '${CMAKE_MATCH_1}' does not match VERSION '${LLVMDSDL_TOOL_VERSION}'. "
    "Add a new top entry to ${LLVMDSDL_DEB_CHANGELOG_SRC} when bumping VERSION.")
endif()

find_program(LLVMDSDL_GZIP_EXE gzip)
if(LLVMDSDL_GZIP_EXE)
  set(LLVMDSDL_DEB_CHANGELOG_GZ "${LLVMDSDL_BINARY_DIR}/packaging/changelog.gz")
  # -n omits the source name and mtime from the gzip header, so the compressed
  # changelog is byte-identical across builds of the same source.
  add_custom_command(
    OUTPUT "${LLVMDSDL_DEB_CHANGELOG_GZ}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${LLVMDSDL_BINARY_DIR}/packaging"
    COMMAND ${CMAKE_COMMAND} -E copy
            "${LLVMDSDL_DEB_CHANGELOG_SRC}" "${LLVMDSDL_BINARY_DIR}/packaging/changelog"
    COMMAND "${LLVMDSDL_GZIP_EXE}" -9 -n -f "${LLVMDSDL_BINARY_DIR}/packaging/changelog"
    DEPENDS "${LLVMDSDL_DEB_CHANGELOG_SRC}"
    COMMENT "Compressing Debian changelog"
    VERBATIM)
  add_custom_target(deb-changelog ALL DEPENDS "${LLVMDSDL_DEB_CHANGELOG_GZ}")

  install(
    FILES ${LLVMDSDL_DEB_CHANGELOG_GZ}
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/doc/llvm-dsdl-dev
    COMPONENT ${LLVMDSDL_INSTALL_COMPONENT_DEV})
else()
  message(WARNING
    "gzip not found; the Debian changelog will be omitted and .deb packages "
    "produced from this build will not satisfy Debian policy.")
endif()

set(LLVMDSDL_INSTALL_CONFIG_ARGS)
if(CMAKE_CONFIGURATION_TYPES)
  list(APPEND LLVMDSDL_INSTALL_CONFIG_ARGS --config $<CONFIG>)
elseif(CMAKE_BUILD_TYPE)
  list(APPEND LLVMDSDL_INSTALL_CONFIG_ARGS --config ${CMAKE_BUILD_TYPE})
endif()

set(LLVMDSDL_INSTALL_DEV_DEPENDS ${LLVMDSDL_INSTALLABLE_LIBRARIES})
if(TARGET deb-changelog)
  list(APPEND LLVMDSDL_INSTALL_DEV_DEPENDS deb-changelog)
endif()

add_custom_target(install-dev
  COMMAND
    ${CMAKE_COMMAND}
      --install ${LLVMDSDL_BINARY_DIR}
      ${LLVMDSDL_INSTALL_CONFIG_ARGS}
      --component ${LLVMDSDL_INSTALL_COMPONENT_DEV}
  DEPENDS ${LLVMDSDL_INSTALL_DEV_DEPENDS}
  USES_TERMINAL
  VERBATIM
  COMMENT "Installing llvm-dsdl development files"
)

enable_testing()
add_subdirectory(test)

# Hashed-container iteration in the generated-output path.
#
# A std::unordered_* iteration order is a property of the standard library, not
# of the data, so a loop over one can emit different bytes when built against a
# different stdlib while being deterministic for any single binary. This excludes
# the class at the source: it reads the sources, so it runs without a tool.
if(LLVMDSDL_PYTHON3_EXE)
  add_test(
    NAME llvmdsdl-determinism-unordered-iteration
    COMMAND "${LLVMDSDL_PYTHON3_EXE}"
            "${LLVMDSDL_SOURCE_DIR}/tools/determinism/check_unordered_iteration.py"
            --root "${LLVMDSDL_SOURCE_DIR}")
  set_tests_properties(llvmdsdl-determinism-unordered-iteration PROPERTIES
    LABELS "t0;determinism")

  # CI fails a job in which a test did not run, reading the JUnit report ctest writes; this holds the
  # check to reports in ctest's shape.
  add_test(
    NAME llvmdsdl-test-skips-selftest
    COMMAND "${LLVMDSDL_PYTHON3_EXE}"
            "${LLVMDSDL_SOURCE_DIR}/tools/check_test_skips.py"
            --self-test)
  set_tests_properties(llvmdsdl-test-skips-selftest PROPERTIES LABELS "t0")
endif()

if(LLVMDSDL_ENABLE_LLVM_COVERAGE)
  separate_arguments(LLVMDSDL_COVERAGE_CTEST_ARGS_LIST
                     NATIVE_COMMAND
                     "${LLVMDSDL_COVERAGE_CTEST_ARGS}")

  set(LLVMDSDL_COVERAGE_CONFIG_SUBDIR "")
  set(LLVMDSDL_COVERAGE_CTEST_CONFIG_ARGS "")
  if(CMAKE_CONFIGURATION_TYPES)
    set(LLVMDSDL_COVERAGE_CONFIG_SUBDIR "/$<CONFIG>")
    list(APPEND LLVMDSDL_COVERAGE_CTEST_CONFIG_ARGS --build-config $<CONFIG>)
  elseif(CMAKE_BUILD_TYPE)
    set(LLVMDSDL_COVERAGE_CONFIG_SUBDIR "/${CMAKE_BUILD_TYPE}")
  endif()

  set(LLVMDSDL_COVERAGE_DIR
      "${LLVMDSDL_BINARY_DIR}/coverage${LLVMDSDL_COVERAGE_CONFIG_SUBDIR}")
  set(LLVMDSDL_COVERAGE_PROFILES_DIR "${LLVMDSDL_COVERAGE_DIR}/profiles")
  set(LLVMDSDL_COVERAGE_PROFILE_PATTERN
      "${LLVMDSDL_COVERAGE_PROFILES_DIR}/default-%p-%m.profraw")
  set(LLVMDSDL_COVERAGE_PROFILE_DATA
      "${LLVMDSDL_COVERAGE_DIR}/coverage.profdata")
  set(LLVMDSDL_COVERAGE_SUMMARY
      "${LLVMDSDL_COVERAGE_DIR}/summary.txt")
  set(LLVMDSDL_COVERAGE_LCOV
      "${LLVMDSDL_COVERAGE_DIR}/coverage.lcov")
  set(LLVMDSDL_COVERAGE_HTML_DIR
      "${LLVMDSDL_COVERAGE_DIR}/html")

  add_custom_target(coverage-clean
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${LLVMDSDL_COVERAGE_DIR}"
    USES_TERMINAL
    VERBATIM
    COMMENT "Removing coverage artifacts from ${LLVMDSDL_COVERAGE_DIR}"
  )

  # Everything the instrumented suite executes, discovered rather than remembered: walking test/
  # for executables ties the list to the build system, so a new test binary is picked up by being
  # defined.
  function(llvmdsdl_collect_executables dir out_var)
    set(collected "")
    get_property(dir_targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(candidate IN LISTS dir_targets)
      get_target_property(candidate_type ${candidate} TYPE)
      if(candidate_type STREQUAL "EXECUTABLE")
        list(APPEND collected ${candidate})
      endif()
    endforeach()
    get_property(subdirs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
    foreach(subdir IN LISTS subdirs)
      llvmdsdl_collect_executables("${subdir}" subdir_targets)
      list(APPEND collected ${subdir_targets})
    endforeach()
    set(${out_var} "${collected}" PARENT_SCOPE)
  endfunction()

  llvmdsdl_collect_executables("${LLVMDSDL_SOURCE_DIR}/test" LLVMDSDL_COVERAGE_TEST_EXES)

  set(LLVMDSDL_COVERAGE_OBJECTS "")
  foreach(exe IN LISTS LLVMDSDL_COVERAGE_TEST_EXES)
    list(APPEND LLVMDSDL_COVERAGE_OBJECTS "$<TARGET_FILE:${exe}>")
  endforeach()

  # The one name CI builds before the (serialised) report run, so the workflow keeps no
  # target list of its own either.
  add_custom_target(coverage-prerequisites)
  add_dependencies(coverage-prerequisites ${LLVMDSDL_COVERAGE_TEST_EXES})

  add_custom_target(coverage-run
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${LLVMDSDL_COVERAGE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${LLVMDSDL_COVERAGE_PROFILES_DIR}"
    COMMAND
      ${CMAKE_COMMAND} -E env
        LLVM_PROFILE_FILE=${LLVMDSDL_COVERAGE_PROFILE_PATTERN}
        ${CMAKE_CTEST_COMMAND}
        ${LLVMDSDL_COVERAGE_CTEST_ARGS_LIST}
        ${LLVMDSDL_COVERAGE_CTEST_CONFIG_ARGS}
    WORKING_DIRECTORY ${LLVMDSDL_BINARY_DIR}
    DEPENDS coverage-prerequisites
    USES_TERMINAL
    VERBATIM
    COMMENT "Running tests with LLVM source coverage instrumentation"
  )

  add_custom_target(coverage-report
    COMMAND
      ${CMAKE_COMMAND}
        -DLLVM_PROFDATA=${LLVMDSDL_LLVM_PROFDATA_EXE}
        -DLLVM_COV=${LLVMDSDL_LLVM_COV_EXE}
        -DPROFILE_DIR=${LLVMDSDL_COVERAGE_PROFILES_DIR}
        -DPROFILE_DATA=${LLVMDSDL_COVERAGE_PROFILE_DATA}
        -DREPORT_TXT=${LLVMDSDL_COVERAGE_SUMMARY}
        -DREPORT_LCOV=${LLVMDSDL_COVERAGE_LCOV}
        -DREPORT_HTML_DIR=${LLVMDSDL_COVERAGE_HTML_DIR}
        -DSOURCE_DIR=${LLVMDSDL_SOURCE_DIR}
        -DBINARY_DIR=${LLVMDSDL_BINARY_DIR}
        "-DOBJECTS=${LLVMDSDL_COVERAGE_OBJECTS}"
        "-DIGNORE_FILENAME_REGEX=.*/(build|submodules|\\.venv)/.*"
        -P ${LLVMDSDL_SOURCE_DIR}/cmake/GenerateCoverageReport.cmake
    DEPENDS coverage-run
    USES_TERMINAL
    VERBATIM
    COMMENT "Generating LLVM source coverage reports (text, lcov, html)"
  )

  add_custom_target(coverage
    DEPENDS coverage-report
  )
endif()
