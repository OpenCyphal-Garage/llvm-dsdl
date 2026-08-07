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

# LLVM major-version lock. The C backend is routed through MLIR/EmitC, whose printed output can vary
# across MLIR majors (which are released with LLVM), so the linked major is a semantic input to the
# byte-reproducible generated code -- see docs/reference/guarantees/supply-chain.md. Pinning one major and testing across
# many are competing strategies; this project takes the pin. It was previously enforced only in CI;
# asserting it at configure time makes the lock authoritative everywhere, so an accidental build against
# another major fails loudly instead of silently producing divergent EmitC.
#
# Currently LLVM 22 (23 is not yet stable). Bumping is a deliberate one-line change here plus updating
# the CI pins together (both determinism hosts must move at once). A maintainer evaluating another major
# can opt out with -DLLVMDSDL_ALLOW_LLVM_MAJOR_MISMATCH=ON; MLIR ships with LLVM at the same version, so
# asserting the LLVM major covers MLIR too.
set(LLVMDSDL_REQUIRED_LLVM_MAJOR "22" CACHE STRING
    "LLVM/MLIR major version this project is locked to (see docs/reference/guarantees/supply-chain.md).")
option(LLVMDSDL_ALLOW_LLVM_MAJOR_MISMATCH
       "Permit configuring against an LLVM major other than LLVMDSDL_REQUIRED_LLVM_MAJOR." OFF)
if(NOT LLVM_VERSION_MAJOR STREQUAL LLVMDSDL_REQUIRED_LLVM_MAJOR)
  if(LLVMDSDL_ALLOW_LLVM_MAJOR_MISMATCH)
    message(WARNING
      "LLVM major ${LLVM_VERSION_MAJOR} != locked major ${LLVMDSDL_REQUIRED_LLVM_MAJOR}; "
      "byte-reproducibility of the C/EmitC backend is not guaranteed against this toolchain.")
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

set(LLVMDSDL_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(LLVMDSDL_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR})

find_program(LLVMDSDL_CLANG_FORMAT_EXE
  NAMES clang-format clang-format-21 clang-format-20 clang-format-19
        clang-format-18 clang-format-17 clang-format-16 clang-format-15
)

find_program(LLVMDSDL_CLANG_TIDY_EXE
  NAMES clang-tidy clang-tidy-21 clang-tidy-20 clang-tidy-19
        clang-tidy-18 clang-tidy-17 clang-tidy-16 clang-tidy-15
)

find_program(LLVMDSDL_IWYU_EXE
  NAMES include-what-you-use iwyu include-what-you-use-21
        include-what-you-use-20 include-what-you-use-19
        include-what-you-use-18 include-what-you-use-17
        include-what-you-use-16 include-what-you-use-15
)

find_program(LLVMDSDL_PYTHON3_EXE
  NAMES python3 python
)

option(LLVMDSDL_ENABLE_LLVM_COVERAGE
       "Enable LLVM source-based coverage instrumentation and reporting targets"
       OFF)

option(LLVMDSDL_ENABLE_PYTHON_ACCELERATOR
       "Build optional CPython runtime accelerator module for generated Python code"
       OFF)

option(LLVMDSDL_DISTCLEAN_REMOVE_VENV
       "Remove repository .venv when running distclean"
       OFF)

# Fully static tools.
#
# Intended for the musl toolchain, where it produces static-pie executables with
# no loader dependency at all -- which is what makes the .deb carry no Depends
# rather than the two it carries when linked against glibc.
#
# Deliberately not the default: against glibc this links but is a trap, because
# NSS and dlopen need the shared libc at runtime. dsdlc and dsdl-opt use neither,
# but dsdld loads lint plugins with dlopen, and musl's static dlopen always fails
# with "Dynamic loading not supported". That is a real functional loss, reported
# accurately rather than silently -- see D6 in
# docs/development/distribution-channels.md.
option(LLVMDSDL_STATIC_BINARIES
       "Link the tools fully statically. Linux only; intended for the musl toolchain."
       OFF)

if(LLVMDSDL_STATIC_BINARIES)
  if(APPLE)
    # Apple does not ship a static libSystem and does not support linking one;
    # there is no supported static crt0. The macOS equivalent of this flag is
    # LLVMDSDL_VENDOR_LLVM, which bundles instead.
    message(FATAL_ERROR
      "LLVMDSDL_STATIC_BINARIES is not available on macOS: Apple does not support "
      "statically linking libSystem. Use LLVMDSDL_VENDOR_LLVM for a self-contained "
      "macOS bundle instead.")
  endif()
  if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "LLVMDSDL_STATIC_BINARIES is implemented for Linux only.")
  endif()
  if(LLVMDSDL_ENABLE_PYTHON_ACCELERATOR)
    # The accelerator is a MODULE library that CPython dlopens. These options
    # apply to every target in the directory, and a statically linked loadable
    # module is a contradiction.
    message(FATAL_ERROR
      "LLVMDSDL_STATIC_BINARIES and LLVMDSDL_ENABLE_PYTHON_ACCELERATOR are mutually "
      "exclusive: the accelerator is a loadable module and cannot be linked static.")
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

