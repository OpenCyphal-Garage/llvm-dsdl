cmake_minimum_required(VERSION 3.24)

# Builds and runs the isolated cross-language runtime-primitive equivalence
# drivers (C, Rust, Go) against a single shared golden-vector file. Each driver
# checks every primitive direction on its own, so a bug in one runtime (or one
# direction) cannot be masked by a matching bug in another.

foreach(var C_COMPILER CARGO_EXECUTABLE GO_EXECUTABLE PYTHON_EXECUTABLE SOURCE_ROOT OUT_DIR VECTORS_FILE)
  if(NOT DEFINED ${var} OR "${${var}}" STREQUAL "")
    message(FATAL_ERROR "Missing required variable: ${var}")
  endif()
endforeach()

foreach(tool "${C_COMPILER}" "${CARGO_EXECUTABLE}" "${GO_EXECUTABLE}" "${PYTHON_EXECUTABLE}")
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

# ---------------------------------------------------------- Python driver -----
# Python is double-typed: it runs the same shared vectors but may SKIP a small,
# explicitly-reported subset that is not comparable at the raw-primitive level
# (e.g. float16 overflow, which the Python primitive raises on while the C
# magic-float primitive returns inf). processed + skipped must still cover every
# vector, so nothing is dropped silently.
execute_process(
  COMMAND "${PYTHON_EXECUTABLE}" "${driver_dir}/PrimitiveEquivalenceDriver.py"
    "${VECTORS_FILE}" "${SOURCE_ROOT}/runtime/python/_dsdl_runtime.py"
  RESULT_VARIABLE py_rc OUTPUT_VARIABLE py_out ERROR_VARIABLE py_err)
if(NOT py_rc EQUAL 0)
  message(FATAL_ERROR "Python primitive equivalence FAILED:\n${py_out}\n${py_err}")
endif()
extract_processed("Python" py_out py_count)
string(REGEX MATCH "SKIPPED ([0-9]+)" _pysk "${py_out}")
if(NOT _pysk)
  message(FATAL_ERROR "Python driver did not report a SKIPPED count:\n${py_out}")
endif()
set(py_skipped "${CMAKE_MATCH_1}")

# ------------------------------------------------------- TypeScript driver ----
# Optional: only runs when tsc, node, and dsdlc are all available (the generated
# TS runtime must be produced by dsdlc first). TS is double-typed like Python, so
# float16 unpack results that are NaN are SKIPPED (JS numbers canonicalize NaN
# payloads); everything else is exact.
set(ts_ran FALSE)
if(DEFINED TSC_EXECUTABLE AND EXISTS "${TSC_EXECUTABLE}"
   AND DEFINED NODE_EXECUTABLE AND EXISTS "${NODE_EXECUTABLE}"
   AND DEFINED DSDLC AND EXISTS "${DSDLC}")
  set(ts_dir "${OUT_DIR}/ts")
  file(MAKE_DIRECTORY "${ts_dir}")
  set(uavcan_root "${SOURCE_ROOT}/submodules/public_regulated_data_types/uavcan")
  execute_process(
    COMMAND "${DSDLC}" --target-language ts -I "${uavcan_root}"
      --outdir "${ts_dir}" "${uavcan_root}/primitive/scalar/Real32.1.0.dsdl" --omit-dependencies
    RESULT_VARIABLE ts_gen_rc OUTPUT_VARIABLE ts_gen_out ERROR_VARIABLE ts_gen_err)
  if(NOT ts_gen_rc EQUAL 0 OR NOT EXISTS "${ts_dir}/dsdl_runtime.ts")
    message(FATAL_ERROR "TS runtime generation failed:\n${ts_gen_out}\n${ts_gen_err}")
  endif()
  file(COPY_FILE "${driver_dir}/PrimitiveEquivalenceDriver.ts" "${ts_dir}/driver.ts")
  file(WRITE "${ts_dir}/tsconfig.json"
    "{\n  \"compilerOptions\": {\n    \"target\": \"ES2022\",\n    \"module\": \"CommonJS\",\n"
    "    \"moduleResolution\": \"Node\",\n    \"strict\": true,\n    \"skipLibCheck\": true,\n"
    "    \"types\": [],\n    \"outDir\": \"./js\"\n  },\n  \"include\": [\"./driver.ts\", \"./dsdl_runtime.ts\"]\n}\n")
  execute_process(
    COMMAND "${TSC_EXECUTABLE}" -p "${ts_dir}/tsconfig.json" --pretty false
    WORKING_DIRECTORY "${ts_dir}"
    RESULT_VARIABLE ts_tsc_rc OUTPUT_VARIABLE ts_tsc_out ERROR_VARIABLE ts_tsc_err)
  if(NOT ts_tsc_rc EQUAL 0)
    message(FATAL_ERROR "TS driver compilation failed:\n${ts_tsc_out}\n${ts_tsc_err}")
  endif()
  file(WRITE "${ts_dir}/js/package.json" "{\n  \"type\": \"commonjs\"\n}\n")
  execute_process(
    COMMAND "${NODE_EXECUTABLE}" "${ts_dir}/js/driver.js" "${VECTORS_FILE}"
    RESULT_VARIABLE ts_rc OUTPUT_VARIABLE ts_out ERROR_VARIABLE ts_err)
  if(NOT ts_rc EQUAL 0)
    message(FATAL_ERROR "TypeScript primitive equivalence FAILED:\n${ts_out}\n${ts_err}")
  endif()
  extract_processed("TypeScript" ts_out ts_count)
  string(REGEX MATCH "SKIPPED ([0-9]+)" _tssk "${ts_out}")
  if(NOT _tssk)
    message(FATAL_ERROR "TS driver did not report a SKIPPED count:\n${ts_out}")
  endif()
  set(ts_skipped "${CMAKE_MATCH_1}")
  set(ts_ran TRUE)
else()
  message(STATUS "primitive equivalence: skipping TypeScript (need tsc, node, and dsdlc)")
endif()

if(c_count LESS 1)
  message(FATAL_ERROR "no vectors were processed")
endif()
if(NOT c_count EQUAL go_count OR NOT c_count EQUAL rs_count)
  message(FATAL_ERROR
    "drivers disagree on processed vector count: C=${c_count} Go=${go_count} Rust=${rs_count}")
endif()
math(EXPR py_total "${py_count} + ${py_skipped}")
if(NOT py_total EQUAL c_count)
  message(FATAL_ERROR
    "Python covered ${py_total} vectors (processed=${py_count} skipped=${py_skipped}) "
    "but the native runtimes ran ${c_count}")
endif()

set(ts_summary "TypeScript=skipped")
if(ts_ran)
  math(EXPR ts_total "${ts_count} + ${ts_skipped}")
  if(NOT ts_total EQUAL c_count)
    message(FATAL_ERROR
      "TypeScript covered ${ts_total} vectors (processed=${ts_count} skipped=${ts_skipped}) "
      "but the native runtimes ran ${c_count}")
  endif()
  set(ts_summary "TypeScript agreed on ${ts_count} (${ts_skipped} double-typed skip(s))")
endif()

message(STATUS
  "Primitive equivalence PASS: C, Rust, Go agreed on all ${c_count} shared vectors; "
  "Python agreed on ${py_count} (${py_skipped} double-typed skip(s)); ${ts_summary}")
file(WRITE "${OUT_DIR}/primitive-equivalence-summary.txt"
  "vectors=${c_count}\npython_skipped=${py_skipped}\n${c_out}${go_out}${rs_out}${py_out}")
