#===----------------------------------------------------------------------===#
#
# Part of the OpenCyphal project, under the MIT licence
# SPDX-License-Identifier: MIT
#
#===----------------------------------------------------------------------===#
#
# An accessors-only run's output, compiled standalone in every language and read.
#
# `--aliasable-only` emits each type's field accessors and neither its object type nor its
# serialisation. This lane generates the `@aliasable` fixture in that mode for every target, builds
# a probe against the output and nothing else -- under warnings-as-errors, `go vet` and
# `noUnusedLocals` -- and reads a known buffer through the outer type's composite getter and the
# inner type's field getter, a setter round trip and a short read.
#
# Every language whose toolchain was given runs; C, the object target and C++ always do.
#
cmake_minimum_required(VERSION 3.24)

foreach(var DSDLC SOURCE_ROOT OUT_DIR C_COMPILER CXX_COMPILER)
  if(NOT DEFINED ${var} OR "${${var}}" STREQUAL "")
    message(FATAL_ERROR "Missing required variable: ${var}")
  endif()
endforeach()
foreach(tool "${DSDLC}" "${C_COMPILER}" "${CXX_COMPILER}")
  if(NOT EXISTS "${tool}")
    message(FATAL_ERROR "tool not found: ${tool}")
  endif()
endforeach()

set(fixture_root "${SOURCE_ROOT}/test/lit/fixtures_aliasable")
set(probe_dir "${SOURCE_ROOT}/test/integration")
file(REMOVE_RECURSE "${OUT_DIR}")
file(MAKE_DIRECTORY "${OUT_DIR}")

set(legs "")

# ------------------------------------------------------------------ shared ----

# Generates the fixture for one target in accessors-only mode. ARGN holds the target's own options.
function(_aliasable_only_generate language out_dir)
  execute_process(
    COMMAND "${DSDLC}" --target-language ${language} --aliasable-only "${fixture_root}" --outdir "${out_dir}" ${ARGN}
    RESULT_VARIABLE gen_result
    OUTPUT_VARIABLE gen_stdout
    ERROR_VARIABLE gen_stderr
  )
  if(NOT gen_result EQUAL 0)
    message(STATUS "dsdlc stdout:\n${gen_stdout}")
    message(STATUS "dsdlc stderr:\n${gen_stderr}")
    message(FATAL_ERROR "${language} accessors-only generation failed")
  endif()
endfunction()

# Runs one probe and fails the lane unless it exits nought and reports ok.
function(_aliasable_only_run label)
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
  if(NOT run_out MATCHES "aliasable-only [^:]+: ok")
    message(FATAL_ERROR "${label}: the probe did not report ok")
  endif()
endfunction()

# Compiles one C or C++ probe. ARGN holds the compiler's arguments after the flags.
function(_aliasable_only_compile label compiler)
  execute_process(
    COMMAND "${compiler}" ${ARGN}
    RESULT_VARIABLE build_result
    OUTPUT_VARIABLE build_stdout
    ERROR_VARIABLE build_stderr
  )
  if(NOT build_result EQUAL 0)
    message(FATAL_ERROR "${label} build failed:\n${build_stdout}\n${build_stderr}")
  endif()
endfunction()

# ----------------------------------------------------------------------- C ----
set(c_out "${OUT_DIR}/c")
_aliasable_only_generate(c "${c_out}")
file(GLOB_RECURSE c_sources "${c_out}/*.c")
_aliasable_only_compile("C" "${C_COMPILER}"
  -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror -I "${c_out}"
  "${probe_dir}/AliasableOnlyProbe.c" ${c_sources} -o "${OUT_DIR}/probe_c")
_aliasable_only_run("C" "${OUT_DIR}/probe_c")
list(APPEND legs "C")

# ------------------------------------------------------------------ object ----
set(obj_out "${OUT_DIR}/obj")
_aliasable_only_generate(obj "${obj_out}")
file(GLOB_RECURSE obj_objects "${obj_out}/*.o")
if(obj_objects STREQUAL "")
  message(FATAL_ERROR "the object target wrote no object")
endif()
_aliasable_only_compile("object" "${C_COMPILER}"
  -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror -I "${obj_out}"
  "${probe_dir}/AliasableOnlyProbe.c" ${obj_objects} -o "${OUT_DIR}/probe_obj")
_aliasable_only_run("object" "${OUT_DIR}/probe_obj")
list(APPEND legs "object")

# --------------------------------------------------------------------- C++ ----
set(cpp_out "${OUT_DIR}/cpp")
_aliasable_only_generate(cpp "${cpp_out}")
foreach(profile std pmr)
  _aliasable_only_compile("C++ ${profile}" "${CXX_COMPILER}"
    -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -I "${cpp_out}/${profile}"
    "${probe_dir}/AliasableOnlyProbe.cpp" -o "${OUT_DIR}/probe_cpp_${profile}")
  _aliasable_only_run("C++ ${profile}" "${OUT_DIR}/probe_cpp_${profile}")
endforeach()
list(APPEND legs "C++")

