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

if(LLVMDSDL_PYTHON3_EXE)
  add_custom_target(convergence-report
    COMMAND
      ${CMAKE_COMMAND}
        -DPYTHON_EXECUTABLE=${LLVMDSDL_PYTHON3_EXE}
        -DREPO_ROOT=${LLVMDSDL_SOURCE_DIR}
        -DOUTPUT_JSON=${LLVMDSDL_BINARY_DIR}/convergence/$<CONFIG>/report.json
        -DOUTPUT_MD=${LLVMDSDL_BINARY_DIR}/convergence/$<CONFIG>/CONVERGENCE_SCORECARD.md
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
        -DOUTPUT_MD=${LLVMDSDL_BINARY_DIR}/convergence/$<CONFIG>/PARITY_MATRIX.md
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
        -DOUTPUT_MD=${LLVMDSDL_BINARY_DIR}/convergence/$<CONFIG>/MALFORMED_INPUT_CONTRACT_MATRIX.md
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
        -DOUTPUT_MD=${LLVMDSDL_BINARY_DIR}/convergence/$<CONFIG>/DETERMINISM_MATRIX.md
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

install(
  TARGETS ${LLVMDSDL_INSTALLABLE_TOOLS}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  COMPONENT ${LLVMDSDL_INSTALL_COMPONENT_BIN})

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

set(LLVMDSDL_INSTALL_CONFIG_ARGS)
if(CMAKE_CONFIGURATION_TYPES)
  list(APPEND LLVMDSDL_INSTALL_CONFIG_ARGS --config $<CONFIG>)
elseif(CMAKE_BUILD_TYPE)
  list(APPEND LLVMDSDL_INSTALL_CONFIG_ARGS --config ${CMAKE_BUILD_TYPE})
endif()

add_custom_target(install-bin
  COMMAND
    ${CMAKE_COMMAND}
      --install ${LLVMDSDL_BINARY_DIR}
      ${LLVMDSDL_INSTALL_CONFIG_ARGS}
      --component ${LLVMDSDL_INSTALL_COMPONENT_BIN}
  DEPENDS ${LLVMDSDL_INSTALLABLE_TOOLS}
  USES_TERMINAL
  VERBATIM
  COMMENT "Installing llvm-dsdl binaries only"
)

add_custom_target(install-dev
  COMMAND
    ${CMAKE_COMMAND}
      --install ${LLVMDSDL_BINARY_DIR}
      ${LLVMDSDL_INSTALL_CONFIG_ARGS}
      --component ${LLVMDSDL_INSTALL_COMPONENT_DEV}
  DEPENDS ${LLVMDSDL_INSTALLABLE_LIBRARIES}
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

set(LLVMDSDL_SELF_CONTAINED_TOOL_BUNDLE_DEFAULT OFF)
if(APPLE OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(LLVMDSDL_SELF_CONTAINED_TOOL_BUNDLE_DEFAULT ON)
endif()

option(LLVMDSDL_ENABLE_SELF_CONTAINED_TOOL_BUNDLE
       "Enable self-contained tool bundle target (copies dsdlc/dsdl-opt with runtime deps)"
       ${LLVMDSDL_SELF_CONTAINED_TOOL_BUNDLE_DEFAULT})

if(LLVMDSDL_ENABLE_SELF_CONTAINED_TOOL_BUNDLE)
  set(LLVMDSDL_SELF_CONTAINED_TOOLS_DIR
      "${LLVMDSDL_BINARY_DIR}/self-contained-tools")
  add_custom_target(bundle-tools-self-contained
    COMMAND
      ${CMAKE_COMMAND}
        -DTOOL_DSDLC=$<TARGET_FILE:dsdlc>
        -DTOOL_DSDLOPT=$<TARGET_FILE:dsdl-opt>
        -DOUTPUT_DIR=${LLVMDSDL_SELF_CONTAINED_TOOLS_DIR}
        -DLLVMDSDL_SOURCE_DIR=${LLVMDSDL_SOURCE_DIR}
        -P ${LLVMDSDL_SOURCE_DIR}/cmake/BundleSelfContainedTools.cmake
    DEPENDS dsdlc dsdl-opt
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
                                    --target-language c)
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
