cmake_minimum_required(VERSION 3.24)

# Builds and runs the isolated cross-language runtime-primitive equivalence
# drivers (C, Rust, Go) against a single shared golden-vector file. Each driver
# checks every primitive direction on its own, so a bug in one runtime (or one
# direction) cannot be masked by a matching bug in another.

foreach(var C_COMPILER CARGO_EXECUTABLE GO_EXECUTABLE SOURCE_ROOT OUT_DIR VECTORS_FILE)
  if(NOT DEFINED ${var} OR "${${var}}" STREQUAL "")
    message(FATAL_ERROR "Missing required variable: ${var}")
  endif()
endforeach()

foreach(tool "${C_COMPILER}" "${CARGO_EXECUTABLE}" "${GO_EXECUTABLE}")
  if(NOT EXISTS "${tool}")
    message(FATAL_ERROR "required tool not found: ${tool}")
  endif()
endforeach()
if(NOT EXISTS "${VECTORS_FILE}")
  message(FATAL_ERROR "vectors file not found: ${VECTORS_FILE}")
endif()

set(driver_dir "${SOURCE_ROOT}/test/integration")
set(runtime_c_include "${SOURCE_ROOT}/runtime")
set(runtime_rs "${SOURCE_ROOT}/runtime/rust")
set(runtime_go "${SOURCE_ROOT}/runtime/go")

file(REMOVE_RECURSE "${OUT_DIR}")
file(MAKE_DIRECTORY "${OUT_DIR}")

# Each driver reports how many vectors it processed. A driver errors out on any
# unrecognized non-blank line, so it cannot silently skip a real vector; the
# final cross-check that all three counts agree (and are non-zero) therefore
# guarantees every runtime ran the same shared vectors.
macro(extract_processed lang stdout_var out_count)
  string(REGEX MATCH "PROCESSED ([0-9]+)" _m "${${stdout_var}}")
  if(NOT _m)
    message(FATAL_ERROR "${lang} driver did not report a PROCESSED count:\n${${stdout_var}}")
  endif()
  set(${out_count} "${CMAKE_MATCH_1}")
endmacro()

# ---------------------------------------------------------------- C driver ----
set(c_bin "${OUT_DIR}/c_primitive_driver")
execute_process(
  COMMAND "${C_COMPILER}" -std=c11 -O2 -Wall -Wextra
    -I "${runtime_c_include}"
    "${driver_dir}/PrimitiveEquivalenceDriver.c" -o "${c_bin}"
  RESULT_VARIABLE c_build_rc OUTPUT_VARIABLE c_build_out ERROR_VARIABLE c_build_err)
if(NOT c_build_rc EQUAL 0)
  message(FATAL_ERROR "C driver build failed:\n${c_build_out}\n${c_build_err}")
endif()
execute_process(
  COMMAND "${c_bin}" "${VECTORS_FILE}"
  RESULT_VARIABLE c_rc OUTPUT_VARIABLE c_out ERROR_VARIABLE c_err)
if(NOT c_rc EQUAL 0)
  message(FATAL_ERROR "C primitive equivalence FAILED:\n${c_out}\n${c_err}")
endif()
extract_processed("C" c_out c_count)

# ------------------------------------------------------------- Go driver ------
set(go_dir "${OUT_DIR}/go")
file(MAKE_DIRECTORY "${go_dir}")
file(WRITE "${go_dir}/go.mod"
  "module primitive_equivalence_driver\n\ngo 1.22\n\n"
  "require github.com/thirtytwobits/llvm-dsdl/runtime/go v0.0.0\n\n"
  "replace github.com/thirtytwobits/llvm-dsdl/runtime/go => ${runtime_go}\n")
file(COPY_FILE "${driver_dir}/PrimitiveEquivalenceDriver.go" "${go_dir}/main.go")
set(go_bin "${go_dir}/go_primitive_driver")
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
    "GOFLAGS=-mod=mod" "GOCACHE=${go_dir}/.gocache" "GOMODCACHE=${go_dir}/.gomodcache"
    "${GO_EXECUTABLE}" build -o "${go_bin}" .
  WORKING_DIRECTORY "${go_dir}"
  RESULT_VARIABLE go_build_rc OUTPUT_VARIABLE go_build_out ERROR_VARIABLE go_build_err)
if(NOT go_build_rc EQUAL 0)
  message(FATAL_ERROR "Go driver build failed:\n${go_build_out}\n${go_build_err}")
endif()
execute_process(
  COMMAND "${go_bin}" "${VECTORS_FILE}"
  RESULT_VARIABLE go_rc OUTPUT_VARIABLE go_out ERROR_VARIABLE go_err)
if(NOT go_rc EQUAL 0)
  message(FATAL_ERROR "Go primitive equivalence FAILED:\n${go_out}\n${go_err}")
endif()
extract_processed("Go" go_out go_count)

# ----------------------------------------------------------- Rust driver ------
# The runtime source and its generated semantic-wrappers sibling are copied in
# so the module's leading inner attributes stay at the true module start.
set(rs_dir "${OUT_DIR}/rust")
file(MAKE_DIRECTORY "${rs_dir}/src")
file(WRITE "${rs_dir}/Cargo.toml"
  "[package]\nname = \"primitive_equivalence_driver\"\nversion = \"0.0.0\"\nedition = \"2021\"\n\n"
  "[[bin]]\nname = \"primitive_equivalence_driver\"\npath = \"src/main.rs\"\n\n"
  "[features]\ndefault = [\"std\"]\nstd = []\nruntime-fast = []\n")
file(COPY_FILE "${driver_dir}/PrimitiveEquivalenceDriver.rs" "${rs_dir}/src/main.rs")
file(COPY_FILE "${runtime_rs}/dsdl_runtime.rs" "${rs_dir}/src/dsdl_runtime.rs")
file(COPY_FILE "${runtime_rs}/dsdl_runtime_semantic_wrappers.rs"
  "${rs_dir}/src/dsdl_runtime_semantic_wrappers.rs")
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env "CARGO_TARGET_DIR=${rs_dir}/target"
    "${CARGO_EXECUTABLE}" build --quiet --manifest-path "${rs_dir}/Cargo.toml"
  RESULT_VARIABLE rs_build_rc OUTPUT_VARIABLE rs_build_out ERROR_VARIABLE rs_build_err)
if(NOT rs_build_rc EQUAL 0)
  message(FATAL_ERROR "Rust driver build failed:\n${rs_build_out}\n${rs_build_err}")
endif()
execute_process(
  COMMAND "${rs_dir}/target/debug/primitive_equivalence_driver" "${VECTORS_FILE}"
  RESULT_VARIABLE rs_rc OUTPUT_VARIABLE rs_out ERROR_VARIABLE rs_err)
if(NOT rs_rc EQUAL 0)
  message(FATAL_ERROR "Rust primitive equivalence FAILED:\n${rs_out}\n${rs_err}")
endif()
extract_processed("Rust" rs_out rs_count)

if(c_count LESS 1)
  message(FATAL_ERROR "no vectors were processed")
endif()
if(NOT c_count EQUAL go_count OR NOT c_count EQUAL rs_count)
  message(FATAL_ERROR
    "drivers disagree on processed vector count: C=${c_count} Go=${go_count} Rust=${rs_count}")
endif()

message(STATUS
  "Primitive equivalence PASS: C, Rust, and Go each agreed on all ${c_count} shared vectors")
file(WRITE "${OUT_DIR}/primitive-equivalence-summary.txt"
  "vectors=${c_count}\n${c_out}${go_out}${rs_out}")
