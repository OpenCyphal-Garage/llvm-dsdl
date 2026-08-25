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

if(NOT DEFINED C_TYPE_NAME_SCHEME)
  set(C_TYPE_NAME_SCHEME "unversioned")
endif()
if(NOT DEFINED TYPE_NAME_SCHEME)
  set(TYPE_NAME_SCHEME "unversioned")
endif()
include("${SOURCE_ROOT}/cmake/HarnessTypeNameTokens.cmake")
llvmdsdl_harness_type_name_tokens("${C_TYPE_NAME_SCHEME}" "${TYPE_NAME_SCHEME}")

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
# The driver names generated types, so it goes through the token substitution like every other
# harness rather than being compiled straight from the source tree.
set(c_driver "${OUT_DIR}/ForwardCompatDriver.c")
configure_file("${SOURCE_ROOT}/test/integration/ForwardCompatDriver.c" "${c_driver}" @ONLY)
execute_process(
  COMMAND
    "${C_COMPILER}" -std=c11 -Wall -Wextra -Werror
      -I "${c_out}"
      "${c_driver}"
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
configure_file("${SOURCE_ROOT}/test/integration/ForwardCompatMain.rs" "${rust_harness}/src/main.rs" @ONLY)
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
configure_file("${SOURCE_ROOT}/test/integration/ForwardCompatMain.go" "${go_out}/cmd/forwardcompat/main.go" @ONLY)
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

set(ran_backends "C" "Rust" "Go")

# --- Python (optional; run when a python3 interpreter is provided) ------------------------------------
if(PYTHON_EXECUTABLE AND NOT "${PYTHON_EXECUTABLE}" STREQUAL "")
  set(py_out "${OUT_DIR}/py")
  dsdlc_generate(python "" "${py_out}")
  set(py_driver "${OUT_DIR}/ForwardCompatDriver.py")
  configure_file("${SOURCE_ROOT}/test/integration/ForwardCompatDriver.py" "${py_driver}" @ONLY)
  execute_process(
    COMMAND
      "${CMAKE_COMMAND}" -E env "PYTHONPATH=${py_out}"
        "${PYTHON_EXECUTABLE}" "${py_driver}"
    RESULT_VARIABLE py_run_result
    OUTPUT_VARIABLE py_run_stdout
    ERROR_VARIABLE py_run_stderr
  )
  string(STRIP "${py_run_stdout}" py_output)
  if(NOT py_run_result EQUAL 0 OR NOT py_output STREQUAL expected_output)
    message(STATUS "Python harness stdout:\n${py_run_stdout}")
    message(STATUS "Python harness stderr:\n${py_run_stderr}")
    message(FATAL_ERROR "forward-compat Python harness failed (rc=${py_run_result})")
  endif()
  list(APPEND ran_backends "Python")
endif()

# --- TypeScript (optional; run when both tsc and node are provided) -----------------------------------
if(TSC_EXECUTABLE AND NOT "${TSC_EXECUTABLE}" STREQUAL "" AND NODE_EXECUTABLE AND NOT "${NODE_EXECUTABLE}" STREQUAL "")
  set(ts_out "${OUT_DIR}/ts")
  dsdlc_generate(ts "--ts-module;fcwire" "${ts_out}")
  configure_file(
    "${SOURCE_ROOT}/test/integration/ForwardCompatDriver.ts" "${ts_out}/forward_compat_driver.ts" @ONLY)
  file(WRITE "${ts_out}/tsconfig-forward-compat.json"
    "{ \"compilerOptions\": { \"target\": \"ES2020\", \"module\": \"CommonJS\", \"moduleResolution\": \"Node\", \"strict\": true, \"outDir\": \"./js\" }, \"include\": [\"./**/*.ts\"] }\n")
  execute_process(
    COMMAND "${TSC_EXECUTABLE}" -p "${ts_out}/tsconfig-forward-compat.json" --pretty false
    WORKING_DIRECTORY "${ts_out}"
    RESULT_VARIABLE tsc_result
    OUTPUT_VARIABLE tsc_stdout
    ERROR_VARIABLE tsc_stderr
  )
  if(NOT tsc_result EQUAL 0)
    message(STATUS "tsc stdout:\n${tsc_stdout}")
    message(STATUS "tsc stderr:\n${tsc_stderr}")
    message(FATAL_ERROR "forward-compat TypeScript harness failed to compile")
  endif()
  file(WRITE "${ts_out}/js/package.json" "{\n  \"type\": \"commonjs\"\n}\n")
  execute_process(
    COMMAND "${NODE_EXECUTABLE}" "${ts_out}/js/forward_compat_driver.js"
    RESULT_VARIABLE ts_run_result
    OUTPUT_VARIABLE ts_run_stdout
    ERROR_VARIABLE ts_run_stderr
  )
  string(STRIP "${ts_run_stdout}" ts_output)
  if(NOT ts_run_result EQUAL 0 OR NOT ts_output STREQUAL expected_output)
    message(STATUS "node stdout:\n${ts_run_stdout}")
    message(STATUS "node stderr:\n${ts_run_stderr}")
    message(FATAL_ERROR "forward-compat TypeScript harness failed (rc=${ts_run_result})")
  endif()
  list(APPEND ran_backends "TypeScript")
endif()

# Each backend above is asserted equal to `expected_output`, so they are equal to each other; the run
# list records which backends actually participated (Python/TS are skipped if their tools are absent).
string(REPLACE ";" ", " ran_backends_pretty "${ran_backends}")
message(STATUS "forward/backward compatibility lane passed (${ran_backends_pretty})")
