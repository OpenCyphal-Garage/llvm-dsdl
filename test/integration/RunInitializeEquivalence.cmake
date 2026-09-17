#===----------------------------------------------------------------------===#
#
# Part of the OpenCyphal project, under the MIT licence
# SPDX-License-Identifier: MIT
#
#===----------------------------------------------------------------------===#
#
# The default of a type, in every language, against the specification's definition of it.
#
# The specification defines a default as the object deserialised from nothing: every scalar nought,
# every array empty, a union at its first option. Each backend spells the default its own way -- a
# C function, C++ member initialisers, Rust's `Default`, Go's zero value, a TypeScript factory, a
# Python dataclass -- and each spelling is a translation of the same initialise body. This lane
# holds every spelling to the definition: for seven regulated types, the default serialised must
# equal the default deserialised from nothing and serialised. The types cover scalars, nested
# composites, fixed and variable arrays of scalars, bools and composites, and unions of each.
#
# Every language whose toolchain was given runs; C and C++ always do.
#
cmake_minimum_required(VERSION 3.24)

foreach(var DSDLC SOURCE_ROOT UAVCAN_ROOT OUT_DIR C_COMPILER CXX_COMPILER)
  if(NOT DEFINED ${var} OR "${${var}}" STREQUAL "")
    message(FATAL_ERROR "Missing required variable: ${var}")
  endif()
endforeach()
foreach(tool "${DSDLC}" "${C_COMPILER}" "${CXX_COMPILER}")
  if(NOT EXISTS "${tool}")
    message(FATAL_ERROR "tool not found: ${tool}")
  endif()
endforeach()
if(NOT EXISTS "${UAVCAN_ROOT}")
  message(FATAL_ERROR "uavcan root not found: ${UAVCAN_ROOT}")
endif()

set(driver_dir "${SOURCE_ROOT}/test/integration")
file(REMOVE_RECURSE "${OUT_DIR}")
file(MAKE_DIRECTORY "${OUT_DIR}")

set(legs "")

# ------------------------------------------------------------------ shared ----

# Generates the regulated types for one language. ARGN holds the language's own options.
function(_equivalence_generate language out_dir)
  execute_process(
    COMMAND "${DSDLC}" --target-language ${language} "${UAVCAN_ROOT}" --outdir "${out_dir}" ${ARGN}
    RESULT_VARIABLE gen_result
    OUTPUT_VARIABLE gen_stdout
    ERROR_VARIABLE gen_stderr
  )
  if(NOT gen_result EQUAL 0)
    message(STATUS "dsdlc stdout:\n${gen_stdout}")
    message(STATUS "dsdlc stderr:\n${gen_stderr}")
    message(FATAL_ERROR "uavcan ${language} generation failed")
  endif()
endfunction()

# Runs one driver and fails the lane unless it exits nought and reports every type the same.
set(equivalence_types
  "uavcan.node.Heartbeat"
  "uavcan.metatransport.can.Frame"
  "uavcan.primitive.array.Real32"
  "uavcan.node.port.SubjectIDList"
  "uavcan.pnp.NodeIDAllocationData"
  "uavcan.diagnostic.Record"
  "uavcan.time.SynchronizedTimestamp"
)
function(_equivalence_run label)
  execute_process(
    COMMAND ${ARGN}
    RESULT_VARIABLE run_result
    OUTPUT_VARIABLE run_out
    ERROR_VARIABLE run_err
  )
  message(STATUS "${label}:\n${run_out}")
  if(NOT run_err STREQUAL "")
    message(STATUS "${label} stderr:\n${run_err}")
  endif()
  if(NOT run_result EQUAL 0)
    message(FATAL_ERROR "${label} failed")
  endif()
  foreach(type IN LISTS equivalence_types)
    if(NOT run_out MATCHES "${type} +same")
      message(FATAL_ERROR "${label}: ${type} is not reported the same")
    endif()
  endforeach()
endfunction()

# ----------------------------------------------------------------------- C ----
set(c_out "${OUT_DIR}/c")
_equivalence_generate(c "${c_out}")
configure_file("${driver_dir}/InitializeEquivalenceDriver.c" "${OUT_DIR}/c_driver.c" @ONLY)
file(GLOB_RECURSE c_sources "${c_out}/*.c")
execute_process(
  COMMAND "${C_COMPILER}" -std=c11 -O2 -Wall -Wextra -I "${c_out}" "${OUT_DIR}/c_driver.c" ${c_sources}
    -o "${OUT_DIR}/c_equivalence"
  RESULT_VARIABLE build_result
  OUTPUT_VARIABLE build_stdout
  ERROR_VARIABLE build_stderr
)
if(NOT build_result EQUAL 0)
  message(FATAL_ERROR "C build failed:\n${build_stdout}\n${build_stderr}")
endif()
_equivalence_run("C" "${OUT_DIR}/c_equivalence")
list(APPEND legs "C")

# --------------------------------------------------------------------- C++ ----
set(cpp_out "${OUT_DIR}/cpp")
_equivalence_generate(cpp "${cpp_out}")
configure_file("${driver_dir}/InitializeEquivalenceDriver.cpp" "${OUT_DIR}/cpp_driver.cpp" @ONLY)
execute_process(
  COMMAND "${CXX_COMPILER}" -std=c++17 -O2 -Wall -Wextra -I "${cpp_out}/std" "${OUT_DIR}/cpp_driver.cpp"
    -o "${OUT_DIR}/cpp_equivalence"
  RESULT_VARIABLE build_result
  OUTPUT_VARIABLE build_stdout
  ERROR_VARIABLE build_stderr
)
if(NOT build_result EQUAL 0)
  message(FATAL_ERROR "C++ build failed:\n${build_stdout}\n${build_stderr}")
