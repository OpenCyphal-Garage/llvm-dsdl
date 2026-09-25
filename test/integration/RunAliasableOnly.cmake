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
# serialisation. This lane generates the `@aliasable` fixtures in that mode for every target, builds
# a probe against the output and nothing else -- under warnings-as-errors, `go vet` and
# `noUnusedLocals` -- and reads a known buffer through the outer type's composite getter and the
# inner type's field getter, a setter round trip and a short read.
#
# The probe reads one type; the mode's promise is about every file a run writes. So each leg also
# compiles what the probe does not reach: C, the object target and C++ include every header they
# wrote, TypeScript type-checks every file, and Python imports every module. Rust and Go build the
# whole crate and module as they are.
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

# Three roots, because the mode's promise is about every file a run writes and one root reaches
# too little of it. `fixtures_aliasable` is a message nesting a message in its own namespace, which
# the probes read; `fixtures_aliasable_service_flat` is a service, reached through an alias whose
# wrappers call the serialisation this mode omits; `fixtures_aliasable_xns` nests across
# namespaces, which is what makes Go write an import.
set(fixture_roots
  "${SOURCE_ROOT}/test/lit/fixtures_aliasable"
  "${SOURCE_ROOT}/test/lit/fixtures_aliasable_service_flat"
  "${SOURCE_ROOT}/test/lit/fixtures_aliasable_xns")
set(probe_dir "${SOURCE_ROOT}/test/integration")
file(REMOVE_RECURSE "${OUT_DIR}")
file(MAKE_DIRECTORY "${OUT_DIR}")

set(legs "")

# ------------------------------------------------------------------ shared ----

# Generates the fixture for one target in accessors-only mode. ARGN holds the target's own options.
function(_aliasable_only_generate language out_dir)
  execute_process(
    COMMAND "${DSDLC}" --target-language ${language} --aliasable-only ${fixture_roots} --outdir "${out_dir}" ${ARGN}
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

# Writes a translation unit including every generated header under @p root.
#
# A probe reads one type, so it reaches one type's header. What the mode promises is that every
# file a run writes compiles, and a header nothing includes is a header nothing checks: that is how
# a service's alias kept wrappers calling the serialisation this mode leaves out.
function(_aliasable_only_sweep out_file root extension)
  file(GLOB_RECURSE headers "${root}/*.${extension}")
  list(SORT headers)
  if(headers STREQUAL "")
    message(FATAL_ERROR "no .${extension} under ${root} to sweep")
  endif()
  set(lines "")
  foreach(header IN LISTS headers)
    file(RELATIVE_PATH relative "${root}" "${header}")
    string(APPEND lines "#include \"${relative}\"\n")
  endforeach()
  file(WRITE "${out_file}" "/* Written by RunAliasableOnly.cmake: every header this run wrote. */\n${lines}")
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
_aliasable_only_sweep("${OUT_DIR}/sweep_c.c" "${c_out}" "h")
_aliasable_only_compile("C headers" "${C_COMPILER}"
  -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror -I "${c_out}"
  -c "${OUT_DIR}/sweep_c.c" -o "${OUT_DIR}/sweep_c.o")
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
_aliasable_only_sweep("${OUT_DIR}/sweep_obj.c" "${obj_out}" "h")
_aliasable_only_compile("object headers" "${C_COMPILER}"
  -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror -I "${obj_out}"
  -c "${OUT_DIR}/sweep_obj.c" -o "${OUT_DIR}/sweep_obj.o")
list(APPEND legs "object")

# --------------------------------------------------------------------- C++ ----
set(cpp_out "${OUT_DIR}/cpp")
_aliasable_only_generate(cpp "${cpp_out}")
foreach(profile std pmr)
  _aliasable_only_compile("C++ ${profile}" "${CXX_COMPILER}"
    -std=c++20 -O2 -Wall -Wextra -Wpedantic -Werror -I "${cpp_out}/${profile}"
    "${probe_dir}/AliasableOnlyProbe.cpp" -o "${OUT_DIR}/probe_cpp_${profile}")
  _aliasable_only_run("C++ ${profile}" "${OUT_DIR}/probe_cpp_${profile}")
  _aliasable_only_sweep("${OUT_DIR}/sweep_cpp_${profile}.cpp" "${cpp_out}/${profile}" "hpp")
  _aliasable_only_compile("C++ ${profile} headers" "${CXX_COMPILER}"
    -std=c++20 -O2 -Wall -Wextra -Wpedantic -Werror -I "${cpp_out}/${profile}"
    -c "${OUT_DIR}/sweep_cpp_${profile}.cpp" -o "${OUT_DIR}/sweep_cpp_${profile}.o")
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
    "  \"include\": [\"./**/*.ts\"]\n}\n")
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
  # Every generated module is imported, for the reason the C and C++ header sweeps exist: the probe
  # reaches one type, and a module nothing imports is a module nothing runs.
  execute_process(
    COMMAND "${PYTHON_EXECUTABLE}" -c
      "import importlib, pkgutil, sys; sys.path.insert(0, sys.argv[1]); root = importlib.import_module(sys.argv[2]); [importlib.import_module(found.name) for found in pkgutil.walk_packages(root.__path__, root.__name__ + '.')]"
      "${py_out}" "${PY_PACKAGE}"
    RESULT_VARIABLE py_sweep_result
    OUTPUT_VARIABLE py_sweep_stdout
    ERROR_VARIABLE py_sweep_stderr
  )
  if(NOT py_sweep_result EQUAL 0)
    message(FATAL_ERROR "Python: importing every generated module failed:\n${py_sweep_stdout}\n${py_sweep_stderr}")
  endif()
  list(APPEND legs "Python")
else()
  message(STATUS "Python: python3 not given; leg skipped")
endif()

list(JOIN legs ", " legs_text)
message(STATUS "aliasable-only passed; legs run: ${legs_text}")