# -------------------------------------------------------------------- Rust ----
if(DEFINED CARGO_EXECUTABLE AND EXISTS "${CARGO_EXECUTABLE}")
  set(RUST_OUT "${OUT_DIR}/rust")
  _aliasable_only_generate(rust "${RUST_OUT}" --rust-crate-name aliasable_only_generated)
  set(harness_out "${OUT_DIR}/rust-harness")
  file(MAKE_DIRECTORY "${harness_out}/src")
  configure_file("${probe_dir}/AliasableOnlyCargo.toml.in" "${harness_out}/Cargo.toml" @ONLY)
  configure_file("${probe_dir}/AliasableOnlyMain.rs" "${harness_out}/src/main.rs" COPYONLY)
  _aliasable_only_run("Rust"
    "${CMAKE_COMMAND}" -E env "CARGO_TARGET_DIR=${OUT_DIR}/cargo-target" "RUSTFLAGS=-D warnings"
    "${CARGO_EXECUTABLE}" run --quiet --manifest-path "${harness_out}/Cargo.toml")
  list(APPEND legs "Rust")
else()
  message(STATUS "Rust: cargo not given; leg skipped")
endif()

# ---------------------------------------------------------------------- Go ----
if(DEFINED GO_EXECUTABLE AND EXISTS "${GO_EXECUTABLE}")
  set(go_out "${OUT_DIR}/go")
  _aliasable_only_generate(go "${go_out}" --go-module aliasable_only_generated)
  file(MAKE_DIRECTORY "${go_out}/probe")
  configure_file("${probe_dir}/AliasableOnlyMain.go" "${go_out}/probe/main.go" COPYONLY)
  foreach(step "vet ./..." "build -buildvcs=false -o ${OUT_DIR}/probe_go ./probe/")
    separate_arguments(step_args NATIVE_COMMAND "${step}")
    execute_process(
      COMMAND "${CMAKE_COMMAND}" -E env
        "GOCACHE=${OUT_DIR}/.gocache" "GOMODCACHE=${OUT_DIR}/.gomodcache" "GOFLAGS=-mod=mod"
        "${GO_EXECUTABLE}" ${step_args}
      WORKING_DIRECTORY "${go_out}"
      RESULT_VARIABLE build_result
      OUTPUT_VARIABLE build_stdout
      ERROR_VARIABLE build_stderr
    )
    if(NOT build_result EQUAL 0)
      message(FATAL_ERROR "go ${step} failed:\n${build_stdout}\n${build_stderr}")
    endif()
  endforeach()
  _aliasable_only_run("Go" "${OUT_DIR}/probe_go")
  list(APPEND legs "Go")
else()
  message(STATUS "Go: go not given; leg skipped")
endif()

# -------------------------------------------------------------- TypeScript ----
if(DEFINED TSC_EXECUTABLE AND EXISTS "${TSC_EXECUTABLE}"
   AND DEFINED NODE_EXECUTABLE AND EXISTS "${NODE_EXECUTABLE}")
  set(ts_out "${OUT_DIR}/ts")
  _aliasable_only_generate(ts "${ts_out}")
  configure_file("${probe_dir}/AliasableOnlyProbe.ts" "${ts_out}/probe.ts" COPYONLY)
  file(WRITE "${ts_out}/tsconfig.json"
    "{\n  \"compilerOptions\": {\n    \"target\": \"ES2022\",\n    \"module\": \"CommonJS\",\n"
    "    \"moduleResolution\": \"Node\",\n    \"strict\": true,\n    \"noUnusedLocals\": true,\n"
    "    \"skipLibCheck\": true,\n    \"types\": [],\n    \"outDir\": \"./js\"\n  },\n"
    "  \"include\": [\"./probe.ts\"]\n}\n")
  execute_process(
    COMMAND "${TSC_EXECUTABLE}" -p "${ts_out}/tsconfig.json" --pretty false
    WORKING_DIRECTORY "${ts_out}"
    RESULT_VARIABLE tsc_result
    OUTPUT_VARIABLE tsc_stdout
    ERROR_VARIABLE tsc_stderr
  )
  if(NOT tsc_result EQUAL 0)
    message(FATAL_ERROR "TypeScript probe compilation failed:\n${tsc_stdout}\n${tsc_stderr}")
  endif()
  file(WRITE "${ts_out}/js/package.json" "{\n  \"type\": \"commonjs\"\n}\n")
  _aliasable_only_run("TypeScript" "${NODE_EXECUTABLE}" "${ts_out}/js/probe.js")
  list(APPEND legs "TypeScript")
else()
  message(STATUS "TypeScript: tsc or node not given; leg skipped")
endif()

# ------------------------------------------------------------------ Python ----
if(DEFINED PYTHON_EXECUTABLE AND EXISTS "${PYTHON_EXECUTABLE}")
  set(py_out "${OUT_DIR}/python")
  set(PY_PACKAGE "aliasable_only_generated_py")
  _aliasable_only_generate(python "${py_out}" --py-package "${PY_PACKAGE}")
  _aliasable_only_run("Python"
    "${PYTHON_EXECUTABLE}" "${probe_dir}/AliasableOnlyProbe.py" "${py_out}" "${PY_PACKAGE}")
  list(APPEND legs "Python")
else()
  message(STATUS "Python: python3 not given; leg skipped")
endif()

list(JOIN legs ", " legs_text)
message(STATUS "aliasable-only passed; legs run: ${legs_text}")
