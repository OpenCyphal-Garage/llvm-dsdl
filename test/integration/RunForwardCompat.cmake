# Runtime forward/backward compatibility lane for the generated C, Rust, and Go serializers.
#
# The rest of the suite only ever decodes a buffer with the SAME type version that produced it, so a
# delimited composite's version-skew skip (advance the outer offset by the delimiter header, not by the
# nested reader's consumed count) is never exercised at runtime -- exactly the gap that hid a C-backend
# forward-compat bug. This lane generates TWO versions of a delimited composite (wire.nar.Inner has one
# field; wire.wid.Inner appends a second) and, in each language, decodes a wide-written buffer with the
# narrow reader (forward compatibility: the appended field is skipped and the trailing field still
# reads) and a narrow-written buffer with the wide reader (zero extension: the absent field zero-fills).
# All three backends must emit the same two markers.
cmake_minimum_required(VERSION 3.24)

foreach(var DSDLC OUT_DIR SOURCE_ROOT C_COMPILER CARGO_EXECUTABLE GO_EXECUTABLE)
  if(NOT DEFINED ${var} OR "${${var}}" STREQUAL "")
    message(FATAL_ERROR "Missing required variable: ${var}")
  endif()
endforeach()
foreach(tool "${DSDLC}" "${C_COMPILER}" "${CARGO_EXECUTABLE}" "${GO_EXECUTABLE}")
  if(NOT EXISTS "${tool}")
    message(FATAL_ERROR "required tool not found: ${tool}")
  endif()
endforeach()

set(expected_output [=[fwdcompat_ok
zeroext_ok]=])

file(REMOVE_RECURSE "${OUT_DIR}")
# The passed root's basename is the first namespace component, so `wire/` yields wire.nar / wire.wid.
set(dsdl_root "${OUT_DIR}/wire")
file(MAKE_DIRECTORY "${dsdl_root}/nar")
file(MAKE_DIRECTORY "${dsdl_root}/wid")

# Narrow (older) version: the delimited inner carries a single field.
file(WRITE "${dsdl_root}/nar/Inner.1.0.dsdl" "uint8 x\n@extent 64\n")
file(WRITE "${dsdl_root}/nar/Holder.1.0.dsdl" "wire.nar.Inner.1.0 inner\nuint8 tail\n@sealed\n")
# Wide (newer) version: the delimited inner appends a second field. `tail` still follows.
file(WRITE "${dsdl_root}/wid/Inner.1.0.dsdl" "uint8 x\nuint8 y\n@extent 64\n")
file(WRITE "${dsdl_root}/wid/Holder.1.0.dsdl" "wire.wid.Inner.1.0 inner\nuint8 tail\n@sealed\n")

function(dsdlc_generate language extra_args out_dir)
  execute_process(
    COMMAND "${DSDLC}" --target-language "${language}" "${dsdl_root}" --outdir "${out_dir}" ${extra_args}
    RESULT_VARIABLE gen_result
    OUTPUT_VARIABLE gen_stdout
    ERROR_VARIABLE gen_stderr
  )
  if(NOT gen_result EQUAL 0)
    message(STATUS "dsdlc ${language} stdout:\n${gen_stdout}")
    message(STATUS "dsdlc ${language} stderr:\n${gen_stderr}")
    message(FATAL_ERROR "forward-compat ${language} generation failed")
  endif()
endfunction()

# --- C -----------------------------------------------------------------------------------------------
set(c_out "${OUT_DIR}/c")
dsdlc_generate(c "" "${c_out}")
file(GLOB_RECURSE c_sources "${c_out}/wire/*.c")
set(c_bin "${OUT_DIR}/forward_compat_c")
execute_process(
  COMMAND
    "${C_COMPILER}" -std=c11 -Wall -Wextra -Werror
      -I "${c_out}"
      "${SOURCE_ROOT}/test/integration/ForwardCompatDriver.c"
      ${c_sources}
      -o "${c_bin}"
  RESULT_VARIABLE c_cc_result
  OUTPUT_VARIABLE c_cc_stdout
  ERROR_VARIABLE c_cc_stderr
)
if(NOT c_cc_result EQUAL 0)
  message(STATUS "C compile stdout:\n${c_cc_stdout}")
  message(STATUS "C compile stderr:\n${c_cc_stderr}")
  message(FATAL_ERROR "forward-compat C harness failed to compile")