# -LE bench: the benchmarks assert wall-clock thresholds, which say nothing about
# coverage and cannot hold under instrumentation anyway -- a coverage build is
# several times slower by construction, so including them would fail the lane for
# doing exactly what it was asked to do.
#
# showroom-recipes for a related reason: they measure whether the *published build
# recipes* still work, and spend their time inside make, cargo, npm, go and bazel.
# The instrumented binaries here are dsdlc, dsdl-opt and the unit tests, so a recipe
# adds minutes and network flakiness to the lane and not one line of covered C++.
set(LLVMDSDL_COVERAGE_CTEST_ARGS
    "--output-on-failure -LE bench|showroom-recipes"
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
    NAMES llvm-profdata llvm-profdata-21 llvm-profdata-20 llvm-profdata-19
          llvm-profdata-18 llvm-profdata-17 llvm-profdata-16
    HINTS ${LLVM_TOOLS_BINARY_DIR} /opt/homebrew/opt/llvm/bin
  )

  find_program(LLVMDSDL_LLVM_COV_EXE
    NAMES llvm-cov llvm-cov-21 llvm-cov-20 llvm-cov-19
          llvm-cov-18 llvm-cov-17 llvm-cov-16
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
# Instruments the host tools (dsdlc/dsdl-opt/dsdld) AND the in-process frontend
# fuzzers (Lexer/Parser/LSP) with ASan/UBSan. Generated *native decoders* are
# compiled by the integration harness scripts in their own execute_process
# invocations, so they do NOT inherit these flags; those scripts are sanitized
# independently via LLVMDSDL_NATIVE_HARNESS_SANITIZE (forwarded below). Use the
# Debug configuration for sanitizer builds so NDEBUG is absent and the runtime
# copy_bits assert() guards still fire alongside ASan.
set(LLVMDSDL_ENABLE_SANITIZERS
    ""
    CACHE STRING
    "Comma-separated -fsanitize list applied to the host tools and unit fuzzers \
(e.g. \"address,undefined\"). Empty = off. Requires Clang/AppleClang.")

# Sanitizer flavor forwarded to the native-decoder integration harnesses (the
# generated C/C++/Go parity + roundtrip + libFuzzer tests). Kept separate from
# the host-tool switch because those harnesses compile untrusted-input decoders
# and are the primary ASan/libFuzzer target; the host tools are a bonus. When
# LLVMDSDL_ENABLE_SANITIZERS is set and this is left at "auto", it defaults to
# the same list so a single knob turns everything on.
set(LLVMDSDL_NATIVE_HARNESS_SANITIZE
    "auto"
    CACHE STRING
    "Sanitizer list for generated native-decoder harnesses (address,undefined). \
\"auto\" tracks LLVMDSDL_ENABLE_SANITIZERS; \"\" disables.")

if(LLVMDSDL_NATIVE_HARNESS_SANITIZE STREQUAL "auto")
  set(LLVMDSDL_NATIVE_HARNESS_SANITIZE_EFFECTIVE "${LLVMDSDL_ENABLE_SANITIZERS}")
else()
  set(LLVMDSDL_NATIVE_HARNESS_SANITIZE_EFFECTIVE "${LLVMDSDL_NATIVE_HARNESS_SANITIZE}")
endif()

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
  message(STATUS "Sanitizers enabled for host tools/fuzzers: ${LLVMDSDL_ENABLE_SANITIZERS}")
endif()

if(LLVMDSDL_NATIVE_HARNESS_SANITIZE_EFFECTIVE)
  message(STATUS
    "Native-decoder harnesses sanitized with: ${LLVMDSDL_NATIVE_HARNESS_SANITIZE_EFFECTIVE}")
endif()

# Coverage-guided libFuzzer lane over the generated native (C/C++) deserializers
# on the real UAVCAN corpus. Requires a Clang that ships compiler-rt's
# -fsanitize=fuzzer; the harness registration probes for it and skips loudly if
# absent so a toolchain without libFuzzer does not silently drop the lane.
option(LLVMDSDL_ENABLE_NATIVE_DECODER_FUZZ
       "Register the coverage-guided libFuzzer lane over generated native decoders"
       OFF)

# Bounded iteration count for the native-decoder fuzz lane's ctest entry. CI sets
# a small number on PRs and a large number on the weekly deep run; interactive
# fuzzing overrides -runs directly on the built binary.
set(LLVMDSDL_NATIVE_DECODER_FUZZ_RUNS
    "20000"
    CACHE STRING
    "libFuzzer -runs= budget for the native-decoder fuzz ctest entry.")

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

if(NOT LLVMDSDL_IWYU_EXE)
  message(WARNING
    "include-what-you-use not found; check-iwyu target will fail "
    "until it is installed.")
endif()

if(NOT LLVMDSDL_PYTHON3_EXE)
  message(WARNING
    "python3 not found; Python wheel staging targets will fail "
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

add_custom_target(check-iwyu
  COMMAND
    ${CMAKE_COMMAND}
      -DINCLUDE_WHAT_YOU_USE=${LLVMDSDL_IWYU_EXE}
      -DLLVMDSDL_SOURCE_DIR=${LLVMDSDL_SOURCE_DIR}
      -DLLVMDSDL_BINARY_DIR=${LLVMDSDL_BINARY_DIR}
      -P ${LLVMDSDL_SOURCE_DIR}/cmake/CheckIncludeWhatYouUse.cmake
  USES_TERMINAL
  VERBATIM
  COMMENT "Checking source includes with include-what-you-use"
)

add_custom_target(check-include-what-you-use
  DEPENDS check-iwyu
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

# Optional behavioral gating input: a JUnit results file from an executed ctest run.
# When set (e.g. by CI after running the suite with --output-junit), the
# parity/malformed/determinism report gates enforce BEHAVIORAL coverage — a matrix cell
# is covered only if a matching test executed and passed. Empty (default) keeps them as
# structural test-name-presence lints so local doc regeneration needs no prior suite run.
set(LLVMDSDL_REPORT_GATE_JUNIT "" CACHE FILEPATH
  "JUnit XML from an executed ctest run; enables behavioral parity/malformed/determinism gating.")

# Where the four report generators put their markdown: straight into docs/, because that markdown
# *is* the published page (Reference > Guarantees, plus the consistency lint under Development).
# Generating it is what keeps the numbers on the site the numbers the gates just computed; a
# committed copy could only ever be a snapshot of some earlier run. Like docs/showroom, these pages
# are gitignored and produced at publish time -- see the Docs workflow.
#
# The JSON stays in the build tree. It is the machine-readable form the gates and the CI artifact
# collector consume.
set(LLVMDSDL_DOCS_GUARANTEES_DIR "${LLVMDSDL_SOURCE_DIR}/docs/reference/guarantees")
set(LLVMDSDL_DOCS_DEVELOPMENT_DIR "${LLVMDSDL_SOURCE_DIR}/docs/development")

if(LLVMDSDL_PYTHON3_EXE)
  add_custom_target(convergence-report
    COMMAND
      ${CMAKE_COMMAND}
        -DPYTHON_EXECUTABLE=${LLVMDSDL_PYTHON3_EXE}
        -DREPO_ROOT=${LLVMDSDL_SOURCE_DIR}
        -DOUTPUT_JSON=${LLVMDSDL_BINARY_DIR}/convergence/$<CONFIG>/report.json
        -DOUTPUT_MD=${LLVMDSDL_DOCS_DEVELOPMENT_DIR}/convergence-scorecard.md
        -DBASELINE_JSON=${LLVMDSDL_SOURCE_DIR}/tools/convergence/convergence_baseline.json
        -DCONVERGENCE_SCRIPT=${LLVMDSDL_SOURCE_DIR}/tools/convergence/convergence_report.py
        -P ${LLVMDSDL_SOURCE_DIR}/cmake/RunConvergenceReport.cmake
    USES_TERMINAL
    VERBATIM
    COMMENT "Generating convergence score report and scorecard"
  )
else()
  add_custom_target(convergence-report
    COMMAND ${CMAKE_COMMAND} -E echo "python3 not found; cannot run convergence-report"
    COMMAND ${CMAKE_COMMAND} -E false
    USES_TERMINAL
    VERBATIM
    COMMENT "convergence-report requires python3"
  )
endif()

if(LLVMDSDL_PYTHON3_EXE)
  add_custom_target(parity-matrix-report
    COMMAND
      ${CMAKE_COMMAND}
        -DPYTHON_EXECUTABLE=${LLVMDSDL_PYTHON3_EXE}
        -DREPO_ROOT=${LLVMDSDL_SOURCE_DIR}
        -DCTEST_TEST_DIR=${LLVMDSDL_BINARY_DIR}
        -DCTEST_CONFIG=$<CONFIG>
        -DCTEST_JUNIT=${LLVMDSDL_REPORT_GATE_JUNIT}
        -DOUTPUT_JSON=${LLVMDSDL_BINARY_DIR}/convergence/$<CONFIG>/parity-matrix-report.json
        -DOUTPUT_MD=${LLVMDSDL_DOCS_GUARANTEES_DIR}/parity-matrix.md
        -DPARITY_BASELINE_JSON=${LLVMDSDL_SOURCE_DIR}/tools/convergence/parity_matrix_baseline.json
        -DPARITY_MATRIX_SCRIPT=${LLVMDSDL_SOURCE_DIR}/tools/convergence/parity_matrix_report.py
        -P ${LLVMDSDL_SOURCE_DIR}/cmake/RunParityMatrixReport.cmake
    USES_TERMINAL
    VERBATIM
    COMMENT "Generating parity matrix coverage report"
  )
else()
  add_custom_target(parity-matrix-report
    COMMAND ${CMAKE_COMMAND} -E echo "python3 not found; cannot run parity-matrix-report"
    COMMAND ${CMAKE_COMMAND} -E false
    USES_TERMINAL
    VERBATIM
    COMMENT "parity-matrix-report requires python3"
  )
endif()

if(LLVMDSDL_PYTHON3_EXE)
  add_custom_target(malformed-contract-report
    COMMAND
      ${CMAKE_COMMAND}
        -DPYTHON_EXECUTABLE=${LLVMDSDL_PYTHON3_EXE}
        -DREPO_ROOT=${LLVMDSDL_SOURCE_DIR}
        -DCTEST_TEST_DIR=${LLVMDSDL_BINARY_DIR}
        -DCTEST_CONFIG=$<CONFIG>
        -DCTEST_JUNIT=${LLVMDSDL_REPORT_GATE_JUNIT}
        -DOUTPUT_JSON=${LLVMDSDL_BINARY_DIR}/convergence/$<CONFIG>/malformed-contract-matrix-report.json
        -DOUTPUT_MD=${LLVMDSDL_DOCS_GUARANTEES_DIR}/malformed-input.md
        -DMALFORMED_CONTRACT_BASELINE_JSON=${LLVMDSDL_SOURCE_DIR}/tools/convergence/malformed_contract_matrix_baseline.json
        -DMALFORMED_CONTRACT_SCRIPT=${LLVMDSDL_SOURCE_DIR}/tools/convergence/malformed_contract_matrix_report.py
        -P ${LLVMDSDL_SOURCE_DIR}/cmake/RunMalformedContractMatrixReport.cmake
    USES_TERMINAL
    VERBATIM
    COMMENT "Generating malformed-input contract matrix report"
  )
else()
  add_custom_target(malformed-contract-report
    COMMAND ${CMAKE_COMMAND} -E echo "python3 not found; cannot run malformed-contract-report"
    COMMAND ${CMAKE_COMMAND} -E false
    USES_TERMINAL
    VERBATIM
    COMMENT "malformed-contract-report requires python3"
  )
endif()

if(LLVMDSDL_PYTHON3_EXE)
  add_custom_target(determinism-matrix-report
    COMMAND
      ${CMAKE_COMMAND}
        -DPYTHON_EXECUTABLE=${LLVMDSDL_PYTHON3_EXE}
        -DREPO_ROOT=${LLVMDSDL_SOURCE_DIR}
        -DCTEST_TEST_DIR=${LLVMDSDL_BINARY_DIR}
        -DCTEST_CONFIG=$<CONFIG>
        -DCTEST_JUNIT=${LLVMDSDL_REPORT_GATE_JUNIT}
        -DOUTPUT_JSON=${LLVMDSDL_BINARY_DIR}/convergence/$<CONFIG>/determinism-matrix-report.json
        -DOUTPUT_MD=${LLVMDSDL_DOCS_GUARANTEES_DIR}/determinism.md
        -DDETERMINISM_BASELINE_JSON=${LLVMDSDL_SOURCE_DIR}/tools/convergence/determinism_matrix_baseline.json
        -DDETERMINISM_MATRIX_SCRIPT=${LLVMDSDL_SOURCE_DIR}/tools/convergence/determinism_matrix_report.py
        -P ${LLVMDSDL_SOURCE_DIR}/cmake/RunDeterminismMatrixReport.cmake
    USES_TERMINAL
    VERBATIM
    COMMENT "Generating determinism matrix coverage report"
  )
else()
  add_custom_target(determinism-matrix-report
    COMMAND ${CMAKE_COMMAND} -E echo "python3 not found; cannot run determinism-matrix-report"
    COMMAND ${CMAKE_COMMAND} -E false
    USES_TERMINAL
    VERBATIM
    COMMENT "determinism-matrix-report requires python3"
  )
endif()

add_custom_target(release-blocking-report-gates
  DEPENDS convergence-report parity-matrix-report malformed-contract-report determinism-matrix-report
)

add_subdirectory(include/llvmdsdl)
add_subdirectory(lib)
add_subdirectory(tools)
add_subdirectory(examples/cyphal_ping_pong)
add_subdirectory(examples/cyphal_yakut_register_demo)
# examples/showroom is added below enable_testing(): it registers ctests for its build recipes, and
# add_test() in a directory processed before enable_testing() is not reliably picked up.

# --------------------------------------------------------------------------------------------------
# Generated documentation.
#
# Three kinds of page under docs/ are produced rather than written: the showroom (the compiler's own
# output), the guarantee matrices (the report generators' output), and the machine-readable index
# that agents and documentation crawlers read. `docs-generate` produces all of them, in that order,
# and is the single target the Docs workflow builds.
#
# Ordering is load-bearing for the last one: llms.txt lists every published page, so it has to run
# after the pages that are generated exist. It also fails if a page under docs/ is in neither the
# mkdocs nav nor its exclusion list, which is the same coverage rule mkdocs' own `not_in_nav` applies
# -- enforced here so a missing page is caught by a build that does not need mkdocs installed.
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

# showroom-docs joins this chain from examples/showroom/CMakeLists.txt, which is processed after
# this point -- see the note beside the example subdirectories above.
add_dependencies(docs-llms release-blocking-report-gates)

add_custom_target(docs-generate)
add_dependencies(docs-generate docs-llms)

set(LLVMDSDL_INSTALLABLE_TOOLS
    dsdlc
    dsdl-opt
    dsdld)

set(LLVMDSDL_INSTALLABLE_LIBRARIES
    llvmdsdlSupport
    llvmdsdlFrontend
    llvmdsdlSemantics
    llvmdsdlIR
    llvmdsdlLowering
    llvmdsdlTransforms
    llvmdsdlCodeGen
    llvmdsdlLSP)

set(LLVMDSDL_INSTALL_COMPONENT_BIN "bin")
set(LLVMDSDL_INSTALL_COMPONENT_DEV "dev")
set(LLVMDSDL_SELF_CONTAINED_TOOL_BUNDLE_DEFAULT OFF)
if(APPLE OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(LLVMDSDL_SELF_CONTAINED_TOOL_BUNDLE_DEFAULT ON)
endif()

option(LLVMDSDL_ENABLE_SELF_CONTAINED_TOOL_BUNDLE
       "Enable self-contained tool bundle target (copies the tools with runtime deps)"
       ${LLVMDSDL_SELF_CONTAINED_TOOL_BUNDLE_DEFAULT})

set(LLVMDSDL_SELF_CONTAINED_TOOLS_DIR "${LLVMDSDL_BINARY_DIR}/self-contained-tools")


# Vendoring libLLVM into the package.
#
# No Ubuntu release carries LLVM 22, so a downloadable .deb that merely *depended*
# on it would install against stock Ubuntu and then die on first run with a missing
# shared object -- there is no apt repository of ours to resolve it from. Shipping
# the library inside the package is what makes `apt install ./llvm-dsdl_*.deb` work
# on a machine that has never heard of apt.llvm.org.
#
# The same reasoning applies to the macOS tarball: a bare download has no
# dependency resolver either, and a tree whose binaries reference
# /opt/homebrew/opt/llvm only runs on a machine that already has Homebrew LLVM.
#
# Off by default: a developer's `cmake --install` should not copy a ~150 MB library
# into the install tree. The release build turns it on explicitly.
#
# LLVM is Apache-2.0-WITH-LLVM-exception, so redistribution is fine with
# attribution -- packaging/deb/copyright carries the licence paragraph, and its
# Files: stanza must be activated when this is enabled for a published package.
option(LLVMDSDL_VENDOR_LLVM
       "Ship libLLVM inside the install tree so the package runs without a system LLVM."
       OFF)

# Linux and macOS reach the same outcome by different mechanisms: ELF RPATH set
# on the installed binaries, versus Mach-O install names rewritten by the bundle
# target. The flag means "self-contained" on both; only the plumbing differs.
set(LLVMDSDL_VENDOR_LLVM_VIA_BUNDLE OFF)
if(LLVMDSDL_VENDOR_LLVM AND APPLE)
  set(LLVMDSDL_VENDOR_LLVM_VIA_BUNDLE ON)
endif()

if(LLVMDSDL_VENDOR_LLVM AND NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT APPLE)
  message(FATAL_ERROR
    "LLVMDSDL_VENDOR_LLVM is implemented for Linux (ELF RPATH) and macOS "
    "(Mach-O install names) only.")
endif()

if(LLVMDSDL_VENDOR_LLVM AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
  # A private directory rather than ${CMAKE_INSTALL_LIBDIR}: this is our copy of
  # someone else's library, and it must never satisfy anything but our own tools.
  set(LLVMDSDL_VENDOR_LLVM_SUBDIR "${CMAKE_INSTALL_LIBDIR}/llvm-dsdl")

  find_library(LLVMDSDL_LLVM_SHARED_LIB
    NAMES LLVM LLVM-${LLVM_VERSION_MAJOR}
    HINTS ${LLVM_LIBRARY_DIRS}
    NO_DEFAULT_PATH)
  if(NOT LLVMDSDL_LLVM_SHARED_LIB)
    message(FATAL_ERROR
      "LLVMDSDL_VENDOR_LLVM is ON but no shared libLLVM was found in ${LLVM_LIBRARY_DIRS}. "
      "This LLVM install may be static-only, which vendoring cannot help with.")
  endif()

  # find_library resolves to the versioned SONAME target through any symlinks;
  # install that real file, since a dangling symlink in a package is worse than
  # no package. The loader finds it by SONAME, so no symlink is needed beside it.
  get_filename_component(LLVMDSDL_LLVM_SHARED_LIB_REAL "${LLVMDSDL_LLVM_SHARED_LIB}" REALPATH)
  get_filename_component(LLVMDSDL_LLVM_SHARED_LIB_NAME "${LLVMDSDL_LLVM_SHARED_LIB_REAL}" NAME)
  message(STATUS "Vendoring LLVM runtime: ${LLVMDSDL_LLVM_SHARED_LIB_REAL}")

  # Debug information in the vendored library is of no use to us: a backtrace
  # through LLVM internals is not something this project can act on, and anyone
  # debugging LLVM itself has the distro's own -dbgsym packages. This project's
  # own binaries stay unstripped, which is where post-mortems actually land --
  # see packaging/deb/lintian-overrides/llvm-dsdl.
  #
  # Against a distro libLLVM this saves nothing, because Debian and Ubuntu already
  # ship their libraries stripped (measured: byte-identical before and after, no
  # .debug sections present). It is kept for the case that motivates it -- an LLVM
  # we build ourselves, which arrives unstripped and carries a great deal of debug
  # information. See the cached-toolchain pipeline in docs/development/distribution-channels.md §5.
  option(LLVMDSDL_STRIP_VENDORED_LLVM
         "Strip the vendored libLLVM. Does not affect this project's own binaries."
         ON)

  set(LLVMDSDL_VENDORED_LLVM_SOURCE "${LLVMDSDL_LLVM_SHARED_LIB_REAL}")

  if(LLVMDSDL_STRIP_VENDORED_LLVM)
    find_program(LLVMDSDL_STRIP_EXE NAMES llvm-strip strip)
    if(NOT LLVMDSDL_STRIP_EXE)
      message(FATAL_ERROR
        "LLVMDSDL_STRIP_VENDORED_LLVM is ON but no strip was found. Install binutils, "
        "or reconfigure with -DLLVMDSDL_STRIP_VENDORED_LLVM=OFF to ship it unstripped.")
    endif()

    # Strip a copy rather than the system library: the source is root-owned and
    # shared with the rest of the machine, and mutating it would corrupt the
    # build host's own LLVM install.
    set(LLVMDSDL_VENDORED_LLVM_STRIPPED
        "${LLVMDSDL_BINARY_DIR}/vendor/${LLVMDSDL_LLVM_SHARED_LIB_NAME}")
    add_custom_command(
      OUTPUT "${LLVMDSDL_VENDORED_LLVM_STRIPPED}"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${LLVMDSDL_BINARY_DIR}/vendor"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${LLVMDSDL_LLVM_SHARED_LIB_REAL}" "${LLVMDSDL_VENDORED_LLVM_STRIPPED}"
      # --strip-unneeded, not --strip-all: a shared object must keep its dynamic
      # symbol table or nothing can resolve against it at load time.
      COMMAND "${LLVMDSDL_STRIP_EXE}" --strip-unneeded "${LLVMDSDL_VENDORED_LLVM_STRIPPED}"
      DEPENDS "${LLVMDSDL_LLVM_SHARED_LIB_REAL}"
      COMMENT "Stripping vendored ${LLVMDSDL_LLVM_SHARED_LIB_NAME}"
      VERBATIM)
    add_custom_target(vendored-llvm ALL DEPENDS "${LLVMDSDL_VENDORED_LLVM_STRIPPED}")
    set(LLVMDSDL_VENDORED_LLVM_SOURCE "${LLVMDSDL_VENDORED_LLVM_STRIPPED}")
  endif()

  install(
    FILES "${LLVMDSDL_VENDORED_LLVM_SOURCE}"
    DESTINATION "${LLVMDSDL_VENDOR_LLVM_SUBDIR}"
    COMPONENT ${LLVMDSDL_INSTALL_COMPONENT_BIN})

  # $ORIGIN is the directory of the running binary, so from /usr/bin this resolves
  # to /usr/lib/llvm-dsdl. Relative rather than absolute so the tree stays
  # relocatable, which the tarball and any future prefix change both depend on.
  set_target_properties(${LLVMDSDL_INSTALLABLE_TOOLS} PROPERTIES
    INSTALL_RPATH "$ORIGIN/../${LLVMDSDL_VENDOR_LLVM_SUBDIR}"
    # The build tree keeps pointing at the system LLVM; only the installed copies
    # get the private path, so running from the build directory is unaffected.
    BUILD_WITH_INSTALL_RPATH OFF)
endif()

if(LLVMDSDL_VENDOR_LLVM_VIA_BUNDLE)
  # macOS gets its self-contained tree from the bundle target, which rewrites
  # Mach-O install names in place. Installing the bundle rather than the raw
  # targets is the whole point: the raw binaries reference
  # /opt/homebrew/opt/llvm and only run where Homebrew LLVM is already present,
  # which is exactly the trap a downloadable tarball must not contain.
  #
  # The bundle's layout is bin/ + lib/ and its install names are
  # @executable_path/../lib, so it must land at the prefix root unchanged --
  # relocating lib/ elsewhere would break every reference in it.
  if(NOT LLVMDSDL_ENABLE_SELF_CONTAINED_TOOL_BUNDLE)
    message(FATAL_ERROR
      "LLVMDSDL_VENDOR_LLVM on macOS is implemented via the self-contained bundle, "
      "but LLVMDSDL_ENABLE_SELF_CONTAINED_TOOL_BUNDLE is OFF.")
  endif()

  install(
    DIRECTORY "${LLVMDSDL_SELF_CONTAINED_TOOLS_DIR}/bin/"
    DESTINATION "${CMAKE_INSTALL_BINDIR}"
    COMPONENT ${LLVMDSDL_INSTALL_COMPONENT_BIN}
    USE_SOURCE_PERMISSIONS)

  install(
    DIRECTORY "${LLVMDSDL_SELF_CONTAINED_TOOLS_DIR}/lib/"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    COMPONENT ${LLVMDSDL_INSTALL_COMPONENT_BIN}
    USE_SOURCE_PERMISSIONS)
else()
  install(
    TARGETS ${LLVMDSDL_INSTALLABLE_TOOLS}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    COMPONENT ${LLVMDSDL_INSTALL_COMPONENT_BIN})
endif()

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
    PATTERN "*.h"
    PATTERN "*.td")

set(LLVMDSDL_GENERATED_DIALECT_HEADERS
    ${LLVMDSDL_BINARY_DIR}/include/llvmdsdl/IR/DSDLOps.h.inc
    ${LLVMDSDL_BINARY_DIR}/include/llvmdsdl/IR/DSDLDialect.h.inc
    ${LLVMDSDL_BINARY_DIR}/include/llvmdsdl/IR/DSDLTypes.h.inc
    ${LLVMDSDL_BINARY_DIR}/include/llvmdsdl/IR/DSDLAttrs.h.inc)

install(
  FILES ${LLVMDSDL_GENERATED_DIALECT_HEADERS}
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/llvmdsdl/IR
  COMPONENT ${LLVMDSDL_INSTALL_COMPONENT_DEV})

install(
  FILES ${LLVMDSDL_BINARY_DIR}/include/llvmdsdl/Version.h
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/llvmdsdl
  COMPONENT ${LLVMDSDL_INSTALL_COMPONENT_DEV})

# --------------------------------------------------------------------------------------------------
# The downstream CMake package: find_package(llvm-dsdl) + dsdlc_generate().
#
# This is supported API, versioned with the tool, and the reason the c-cmake showroom recipe
# is three lines instead of thirty. See cmake/DSDLCGenerate.cmake for what the function does and
# cmake/llvm-dsdlConfig.cmake.in for why dsdlc is located relative to the config rather than through
# install(EXPORT).
#
# It ships in the BIN component, not DEV. The dev component is about linking against the compiler's
# own libraries; this is about running the compiler, which is what someone who installed only the
# tool wants to do from their build -- and DSDLC_EXECUTABLE resolves to a binary that would not be
# there if the two could be installed apart.
set(LLVMDSDL_CMAKE_CONFIG_SUBDIR "${CMAKE_INSTALL_LIBDIR}/cmake/llvm-dsdl")

# Walk back up from the config directory to the install prefix, so the installed tree stays
# relocatable. Computed rather than written out because CMAKE_INSTALL_LIBDIR is not always one
# component deep -- Debian multiarch makes it lib/x86_64-linux-gnu.
file(RELATIVE_PATH LLVMDSDL_CMAKE_CONFIG_TO_PREFIX
     "${CMAKE_CURRENT_BINARY_DIR}/${LLVMDSDL_CMAKE_CONFIG_SUBDIR}"
     "${CMAKE_CURRENT_BINARY_DIR}")

include(CMakePackageConfigHelpers)

configure_package_config_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/llvm-dsdlConfig.cmake.in"
  "${LLVMDSDL_BINARY_DIR}/cmake-package/llvm-dsdlConfig.cmake"
  INSTALL_DESTINATION "${LLVMDSDL_CMAKE_CONFIG_SUBDIR}")

# SameMinorVersion: generated output embeds the generator version, and the function's argument
# surface is part of what a caller pins. A patch release may be swapped in freely; a minor release
# may add a keyword the caller's build does not know about, so it has to be asked for.
write_basic_package_version_file(
  "${LLVMDSDL_BINARY_DIR}/cmake-package/llvm-dsdlConfigVersion.cmake"
  VERSION "${LLVMDSDL_TOOL_VERSION}"
  COMPATIBILITY SameMinorVersion)

install(
  FILES
    "${LLVMDSDL_BINARY_DIR}/cmake-package/llvm-dsdlConfig.cmake"
    "${LLVMDSDL_BINARY_DIR}/cmake-package/llvm-dsdlConfigVersion.cmake"
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/DSDLCGenerate.cmake"
  DESTINATION "${LLVMDSDL_CMAKE_CONFIG_SUBDIR}"
  COMPONENT ${LLVMDSDL_INSTALL_COMPONENT_BIN})

# The Bazel module, installed as sources rather than configured: a Bazel module is consumed by path
# or by registry, and nothing in it needs to know where it landed. Until it is published to a
# registry, a downstream build reaches it with
# `--override_module=rules_dsdl=<prefix>/share/rules_dsdl`.
#
# In the BIN component with the CMake package, for the same reason: this is about running the
# compiler from a build, not about linking against its libraries.
install(
  DIRECTORY ${LLVMDSDL_SOURCE_DIR}/bazel/rules_dsdl
  DESTINATION ${CMAKE_INSTALL_DATADIR}
  COMPONENT ${LLVMDSDL_INSTALL_COMPONENT_BIN})

# CycloneDX SBOM for the release artifacts: records the tool version and source revision, the locked
# LLVM/MLIR version actually linked in, and the pinned commits of the build/test submodules. Built with
# the default target so `cmake --install` always ships an SBOM that matches the binaries; the generator
# is deterministic (no wall-clock timestamp) so it does not perturb the reproducibility lanes.
# See docs/reference/guarantees/supply-chain.md.
set(LLVMDSDL_SBOM_FILE "${LLVMDSDL_BINARY_DIR}/llvm-dsdl-sbom.cdx.json")
if(DEFINED zstd_VERSION)
  set(LLVMDSDL_SBOM_ZSTD_ARG "-DZSTD_VERSION=${zstd_VERSION}")
else()
  set(LLVMDSDL_SBOM_ZSTD_ARG "-DZSTD_VERSION=")
endif()
add_custom_target(sbom ALL
  COMMAND ${CMAKE_COMMAND}
    -DOUTPUT_FILE=${LLVMDSDL_SBOM_FILE}
    -DTOOL_VERSION=${LLVMDSDL_TOOL_VERSION}
    -DLLVM_VERSION=${LLVM_PACKAGE_VERSION}
    -DSOURCE_ROOT=${LLVMDSDL_SOURCE_DIR}
    "-DTOOLS=${LLVMDSDL_INSTALLABLE_TOOLS}"
    ${LLVMDSDL_SBOM_ZSTD_ARG}
    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateSBOM.cmake
  BYPRODUCTS ${LLVMDSDL_SBOM_FILE}
  COMMENT "Generating CycloneDX SBOM"
  VERBATIM)

install(
  FILES ${LLVMDSDL_SBOM_FILE}
  DESTINATION ${CMAKE_INSTALL_DATADIR}/llvm-dsdl
  COMPONENT ${LLVMDSDL_INSTALL_COMPONENT_BIN})

# Licence terms travel with the binaries. Distribution policy (Debian in particular) requires every
# binary package to ship its licence under /usr/share/doc/<package>/, and an installed tree without
# them cannot be redistributed. LICENSES/ carries the Apache-2.0-with-LLVM-exception text, which
# release .debs need because they vendor libLLVM -- see docs/development/release-packaging.md §3.
install(
  FILES
    ${LLVMDSDL_SOURCE_DIR}/LICENSE.md
    ${LLVMDSDL_SOURCE_DIR}/THIRD_PARTY_NOTICES.md
  DESTINATION ${CMAKE_INSTALL_DOCDIR}
  COMPONENT ${LLVMDSDL_INSTALL_COMPONENT_BIN})

install(
  DIRECTORY ${LLVMDSDL_SOURCE_DIR}/LICENSES
  DESTINATION ${CMAKE_INSTALL_DOCDIR}
  COMPONENT ${LLVMDSDL_INSTALL_COMPONENT_BIN})

# Debian keys the copyright path off the *binary package* name, so each package
# needs its own copy -- llvm-dsdl and llvm-dsdl-dev are separate packages and
# lintian checks both. CMAKE_INSTALL_DOCDIR only covers the first: GNUInstallDirs
# derives it from PROJECT_NAME, so the -dev destination is spelled out.
install(
  FILES ${LLVMDSDL_SOURCE_DIR}/packaging/deb/copyright
  DESTINATION ${CMAKE_INSTALL_DOCDIR}
  COMPONENT ${LLVMDSDL_INSTALL_COMPONENT_BIN})

install(
  FILES ${LLVMDSDL_SOURCE_DIR}/packaging/deb/copyright
  DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/doc/llvm-dsdl-dev
  COMPONENT ${LLVMDSDL_INSTALL_COMPONENT_DEV})

# The tools ship unstripped on purpose; the override records that decision where
# lintian will read it, so the package is policy-clean by declaration rather than
# quietly failing a check. The file name must match the binary package name.
install(
  FILES ${LLVMDSDL_SOURCE_DIR}/packaging/deb/lintian-overrides/llvm-dsdl
  DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/lintian/overrides
  COMPONENT ${LLVMDSDL_INSTALL_COMPONENT_BIN})

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
    DESTINATION ${CMAKE_INSTALL_DOCDIR}
    COMPONENT ${LLVMDSDL_INSTALL_COMPONENT_BIN})

  install(
    FILES ${LLVMDSDL_DEB_CHANGELOG_GZ}
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/doc/llvm-dsdl-dev
    COMPONENT ${LLVMDSDL_INSTALL_COMPONENT_DEV})
else()
  message(WARNING
    "gzip not found; the Debian changelog will be omitted and .deb packages "
    "produced from this build will not satisfy Debian policy.")
endif()

# Man pages, generated from each tool's own --help so the two cannot drift.
#
# The page date comes from the changelog's release trailer rather than the clock:
# a build-time timestamp would make every rebuild produce a different page, which
# this project's reproducibility posture does not tolerate.
set(LLVMDSDL_MAN_DATE "")
file(STRINGS "${LLVMDSDL_DEB_CHANGELOG_SRC}" LLVMDSDL_CHANGELOG_LINES)
foreach(line IN LISTS LLVMDSDL_CHANGELOG_LINES)
  if(line MATCHES "^ -- .*  [A-Za-z]+, ([0-9]+) ([A-Za-z]+) ([0-9]+) ")
    set(_day "${CMAKE_MATCH_1}")
    set(_mon "${CMAKE_MATCH_2}")
    set(_year "${CMAKE_MATCH_3}")
    set(_months Jan Feb Mar Apr May Jun Jul Aug Sep Oct Nov Dec)
    list(FIND _months "${_mon}" _mon_index)
    if(_mon_index GREATER_EQUAL 0)
      math(EXPR _mon_num "${_mon_index} + 1")
      string(LENGTH "${_day}" _day_len)
      if(_day_len EQUAL 1)
        set(_day "0${_day}")
      endif()
      if(_mon_num LESS 10)
        set(_mon_num "0${_mon_num}")
      endif()
      set(LLVMDSDL_MAN_DATE "${_year}-${_mon_num}-${_day}")
    endif()
    break()
  endif()
endforeach()
if(NOT LLVMDSDL_MAN_DATE)
  message(FATAL_ERROR
    "Could not read a release date from the trailer line of ${LLVMDSDL_DEB_CHANGELOG_SRC}.")
endif()

if(LLVMDSDL_PYTHON3_EXE AND LLVMDSDL_GZIP_EXE)
  set(LLVMDSDL_MAN_GENERATOR "${LLVMDSDL_SOURCE_DIR}/tools/man/generate_manpage.py")
  set(LLVMDSDL_MAN_DIR "${LLVMDSDL_BINARY_DIR}/man")

  # dsdlc and dsdld print their own NAME line; dsdl-opt uses LLVM's cl::opt help,
  # whose OVERVIEW just repeats the program name, so its summary is supplied here.
  set(LLVMDSDL_MAN_DESC_dsdlc "")
  set(LLVMDSDL_MAN_DESC_dsdl_opt "MLIR pass driver for the dsdl dialect")
  set(LLVMDSDL_MAN_DESC_dsdld "")

  set(LLVMDSDL_MAN_PAGES)
  foreach(tool IN LISTS LLVMDSDL_INSTALLABLE_TOOLS)
    # dsdl-opt would otherwise form a variable name containing a hyphen.
    string(MAKE_C_IDENTIFIER "${tool}" tool_key)
    set(man_page "${LLVMDSDL_MAN_DIR}/${tool}.1")
    set(desc_arg)
    if(LLVMDSDL_MAN_DESC_${tool_key})
      set(desc_arg --description "${LLVMDSDL_MAN_DESC_${tool_key}}")
    endif()
    # gzip -f consumes the .1 and leaves only the .1.gz, so the .gz is what this
    # command actually produces; declaring the .1 would leave the build convinced
    # the output was never created and regenerate it on every invocation.
    add_custom_command(
      OUTPUT "${man_page}.gz"
      COMMAND "${LLVMDSDL_PYTHON3_EXE}" "${LLVMDSDL_MAN_GENERATOR}"
              --tool "$<TARGET_FILE:${tool}>"
              --name "${tool}"
              --version "${LLVMDSDL_TOOL_VERSION}"
              --date "${LLVMDSDL_MAN_DATE}"
              ${desc_arg}
              --output "${man_page}"
      COMMAND "${LLVMDSDL_GZIP_EXE}" -9 -n -f "${man_page}"
      DEPENDS ${tool} "${LLVMDSDL_MAN_GENERATOR}"
      COMMENT "Generating man page for ${tool}"
      VERBATIM)
    list(APPEND LLVMDSDL_MAN_PAGES "${man_page}.gz")

    install(
      FILES "${man_page}.gz"
      DESTINATION ${CMAKE_INSTALL_MANDIR}/man1
      COMPONENT ${LLVMDSDL_INSTALL_COMPONENT_BIN})
  endforeach()

  add_custom_target(man-pages ALL DEPENDS ${LLVMDSDL_MAN_PAGES})
else()
  message(WARNING
    "python3 or gzip not found; man pages will not be generated and .deb packages "
    "produced from this build will not satisfy Debian policy.")
endif()

set(LLVMDSDL_INSTALL_CONFIG_ARGS)
if(CMAKE_CONFIGURATION_TYPES)
  list(APPEND LLVMDSDL_INSTALL_CONFIG_ARGS --config $<CONFIG>)
elseif(CMAKE_BUILD_TYPE)
  list(APPEND LLVMDSDL_INSTALL_CONFIG_ARGS --config ${CMAKE_BUILD_TYPE})
endif()

# The binaries are not all the `bin` component installs: the SBOM, the compressed Debian changelog
# and the man pages are generated too. They are ALL targets, so a full `cmake --build` produces them
# and the omission is invisible there -- but `cmake --build --target install-bin` on a fresh tree
# builds only what the target asks for, and the install then fails on the first generated file that
# nobody made. Each is added conditionally because two of them are configured out on some platforms.
set(LLVMDSDL_INSTALL_BIN_DEPENDS ${LLVMDSDL_INSTALLABLE_TOOLS})
foreach(generated IN ITEMS sbom deb-changelog man-pages)
  if(TARGET ${generated})
    list(APPEND LLVMDSDL_INSTALL_BIN_DEPENDS ${generated})
  endif()
endforeach()

# Everything `--component bin` will ask for, as one target, so that no caller has to keep a list of
# it. Anything that installs the bin component must build this first -- and that is more than one
# caller: `install-bin` below, and the showroom recipe fixture, which installs into a private prefix
# of its own and so cannot go through `install-bin`. Both used to name targets by hand and both were
# wrong in the same way, because a lane that builds a subset (the coverage lane builds dsdlc,
# dsdl-opt and the unit tests, and no dsdld) then fails inside cmake_install.cmake on a path nobody
# asked for.
add_custom_target(llvmdsdl-bin-component
  COMMENT "Building everything the `bin` install component ships")
add_dependencies(llvmdsdl-bin-component ${LLVMDSDL_INSTALL_BIN_DEPENDS})

add_custom_target(install-bin
  COMMAND
    ${CMAKE_COMMAND}
      --install ${LLVMDSDL_BINARY_DIR}
      ${LLVMDSDL_INSTALL_CONFIG_ARGS}
      --component ${LLVMDSDL_INSTALL_COMPONENT_BIN}
  USES_TERMINAL
  VERBATIM
  COMMENT "Installing llvm-dsdl binaries only"
)
add_dependencies(install-bin llvmdsdl-bin-component)

# The `dev` component ships the same changelog alongside the libraries and headers.
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

if(LLVMDSDL_ENABLE_PYTHON_ACCELERATOR)
  find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter Development.Module)

  add_library(llvmdsdl-python-runtime-accelerator MODULE
    runtime/python_accel/dsdl_runtime_accel.c
  )
  target_include_directories(llvmdsdl-python-runtime-accelerator
    PRIVATE
      ${LLVMDSDL_SOURCE_DIR}/runtime
  )
  target_link_libraries(llvmdsdl-python-runtime-accelerator
    PRIVATE
      Python3::Module
  )
  set_target_properties(llvmdsdl-python-runtime-accelerator
    PROPERTIES
      PREFIX ""
      OUTPUT_NAME "_dsdl_runtime_accel"
      LIBRARY_OUTPUT_DIRECTORY "${LLVMDSDL_BINARY_DIR}/python-accel"
      LIBRARY_OUTPUT_DIRECTORY_DEBUG "${LLVMDSDL_BINARY_DIR}/python-accel/Debug"
      LIBRARY_OUTPUT_DIRECTORY_RELWITHDEBINFO "${LLVMDSDL_BINARY_DIR}/python-accel/RelWithDebInfo"
      LIBRARY_OUTPUT_DIRECTORY_RELEASE "${LLVMDSDL_BINARY_DIR}/python-accel/Release"
      LIBRARY_OUTPUT_DIRECTORY_MINSIZEREL "${LLVMDSDL_BINARY_DIR}/python-accel/MinSizeRel"
  )

  add_custom_target(build-python-runtime-accelerator
    DEPENDS llvmdsdl-python-runtime-accelerator
  )
endif()

if(LLVMDSDL_ENABLE_SELF_CONTAINED_TOOL_BUNDLE)
  # All three tools, not just the two that generate code: the macOS release
  # tarball ships this tree, and a package missing the language server would be
  # a strange thing to hand someone.
  #
  # Part of ALL only when the install rules consume it (macOS vendoring), because
  # then `cmake --install` must find it already built. Otherwise it stays opt-in:
  # it copies and re-signs ~150 MB of LLVM, which no ordinary build should pay for.
  set(LLVMDSDL_BUNDLE_IN_ALL "")
  if(LLVMDSDL_VENDOR_LLVM_VIA_BUNDLE)
    set(LLVMDSDL_BUNDLE_IN_ALL ALL)
  endif()

  add_custom_target(bundle-tools-self-contained ${LLVMDSDL_BUNDLE_IN_ALL}
    COMMAND
      ${CMAKE_COMMAND}
        "-DTOOLS=$<TARGET_FILE:dsdlc>;$<TARGET_FILE:dsdl-opt>;$<TARGET_FILE:dsdld>"
        -DOUTPUT_DIR=${LLVMDSDL_SELF_CONTAINED_TOOLS_DIR}
        -DLLVMDSDL_SOURCE_DIR=${LLVMDSDL_SOURCE_DIR}
        -P ${LLVMDSDL_SOURCE_DIR}/cmake/BundleSelfContainedTools.cmake
    DEPENDS ${LLVMDSDL_INSTALLABLE_TOOLS}
    USES_TERMINAL
    VERBATIM
    COMMENT
      "Bundling self-contained tools into ${LLVMDSDL_SELF_CONTAINED_TOOLS_DIR}"
  )
endif()

set(LLVMDSDL_UAVCAN_ROOT "")
if(EXISTS "${LLVMDSDL_SOURCE_DIR}/public_regulated_dsdl/uavcan")
  set(LLVMDSDL_UAVCAN_ROOT
      "${LLVMDSDL_SOURCE_DIR}/public_regulated_dsdl/uavcan")
elseif(EXISTS "${LLVMDSDL_SOURCE_DIR}/submodules/public_regulated_data_types/uavcan")
  set(LLVMDSDL_UAVCAN_ROOT
      "${LLVMDSDL_SOURCE_DIR}/submodules/public_regulated_data_types/uavcan")
endif()

if(LLVMDSDL_UAVCAN_ROOT)
  set(LLVMDSDL_GENERATED_UAVCAN_DIR
      "${LLVMDSDL_BINARY_DIR}/generated/uavcan")
  set(LLVMDSDL_UAVCAN_PY_PACKAGE "uavcan_dsdl_generated_py")
  set(LLVMDSDL_UAVCAN_PY_OUT_DIR
      "${LLVMDSDL_GENERATED_UAVCAN_DIR}/python")
  set(LLVMDSDL_UAVCAN_PY_WHEEL_OUT_DIR
      "${LLVMDSDL_GENERATED_UAVCAN_DIR}/python-wheel/$<CONFIG>")

  function(llvmdsdl_add_uavcan_codegen_target target_name out_subdir)
    set(out_dir "${LLVMDSDL_GENERATED_UAVCAN_DIR}/${out_subdir}")
    add_custom_target(${target_name}
      COMMAND ${CMAKE_COMMAND} -E rm -rf "${out_dir}"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${out_dir}"
      COMMAND
        $<TARGET_FILE:dsdlc>
          ${ARGN}
          "${LLVMDSDL_UAVCAN_ROOT}"
          --outdir "${out_dir}"
      DEPENDS dsdlc
      USES_TERMINAL
      VERBATIM
      COMMENT "Generating uavcan DSDL artifacts: ${target_name}"
    )
  endfunction()

  function(llvmdsdl_add_uavcan_c_impl_target target_name out_subdir)
    set(out_dir "${LLVMDSDL_GENERATED_UAVCAN_DIR}/${out_subdir}")
    add_custom_target(${target_name}
      COMMAND ${CMAKE_COMMAND} -E rm -rf "${out_dir}"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${out_dir}"
      COMMAND
        $<TARGET_FILE:dsdlc>
          --target-language c
          ${ARGN}
          "${LLVMDSDL_UAVCAN_ROOT}"
          --outdir "${out_dir}"
      COMMAND
        ${CMAKE_COMMAND}
          -DC_COMPILER=${CMAKE_C_COMPILER}
          -DSOURCE_ROOT=${out_dir}
          -DINCLUDE_ROOT=${out_dir}
          -P "${LLVMDSDL_SOURCE_DIR}/cmake/CompileGeneratedCSources.cmake"
      DEPENDS dsdlc
      USES_TERMINAL
      VERBATIM
      COMMENT "Generating and compiling uavcan C impl translation units: ${target_name}"
    )
  endfunction()

  llvmdsdl_add_uavcan_c_impl_target(generate-uavcan-c
                                    c)
  llvmdsdl_add_uavcan_codegen_target(generate-uavcan-cpp-std
                                     cpp-std
                                     --target-language cpp --cpp-profile std)
  llvmdsdl_add_uavcan_codegen_target(generate-uavcan-cpp-pmr
                                     cpp-pmr
                                     --target-language cpp --cpp-profile pmr)
  llvmdsdl_add_uavcan_codegen_target(generate-uavcan-cpp-autosar
                                     cpp-autosar
                                     --target-language cpp --cpp-profile autosar)
  llvmdsdl_add_uavcan_codegen_target(generate-uavcan-cpp-both
                                     cpp-both
                                     --target-language cpp --cpp-profile both)
  llvmdsdl_add_uavcan_codegen_target(generate-uavcan-rust-std
                                     rust-std
                                     --target-language rust --rust-profile std --rust-crate-name
                                     uavcan_dsdl_generated)
  llvmdsdl_add_uavcan_codegen_target(generate-uavcan-rust-no-std-alloc
                                     rust-no-std-alloc
                                     --target-language rust --rust-profile no-std-alloc --rust-crate-name
                                     uavcan_dsdl_generated_no_std)
  llvmdsdl_add_uavcan_codegen_target(generate-uavcan-rust-runtime-fast
                                     rust-runtime-fast
                                     --target-language rust --rust-runtime-specialization fast --rust-profile std --rust-crate-name
                                     uavcan_dsdl_generated_fast)
  llvmdsdl_add_uavcan_codegen_target(generate-uavcan-rust-no-std-runtime-fast
                                     rust-no-std-runtime-fast
                                     --target-language rust --rust-profile no-std-alloc --rust-runtime-specialization fast --rust-crate-name
                                     uavcan_dsdl_generated_no_std_fast)
  llvmdsdl_add_uavcan_codegen_target(generate-uavcan-go
                                     go
                                     --target-language go --go-module uavcan_dsdl_generated)
  llvmdsdl_add_uavcan_codegen_target(generate-uavcan-ts
                                     ts
                                     --target-language ts --ts-module uavcan_dsdl_generated_ts)
  llvmdsdl_add_uavcan_codegen_target(generate-uavcan-ts-runtime-fast
                                     ts-runtime-fast
                                     --target-language ts --ts-module uavcan_dsdl_generated_ts_fast --ts-runtime-specialization fast)
  llvmdsdl_add_uavcan_codegen_target(generate-uavcan-python
                                     python
                                     --target-language python --py-package ${LLVMDSDL_UAVCAN_PY_PACKAGE})

  add_custom_target(stage-uavcan-python-runtime-accelerator
    COMMAND
      ${CMAKE_COMMAND}
        -DGENERATED_OUT_DIR=${LLVMDSDL_UAVCAN_PY_OUT_DIR}
        -DPY_PACKAGE=${LLVMDSDL_UAVCAN_PY_PACKAGE}
        -DREQUIRE_ACCELERATOR=OFF
        -DACCEL_MODULE=$<$<TARGET_EXISTS:llvmdsdl-python-runtime-accelerator>:$<TARGET_FILE:llvmdsdl-python-runtime-accelerator>>
        -P ${LLVMDSDL_SOURCE_DIR}/cmake/StagePythonRuntimeAccelerator.cmake
    DEPENDS generate-uavcan-python
    USES_TERMINAL
    VERBATIM
    COMMENT "Staging optional Python accelerator beside generated uavcan Python package"
  )

  add_custom_target(stage-uavcan-python-runtime-accelerator-required
    COMMAND
      ${CMAKE_COMMAND}
        -DGENERATED_OUT_DIR=${LLVMDSDL_UAVCAN_PY_OUT_DIR}
        -DPY_PACKAGE=${LLVMDSDL_UAVCAN_PY_PACKAGE}
        -DREQUIRE_ACCELERATOR=ON
        -DACCEL_MODULE=$<$<TARGET_EXISTS:llvmdsdl-python-runtime-accelerator>:$<TARGET_FILE:llvmdsdl-python-runtime-accelerator>>
        -P ${LLVMDSDL_SOURCE_DIR}/cmake/StagePythonRuntimeAccelerator.cmake
    DEPENDS generate-uavcan-python
    USES_TERMINAL
    VERBATIM
    COMMENT "Staging Python accelerator (required mode)"
  )

  add_custom_target(stage-uavcan-python-wheel
    COMMAND
      ${CMAKE_COMMAND}
        -DGENERATED_OUT_DIR=${LLVMDSDL_UAVCAN_PY_OUT_DIR}
        -DPY_PACKAGE=${LLVMDSDL_UAVCAN_PY_PACKAGE}
        -DREQUIRE_ACCELERATOR=OFF
        -DACCEL_MODULE=$<$<TARGET_EXISTS:llvmdsdl-python-runtime-accelerator>:$<TARGET_FILE:llvmdsdl-python-runtime-accelerator>>
        -P ${LLVMDSDL_SOURCE_DIR}/cmake/StagePythonRuntimeAccelerator.cmake
    COMMAND
      ${CMAKE_COMMAND}
        -DGENERATED_OUT_DIR=${LLVMDSDL_UAVCAN_PY_OUT_DIR}
        -DWHEEL_OUT_DIR=${LLVMDSDL_UAVCAN_PY_WHEEL_OUT_DIR}
        -DPYTHON_EXECUTABLE=${LLVMDSDL_PYTHON3_EXE}
        -DPY_PACKAGE=${LLVMDSDL_UAVCAN_PY_PACKAGE}
        -DREQUIRE_ACCELERATOR=OFF
        -P ${LLVMDSDL_SOURCE_DIR}/cmake/BuildPythonWheel.cmake
    DEPENDS generate-uavcan-python
    USES_TERMINAL
    VERBATIM
    COMMENT "Building/staging optional wheel for generated uavcan Python package"
  )

  add_custom_target(stage-uavcan-python-wheel-accel-required
    COMMAND
      ${CMAKE_COMMAND}
        -DGENERATED_OUT_DIR=${LLVMDSDL_UAVCAN_PY_OUT_DIR}
        -DPY_PACKAGE=${LLVMDSDL_UAVCAN_PY_PACKAGE}
        -DREQUIRE_ACCELERATOR=ON
        -DACCEL_MODULE=$<$<TARGET_EXISTS:llvmdsdl-python-runtime-accelerator>:$<TARGET_FILE:llvmdsdl-python-runtime-accelerator>>
        -P ${LLVMDSDL_SOURCE_DIR}/cmake/StagePythonRuntimeAccelerator.cmake
    COMMAND
      ${CMAKE_COMMAND}
        -DGENERATED_OUT_DIR=${LLVMDSDL_UAVCAN_PY_OUT_DIR}
        -DWHEEL_OUT_DIR=${LLVMDSDL_UAVCAN_PY_WHEEL_OUT_DIR}
        -DPYTHON_EXECUTABLE=${LLVMDSDL_PYTHON3_EXE}
        -DPY_PACKAGE=${LLVMDSDL_UAVCAN_PY_PACKAGE}
        -DREQUIRE_ACCELERATOR=ON
        -P ${LLVMDSDL_SOURCE_DIR}/cmake/BuildPythonWheel.cmake
    DEPENDS generate-uavcan-python
    USES_TERMINAL
    VERBATIM
    COMMENT "Building/staging accelerator-required wheel for generated uavcan Python package"
  )

  if(TARGET build-python-runtime-accelerator)
    add_dependencies(stage-uavcan-python-runtime-accelerator
                     build-python-runtime-accelerator)
    add_dependencies(stage-uavcan-python-runtime-accelerator-required
                     build-python-runtime-accelerator)
    add_dependencies(stage-uavcan-python-wheel
                     build-python-runtime-accelerator)
    add_dependencies(stage-uavcan-python-wheel-accel-required
                     build-python-runtime-accelerator)
  endif()

  add_custom_target(generate-uavcan-all)
  add_dependencies(generate-uavcan-all
                   generate-uavcan-c
                   generate-uavcan-cpp-both
                   generate-uavcan-cpp-std
                   generate-uavcan-cpp-pmr
                   generate-uavcan-cpp-autosar
                   generate-uavcan-rust-std
                   generate-uavcan-rust-no-std-alloc
                   generate-uavcan-rust-runtime-fast
                   generate-uavcan-rust-no-std-runtime-fast
                   generate-uavcan-go
                   generate-uavcan-ts
                   generate-uavcan-ts-runtime-fast
                   generate-uavcan-python)
else()
  message(WARNING
    "uavcan DSDL root not found. Expected either "
    "${LLVMDSDL_SOURCE_DIR}/public_regulated_dsdl/uavcan or "
    "${LLVMDSDL_SOURCE_DIR}/submodules/public_regulated_data_types/uavcan. "
    "Skipping generation targets.")
endif()

enable_testing()
add_subdirectory(test)

# Hashed-container iteration in the generated-output path.
#
# A std::unordered_* iteration order is a property of the standard library, not
# of the data, so a loop over one can emit different bytes when built against a
# different stdlib while being perfectly deterministic for any single binary.
# The cross-stdlib corpus gate detects that after the fact, and only for as long
# as the two CI halves keep using different standard libraries -- which owning
# one toolchain ends. This excludes the class at the source instead, and costs
# no build: it reads the sources, so it runs without dsdlc.
if(LLVMDSDL_PYTHON3_EXE)
  add_test(
    NAME llvmdsdl-determinism-unordered-iteration
    COMMAND "${LLVMDSDL_PYTHON3_EXE}"
            "${LLVMDSDL_SOURCE_DIR}/tools/determinism/check_unordered_iteration.py"
            --root "${LLVMDSDL_SOURCE_DIR}")
  set_tests_properties(llvmdsdl-determinism-unordered-iteration PROPERTIES
    LABELS "determinism;contract")
endif()

# Below enable_testing() rather than beside the other examples: the showroom registers ctests for its
# build recipes, and add_test() in a directory processed before enable_testing() is not reliably
# picked up. It also joins docs-llms to the documentation chain from there, for the same ordering
# reason in the other direction.
add_subdirectory(examples/showroom)

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
    DEPENDS llvmdsdl-unit-tests dsdlc dsdl-opt
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
        "-DOBJECTS=$<TARGET_FILE:dsdlc>;$<TARGET_FILE:dsdl-opt>;$<TARGET_FILE:llvmdsdl-unit-tests>"
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

# Release packaging (CPack). Included last: include(CPack) snapshots every CPACK_*
# variable at that point, so it must follow all install() rules and settings.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Packaging.cmake)
