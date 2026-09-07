#===----------------------------------------------------------------------===#
#
# Part of the OpenCyphal project, under the MIT licence
# SPDX-License-Identifier: MIT
#
#===----------------------------------------------------------------------===#
#
# Compile gate for the language-native deprecation attributes.
#
# A language-native deprecation attribute makes generated code warn about itself unless the emitter
# takes care: the deprecated name is referenced by its own serialiser signatures and by any struct
# that embeds it. C names the type through its struct tag, which carries no attribute; C++ and Rust
# suppress the diagnostic across each generated file. This test is what keeps that honest -- without
# it a reference spelled through the typedef, or a misplaced pragma, only shows up in a downstream
# -Werror build.
#
# The attributes are on by default, so no generation run here passes a flag: the default is exactly
# what needs guarding.
#
# The regulated uavcan namespace is the corpus on purpose: it contains two dozen deprecated
# definitions, including uavcan.file.Path.1.0, which five other definitions embed as a field.
#
# The gate asserts three things:
#   1. Including every generated header, and compiling the generated translation units, is clean
#      under -Werror.
#   2. Naming a deprecated type from user code still warns.
#   3. --no-deprecation-attributes is a real escape hatch: the same user code is then diagnostic-free
#      under -Werror. This is the supported answer for a -Werror build that cannot yet migrate, so it
#      has to keep working.
#
# GCC and Clang disagree on what counts as a deprecated mention: Clang exempts one made from inside a
# deprecated function or class, GCC exempts nothing beyond the deprecated entity's own member
# declarations. A gate run under one family misses what the other reports, so the C and C++ halves
# compile under both when the host has both: the configured compiler, and the other family looked up
# by name. The family of what is found is checked, since `gcc` on macOS is Clang.

if(NOT DEFINED DSDLC OR DSDLC STREQUAL "")
  message(FATAL_ERROR "DSDLC must be provided")
endif()
if(NOT DEFINED UAVCAN_ROOT OR UAVCAN_ROOT STREQUAL "")
  message(FATAL_ERROR "UAVCAN_ROOT must be provided")
endif()
if(NOT DEFINED WORK_DIR OR WORK_DIR STREQUAL "")
  message(FATAL_ERROR "WORK_DIR must be provided")
endif()
if(NOT DEFINED C_COMPILER OR C_COMPILER STREQUAL "")
  message(FATAL_ERROR "C_COMPILER must be provided")
endif()
if(NOT DEFINED CXX_COMPILER OR CXX_COMPILER STREQUAL "")
  message(FATAL_ERROR "CXX_COMPILER must be provided")
endif()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")

function(llvmdsdl_run_or_fail description)
  execute_process(
    COMMAND ${ARGN}
    RESULT_VARIABLE _result
    OUTPUT_VARIABLE _stdout
    ERROR_VARIABLE _stderr)
  if(NOT _result EQUAL 0)
    message(FATAL_ERROR "${description} failed (${_result}):\n${_stdout}\n${_stderr}")
  endif()
endfunction()

# The family `compiler` belongs to, `clang` or `gcc`, or empty when it is neither.
function(llvmdsdl_compiler_family compiler out_var)
  execute_process(
    COMMAND "${compiler}" --version
    RESULT_VARIABLE _result
    OUTPUT_VARIABLE _stdout
    ERROR_VARIABLE _stderr)
  set(_family "")
  if(_result EQUAL 0)
    if(_stdout MATCHES "clang")
      set(_family "clang")
    elseif(_stdout MATCHES "GCC|gcc|g\\+\\+")
      set(_family "gcc")
    endif()
  endif()
  set(${out_var} "${_family}" PARENT_SCOPE)
endfunction()

llvmdsdl_compiler_family("${C_COMPILER}" _configured_family)
set(_c_compilers "${C_COMPILER}")
set(_cxx_compilers "${CXX_COMPILER}")
if(_configured_family STREQUAL "clang")
  set(_other_family "gcc")
  set(_other_c_names gcc-15 gcc-14 gcc-13 gcc)
  set(_other_cxx_names g++-15 g++-14 g++-13 g++)
else()
  set(_other_family "clang")
  set(_other_c_names clang)
  set(_other_cxx_names clang++)
