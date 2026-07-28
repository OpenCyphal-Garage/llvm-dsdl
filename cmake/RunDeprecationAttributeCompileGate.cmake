#===----------------------------------------------------------------------===#
#
# Part of the OpenCyphal project, under the MIT licence
# SPDX-License-Identifier: MIT
#
#===----------------------------------------------------------------------===#
#
# Compile gate for --emit-deprecation-attributes.
#
# A language-native deprecation attribute makes the *generated* code warn about itself: the deprecated
# name is referenced by its own declaration, by its serializer signatures, and by any struct that
# embeds it. The backends suppress those diagnostics across each generated file, and this test is what
# keeps that suppression honest -- without it a misplaced pragma only shows up in a downstream
# -Werror build.
#
# The regulated uavcan namespace is the corpus on purpose: it contains two dozen deprecated
# definitions, including uavcan.file.Path.1.0, which five other definitions embed as a field.
#
# The gate asserts both halves of the contract:
#   1. Including every generated header is clean under -Werror.
#   2. Naming a deprecated type from user code still warns.

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

# ---------------------------------------------------------------------------------------------------
# C

llvmdsdl_run_or_fail("dsdlc C generation"
  "${DSDLC}" --target-language c "${UAVCAN_ROOT}"
             --emit-deprecation-attributes --outdir "${WORK_DIR}/c")

# A deprecated service, a deprecated type embedded as a field by other definitions, and a
# non-deprecated type, all in one translation unit.
file(WRITE "${WORK_DIR}/c_include_probe.c"
"#include \"uavcan/file/Read_1_0.h\"
#include \"uavcan/file/Path_1_0.h\"
#include \"uavcan/node/Health_1_0.h\"
int main(void) { return 0; }
")

llvmdsdl_run_or_fail("C include-only compile under -Werror"
  "${C_COMPILER}" -std=c11 -Wall -Wextra -Werror
                  -I "${WORK_DIR}/c" -c "${WORK_DIR}/c_include_probe.c"
                  -o "${WORK_DIR}/c_include_probe.o")

# The other half: user code naming a deprecated type must still be diagnosed. Compiled *without*
# -Werror so the build succeeds, then the diagnostic text is asserted.
file(WRITE "${WORK_DIR}/c_use_probe.c"
"#include \"uavcan/file/Read_1_0.h\"
static uavcan__file__Read__Request req;
int main(void) { (void)req; return 0; }
")

execute_process(
  COMMAND "${C_COMPILER}" -std=c11 -Wall -Wextra
                          -I "${WORK_DIR}/c" -c "${WORK_DIR}/c_use_probe.c"
                          -o "${WORK_DIR}/c_use_probe.o"
  RESULT_VARIABLE _c_use_result
  OUTPUT_VARIABLE _c_use_stdout
  ERROR_VARIABLE _c_use_stderr)
if(NOT _c_use_result EQUAL 0)
  message(FATAL_ERROR "C use-probe failed to compile:\n${_c_use_stdout}\n${_c_use_stderr}")
endif()
if(NOT _c_use_stderr MATCHES "deprecated")
  message(FATAL_ERROR
    "C use-probe produced no deprecation diagnostic; the attribute is not reaching user code:\n${_c_use_stderr}")
endif()

# ---------------------------------------------------------------------------------------------------
# C++

llvmdsdl_run_or_fail("dsdlc C++ generation"
  "${DSDLC}" --target-language cpp "${UAVCAN_ROOT}" --cpp-profile std
             --emit-deprecation-attributes --outdir "${WORK_DIR}/cpp")

file(WRITE "${WORK_DIR}/cpp_include_probe.cpp"
"#include \"uavcan/file/Read_1_0.hpp\"
#include \"uavcan/file/Path_1_0.hpp\"
#include \"uavcan/node/Health_1_0.hpp\"
int main() { return 0; }
")

llvmdsdl_run_or_fail("C++ include-only compile under -Werror"
  "${CXX_COMPILER}" -std=c++20 -Wall -Wextra -Werror
                    -I "${WORK_DIR}/cpp" -c "${WORK_DIR}/cpp_include_probe.cpp"
                    -o "${WORK_DIR}/cpp_include_probe.o")

file(WRITE "${WORK_DIR}/cpp_use_probe.cpp"
"#include \"uavcan/file/Read_1_0.hpp\"
static uavcan::file::Read_1_0__Request req;
int main() { (void)req; return 0; }
")

execute_process(
  COMMAND "${CXX_COMPILER}" -std=c++20 -Wall -Wextra
                            -I "${WORK_DIR}/cpp" -c "${WORK_DIR}/cpp_use_probe.cpp"
                            -o "${WORK_DIR}/cpp_use_probe.o"
  RESULT_VARIABLE _cpp_use_result
  OUTPUT_VARIABLE _cpp_use_stdout
  ERROR_VARIABLE _cpp_use_stderr)
if(NOT _cpp_use_result EQUAL 0)
  message(FATAL_ERROR "C++ use-probe failed to compile:\n${_cpp_use_stdout}\n${_cpp_use_stderr}")
endif()
if(NOT _cpp_use_stderr MATCHES "deprecated")
  message(FATAL_ERROR
    "C++ use-probe produced no deprecation diagnostic; the attribute is not reaching user code:\n${_cpp_use_stderr}")
endif()

# ---------------------------------------------------------------------------------------------------
# Rust
#
# Optional: cargo is not required to build this project, so the Rust half is skipped when it is
# unavailable rather than failing the gate.

llvmdsdl_run_or_fail("dsdlc Rust generation"
  "${DSDLC}" --target-language rust "${UAVCAN_ROOT}" --rust-profile std
             --rust-crate-name deprecation_gate
             --emit-deprecation-attributes --outdir "${WORK_DIR}/rust")

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