endif()
execute_process(
  COMMAND "${c_bin}"
  RESULT_VARIABLE c_run_result
  OUTPUT_VARIABLE c_run_stdout
  ERROR_VARIABLE c_run_stderr
)
string(STRIP "${c_run_stdout}" c_output)
if(NOT c_run_result EQUAL 0 OR NOT c_output STREQUAL expected_output)
  message(STATUS "C harness stdout:\n${c_run_stdout}")
  message(STATUS "C harness stderr:\n${c_run_stderr}")
  message(FATAL_ERROR "forward-compat C harness failed (rc=${c_run_result})")
endif()

# --- Rust --------------------------------------------------------------------------------------------
set(rust_out "${OUT_DIR}/rust")
dsdlc_generate(rust "--rust-crate-name;dsdl_generated" "${rust_out}")
execute_process(
  COMMAND "${DSDLC}" --version
  OUTPUT_VARIABLE dsdlc_version_stdout
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
string(REGEX REPLACE "^dsdlc[ \t]+([0-9]+\\.[0-9]+\\.[0-9]+)$" "\\1" LLVMDSDL_TOOL_VERSION "${dsdlc_version_stdout}")
if(NOT LLVMDSDL_TOOL_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
  set(LLVMDSDL_TOOL_VERSION "0.0.0")
endif()
set(rust_harness "${OUT_DIR}/rust-harness")
file(MAKE_DIRECTORY "${rust_harness}/src")
set(RUST_OUT "${rust_out}")
configure_file("${SOURCE_ROOT}/test/integration/ForwardCompatCargo.toml.in" "${rust_harness}/Cargo.toml" @ONLY)
configure_file("${SOURCE_ROOT}/test/integration/ForwardCompatMain.rs" "${rust_harness}/src/main.rs" COPYONLY)
execute_process(
  COMMAND
    "${CMAKE_COMMAND}" -E env "CARGO_TARGET_DIR=${OUT_DIR}/cargo-target"
      "${CARGO_EXECUTABLE}" run --release --quiet --manifest-path "${rust_harness}/Cargo.toml"
  RESULT_VARIABLE rust_run_result
  OUTPUT_VARIABLE rust_run_stdout
  ERROR_VARIABLE rust_run_stderr
)
string(STRIP "${rust_run_stdout}" rust_output)
if(NOT rust_run_result EQUAL 0 OR NOT rust_output STREQUAL expected_output)
  message(STATUS "Rust harness stdout:\n${rust_run_stdout}")
  message(STATUS "Rust harness stderr:\n${rust_run_stderr}")
  message(FATAL_ERROR "forward-compat Rust harness failed (rc=${rust_run_result})")
endif()

# --- Go ----------------------------------------------------------------------------------------------
set(go_out "${OUT_DIR}/go")
dsdlc_generate(go "--go-module;dsdlfwdcompat" "${go_out}")
file(MAKE_DIRECTORY "${go_out}/cmd/forwardcompat")
configure_file("${SOURCE_ROOT}/test/integration/ForwardCompatMain.go" "${go_out}/cmd/forwardcompat/main.go" COPYONLY)
execute_process(
  COMMAND
    "${CMAKE_COMMAND}" -E env "GOCACHE=${OUT_DIR}/.gocache" "GOFLAGS=-mod=mod"
      "${GO_EXECUTABLE}" run ./cmd/forwardcompat
  WORKING_DIRECTORY "${go_out}"
  RESULT_VARIABLE go_run_result
  OUTPUT_VARIABLE go_run_stdout
  ERROR_VARIABLE go_run_stderr
)
string(STRIP "${go_run_stdout}" go_output)
if(NOT go_run_result EQUAL 0 OR NOT go_output STREQUAL expected_output)
  message(STATUS "Go harness stdout:\n${go_run_stdout}")
  message(STATUS "Go harness stderr:\n${go_run_stderr}")
  message(FATAL_ERROR "forward-compat Go harness failed (rc=${go_run_result})")
endif()

# --- Cross-language parity ---------------------------------------------------------------------------
if(NOT (c_output STREQUAL rust_output AND rust_output STREQUAL go_output))
  message(FATAL_ERROR
    "forward-compat marker mismatch across backends:\n C: ${c_output}\n Rust: ${rust_output}\n Go: ${go_output}")
endif()

message(STATUS "forward/backward compatibility lane passed (C, Rust, Go)")