endif()
find_program(_other_c NAMES ${_other_c_names})
find_program(_other_cxx NAMES ${_other_cxx_names})
set(_other_available FALSE)
if(_other_c AND _other_cxx)
  llvmdsdl_compiler_family("${_other_c}" _other_c_family)
  llvmdsdl_compiler_family("${_other_cxx}" _other_cxx_family)
  if(_other_c_family STREQUAL _other_family AND _other_cxx_family STREQUAL _other_family)
    list(APPEND _c_compilers "${_other_c}")
    list(APPEND _cxx_compilers "${_other_cxx}")
    set(_other_available TRUE)
  endif()
endif()
if(NOT _other_available)
  message(STATUS "no ${_other_family} on the host; the C and C++ probes compile under ${C_COMPILER} only")
endif()

# Every invocation below asks for the whole corpus. A deprecated definition is usually one a newer
# version replaced, so the newest-version default drops the types this gate is about. That the
# default drops them is pinned in test/lit/newest-version-only.txt; what this gate tests is that the
# attribute reaches user code when such a type *is* generated, which needs the type to exist.

# ---------------------------------------------------------------------------------------------------
# C

llvmdsdl_run_or_fail("dsdlc C generation"
  "${DSDLC}" --target-language c --all-type-versions "${UAVCAN_ROOT}" --outdir "${WORK_DIR}/c")

llvmdsdl_run_or_fail("dsdlc C generation with --no-deprecation-attributes"
  "${DSDLC}" --target-language c --all-type-versions "${UAVCAN_ROOT}"
             --no-deprecation-attributes --outdir "${WORK_DIR}/c-noattr")