endif()
_equivalence_run("C++" "${OUT_DIR}/cpp_equivalence")
list(APPEND legs "C++")

# -------------------------------------------------------------------- Rust ----
if(DEFINED CARGO_EXECUTABLE AND EXISTS "${CARGO_EXECUTABLE}")
  set(RUST_OUT "${OUT_DIR}/rust")
  _equivalence_generate(rust "${RUST_OUT}" --rust-crate-name uavcan_dsdl_generated)
  set(harness_out "${OUT_DIR}/rust-harness")
  file(MAKE_DIRECTORY "${harness_out}/src")
  configure_file("${driver_dir}/InitializeEquivalenceCargo.toml.in" "${harness_out}/Cargo.toml" @ONLY)
  configure_file("${driver_dir}/InitializeEquivalenceMain.rs" "${harness_out}/src/main.rs" @ONLY)
  _equivalence_run("Rust"
    "${CMAKE_COMMAND}" -E env "CARGO_TARGET_DIR=${OUT_DIR}/cargo-target"
    "${CARGO_EXECUTABLE}" run --quiet --manifest-path "${harness_out}/Cargo.toml")
  list(APPEND legs "Rust")
else()
  message(STATUS "Rust: cargo not given; leg skipped")
endif()

# ---------------------------------------------------------------------- Go ----
if(DEFINED GO_EXECUTABLE AND EXISTS "${GO_EXECUTABLE}")
  set(go_out "${OUT_DIR}/go")
  _equivalence_generate(go "${go_out}" --go-module uavcan_dsdl_generated)
  file(MAKE_DIRECTORY "${go_out}/initializeequivalence")
  configure_file("${driver_dir}/InitializeEquivalenceMain.go" "${go_out}/initializeequivalence/main.go" @ONLY)
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
      "GOCACHE=${OUT_DIR}/.gocache" "GOMODCACHE=${OUT_DIR}/.gomodcache" "GOFLAGS=-mod=mod"
      "${GO_EXECUTABLE}" build -buildvcs=false -o "${OUT_DIR}/go_equivalence" ./initializeequivalence/
    WORKING_DIRECTORY "${go_out}"
    RESULT_VARIABLE build_result
    OUTPUT_VARIABLE build_stdout
    ERROR_VARIABLE build_stderr
  )
  if(NOT build_result EQUAL 0)
    message(FATAL_ERROR "Go build failed:\n${build_stdout}\n${build_stderr}")
  endif()
  _equivalence_run("Go" "${OUT_DIR}/go_equivalence")
  list(APPEND legs "Go")
else()
  message(STATUS "Go: go not given; leg skipped")
endif()

# -------------------------------------------------------------- TypeScript ----
if(DEFINED TSC_EXECUTABLE AND EXISTS "${TSC_EXECUTABLE}"
   AND DEFINED NODE_EXECUTABLE AND EXISTS "${NODE_EXECUTABLE}")
  set(ts_out "${OUT_DIR}/ts")
  _equivalence_generate(ts "${ts_out}")
  configure_file("${driver_dir}/InitializeEquivalenceDriver.ts" "${ts_out}/driver.ts" @ONLY)
  file(WRITE "${ts_out}/tsconfig.json"
    "{\n  \"compilerOptions\": {\n    \"target\": \"ES2022\",\n    \"module\": \"CommonJS\",\n"
    "    \"moduleResolution\": \"Node\",\n    \"strict\": true,\n    \"skipLibCheck\": true,\n"
    "    \"types\": [],\n    \"outDir\": \"./js\"\n  },\n  \"include\": [\"./driver.ts\"]\n}\n")
  execute_process(
    COMMAND "${TSC_EXECUTABLE}" -p "${ts_out}/tsconfig.json" --pretty false
    WORKING_DIRECTORY "${ts_out}"
    RESULT_VARIABLE tsc_result
    OUTPUT_VARIABLE tsc_stdout
    ERROR_VARIABLE tsc_stderr
  )
  if(NOT tsc_result EQUAL 0)
    message(FATAL_ERROR "TypeScript driver compilation failed:\n${tsc_stdout}\n${tsc_stderr}")
  endif()
  file(WRITE "${ts_out}/js/package.json" "{\n  \"type\": \"commonjs\"\n}\n")
  _equivalence_run("TypeScript" "${NODE_EXECUTABLE}" "${ts_out}/js/driver.js")
  list(APPEND legs "TypeScript")
else()
  message(STATUS "TypeScript: tsc or node not given; leg skipped")
endif()

# ------------------------------------------------------------------ Python ----
if(DEFINED PYTHON_EXECUTABLE AND EXISTS "${PYTHON_EXECUTABLE}")
  set(py_out "${OUT_DIR}/python")
  set(PY_PACKAGE "uavcan_dsdl_generated_py")
  _equivalence_generate(python "${py_out}" --py-package "${PY_PACKAGE}")
  _equivalence_run("Python"
    "${PYTHON_EXECUTABLE}" "${driver_dir}/InitializeEquivalenceDriver.py" "${py_out}" "${PY_PACKAGE}")
  list(APPEND legs "Python")
else()
  message(STATUS "Python: python3 not given; leg skipped")
endif()

list(JOIN legs ", " legs_text)
message(STATUS "initialise equivalence passed; legs run: ${legs_text}")