# A deprecated service, a deprecated type embedded as a field by other definitions, and a
# non-deprecated type, all in one translation unit.
file(WRITE "${WORK_DIR}/c_include_probe.c"
"#include \"uavcan/file/Read_1_0.h\"
#include \"uavcan/file/Path_1_0.h\"
#include \"uavcan/node/Health_1_0.h\"
int main(void) { return 0; }
")

# User code naming a deprecated type: the section typedef, and the service alias, which is a typedef
# of its own and carries its own attribute. Each is compiled *without* -Werror so the build succeeds,
# then the diagnostic text is asserted.
file(WRITE "${WORK_DIR}/c_use_probe.c"
"#include \"uavcan/file/Read_1_0.h\"
static uavcan__file__Read__Request req;
int main(void) { (void)req; return 0; }
")
file(WRITE "${WORK_DIR}/c_alias_probe.c"
"#include \"uavcan/file/Read_1_0.h\"
static uavcan__file__Read req;
int main(void) { (void)req; return 0; }
")

foreach(_cc IN LISTS _c_compilers)
  llvmdsdl_compiler_family("${_cc}" _label)

  llvmdsdl_run_or_fail("C include-only compile under -Werror (${_label})"
    "${_cc}" -std=c11 -Wall -Wextra -Werror
             -I "${WORK_DIR}/c" -c "${WORK_DIR}/c_include_probe.c"
             -o "${WORK_DIR}/c_include_probe.${_label}.o")

  # Including a header exercises the header alone. Each translation unit defines the serialiser
  # functions, and compiling it is what proves the implementation names the type through its tag too.
  foreach(_tu IN ITEMS "uavcan/file/Read_1_0" "uavcan/file/Path_1_0" "uavcan/node/Health_1_0")
    string(REPLACE "/" "_" _tu_object "${_tu}")
    llvmdsdl_run_or_fail("C translation-unit compile under -Werror (${_tu}.c, ${_label})"
      "${_cc}" -std=c11 -Wall -Wextra -Werror
               -I "${WORK_DIR}/c" -c "${WORK_DIR}/c/${_tu}.c"
               -o "${WORK_DIR}/${_tu_object}.${_label}.o")
  endforeach()

  foreach(_probe IN ITEMS c_use_probe c_alias_probe)
    execute_process(
      COMMAND "${_cc}" -std=c11 -Wall -Wextra
                       -I "${WORK_DIR}/c" -c "${WORK_DIR}/${_probe}.c"
                       -o "${WORK_DIR}/${_probe}.${_label}.o"
      RESULT_VARIABLE _probe_result
      OUTPUT_VARIABLE _probe_stdout
      ERROR_VARIABLE _probe_stderr)
    if(NOT _probe_result EQUAL 0)
      message(FATAL_ERROR "${_probe} failed to compile (${_label}):\n${_probe_stdout}\n${_probe_stderr}")
    endif()
    if(NOT _probe_stderr MATCHES "deprecated")
      message(FATAL_ERROR
        "${_probe} produced no deprecation diagnostic (${_label}); the attribute is not reaching user "
        "code:\n${_probe_stderr}")
    endif()

    # The escape hatch. A -Werror build that still depends on a deprecated definition has no migration
    # target to move to, and --no-deprecation-attributes is the supported answer; compiling the same
    # probe against opted-out headers is what keeps that answer true.
    llvmdsdl_run_or_fail("${_probe} compile under -Werror with --no-deprecation-attributes (${_label})"
      "${_cc}" -std=c11 -Wall -Wextra -Werror
               -I "${WORK_DIR}/c-noattr" -c "${WORK_DIR}/${_probe}.c"
               -o "${WORK_DIR}/${_probe}_noattr.${_label}.o")
  endforeach()
endforeach()

# ---------------------------------------------------------------------------------------------------
# C++

llvmdsdl_run_or_fail("dsdlc C++ generation"
  "${DSDLC}" --target-language cpp --all-type-versions "${UAVCAN_ROOT}" --cpp-profile std --outdir "${WORK_DIR}/cpp")

file(WRITE "${WORK_DIR}/cpp_include_probe.cpp"
"#include \"uavcan/file/Read_1_0.hpp\"
#include \"uavcan/file/Path_1_0.hpp\"
#include \"uavcan/node/Health_1_0.hpp\"
int main() { return 0; }
")

file(WRITE "${WORK_DIR}/cpp_use_probe.cpp"
"#include \"uavcan/file/Read_1_0.hpp\"
static uavcan::file::Read_Request req;
int main() { (void)req; return 0; }
")

foreach(_cxx IN LISTS _cxx_compilers)
  llvmdsdl_compiler_family("${_cxx}" _label)

  llvmdsdl_run_or_fail("C++ include-only compile under -Werror (${_label})"
    "${_cxx}" -std=c++20 -Wall -Wextra -Werror
              -I "${WORK_DIR}/cpp" -c "${WORK_DIR}/cpp_include_probe.cpp"
              -o "${WORK_DIR}/cpp_include_probe.${_label}.o")

  execute_process(
    COMMAND "${_cxx}" -std=c++20 -Wall -Wextra
                      -I "${WORK_DIR}/cpp" -c "${WORK_DIR}/cpp_use_probe.cpp"
                      -o "${WORK_DIR}/cpp_use_probe.${_label}.o"
    RESULT_VARIABLE _cpp_use_result
    OUTPUT_VARIABLE _cpp_use_stdout
    ERROR_VARIABLE _cpp_use_stderr)
  if(NOT _cpp_use_result EQUAL 0)
    message(FATAL_ERROR "C++ use-probe failed to compile (${_label}):\n${_cpp_use_stdout}\n${_cpp_use_stderr}")
  endif()
  if(NOT _cpp_use_stderr MATCHES "deprecated")
    message(FATAL_ERROR
      "C++ use-probe produced no deprecation diagnostic (${_label}); the attribute is not reaching user "
      "code:\n${_cpp_use_stderr}")
  endif()
endforeach()

# ---------------------------------------------------------------------------------------------------
# Rust
#
# Optional: cargo is not required to build this project, so the Rust half is skipped when it is
# unavailable rather than failing the gate.

llvmdsdl_run_or_fail("dsdlc Rust generation"
  "${DSDLC}" --target-language rust --all-type-versions "${UAVCAN_ROOT}" --rust-profile std
             --rust-crate-name deprecation_gate --outdir "${WORK_DIR}/rust")

if(CARGO_EXECUTABLE AND NOT CARGO_EXECUTABLE STREQUAL "CARGO_EXECUTABLE-NOTFOUND")
  execute_process(
    COMMAND ${CMAKE_COMMAND} -E env "RUSTFLAGS=-D warnings"
            "CARGO_TARGET_DIR=${WORK_DIR}/rust/target"
            "${CARGO_EXECUTABLE}" build --quiet --manifest-path "${WORK_DIR}/rust/Cargo.toml"
    RESULT_VARIABLE _rust_result
    OUTPUT_VARIABLE _rust_stdout
    ERROR_VARIABLE _rust_stderr)
  if(NOT _rust_result EQUAL 0)
    message(FATAL_ERROR
      "generated Rust crate does not build under -D warnings; the #![allow(deprecated)] guard is "
      "missing or misplaced:\n${_rust_stdout}\n${_rust_stderr}")
  endif()
else()
  message(STATUS "cargo unavailable; skipping the Rust half of the deprecation attribute gate")
endif()

message(STATUS "deprecation attribute compile gate passed")
