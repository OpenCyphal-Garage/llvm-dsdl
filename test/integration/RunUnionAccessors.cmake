#===----------------------------------------------------------------------===#
#
# Part of the OpenCyphal project, under the MIT licence
# SPDX-License-Identifier: MIT
#
#===----------------------------------------------------------------------===#
#
# The accessors of an equal-length union, in every language.
#
# A union whose options are all flat and of one length has its tag at a fixed offset and every
# option after it, so it gets accessors. This lane generates the union fixture for every
# target, builds a probe against the output and nothing else -- under warnings-as-errors,
# `-D warnings`, `go vet` and `noUnusedLocals` -- and holds the accessors to the decode: the tag
# and the selected option read what the decode holds, a composite option answers its bytes, a
# short buffer reads the tag and zeros, and the tag setter selects an option for the decode.
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

set(fixture_root "${SOURCE_ROOT}/test/lit/fixtures_union")
set(probe_dir "${SOURCE_ROOT}/test/integration")
set(probe "${probe_dir}/UnionAccessorsProbe.c")
file(REMOVE_RECURSE "${OUT_DIR}")
file(MAKE_DIRECTORY "${OUT_DIR}")
set(legs "")

# Generates the fixture for one target under the mode. ARGN holds the target's own options.
function(_union_generate language out)
  execute_process(
    COMMAND "${DSDLC}" --target-language ${language} "${fixture_root}"
      --outdir "${out}" ${ARGN}
    RESULT_VARIABLE gen_result
    OUTPUT_VARIABLE gen_stdout
    ERROR_VARIABLE gen_stderr
  )
  if(NOT gen_result EQUAL 0)
    message(STATUS "dsdlc stdout:\n${gen_stdout}")
    message(STATUS "dsdlc stderr:\n${gen_stderr}")
    message(FATAL_ERROR "${language} generation failed")
  endif()
endfunction()

# Runs one probe and fails the lane unless it exits nought and reports ok.
function(_union_run label)
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
  if(NOT run_result EQUAL 0 OR NOT run_out MATCHES "union-accessors [^:]+: ok")
    message(FATAL_ERROR "${label} probe failed")
  endif()
endfunction()

# Generates the fixture for one target under the mode, then compiles and runs the probe against
# the output. ARGN holds the generated inputs the probe links with.
function(_union_leg language label)
  set(out "${OUT_DIR}/${language}")
  execute_process(
    COMMAND "${DSDLC}" --target-language ${language} "${fixture_root}"
      --outdir "${out}"
    RESULT_VARIABLE gen_result
    OUTPUT_VARIABLE gen_stdout
    ERROR_VARIABLE gen_stderr
  )
  if(NOT gen_result EQUAL 0)
    message(STATUS "dsdlc stdout:\n${gen_stdout}")
    message(STATUS "dsdlc stderr:\n${gen_stderr}")
    message(FATAL_ERROR "${label} generation failed")
  endif()
  file(GLOB_RECURSE inputs ${ARGN})
  if("${inputs}" STREQUAL "")
    message(FATAL_ERROR "${label} generation wrote nothing to link the probe with")
  endif()
  execute_process(
    COMMAND "${C_COMPILER}" -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror "-DTARGET_NAME=\"${label}\""
      -I "${out}" "${probe}" ${inputs} -o "${OUT_DIR}/probe_${language}"
    RESULT_VARIABLE build_result
    OUTPUT_VARIABLE build_stdout
    ERROR_VARIABLE build_stderr
  )
  if(NOT build_result EQUAL 0)
    message(FATAL_ERROR "${label} probe build failed:\n${build_stdout}\n${build_stderr}")
  endif()
  execute_process(
    COMMAND "${OUT_DIR}/probe_${language}"
    RESULT_VARIABLE run_result
    OUTPUT_VARIABLE run_out
    ERROR_VARIABLE run_err
  )
  message(STATUS "${label}:\n${run_out}")
  if(NOT run_err STREQUAL "")
    message(STATUS "${label} stderr:\n${run_err}")
  endif()
  if(NOT run_result EQUAL 0 OR NOT run_out MATCHES "union-accessors ${label}: ok")
    message(FATAL_ERROR "${label} probe failed")
  endif()
endfunction()

_union_leg(c "C" "${OUT_DIR}/c/*.c")
_union_leg(obj "object" "${OUT_DIR}/obj/*.o")
list(APPEND legs "C" "object")

# --------------------------------------------------------------------- C++ ----
set(cpp_out "${OUT_DIR}/cpp")
_union_generate(cpp "${cpp_out}")
foreach(profile std pmr)
  execute_process(
    COMMAND "${CXX_COMPILER}" -std=c++20 -O2 -Wall -Wextra -Wpedantic -Werror -I "${cpp_out}/${profile}"
      "${probe_dir}/UnionAccessorsProbe.cpp" -o "${OUT_DIR}/probe_cpp_${profile}"
    RESULT_VARIABLE build_result
    OUTPUT_VARIABLE build_stdout
    ERROR_VARIABLE build_stderr
  )
  if(NOT build_result EQUAL 0)
    message(FATAL_ERROR "C++ ${profile} probe build failed:\n${build_stdout}\n${build_stderr}")
  endif()
  _union_run("C++ ${profile}" "${OUT_DIR}/probe_cpp_${profile}")
endforeach()
list(APPEND legs "C++")

# -------------------------------------------------------------------- Rust ----
if(DEFINED CARGO_EXECUTABLE AND EXISTS "${CARGO_EXECUTABLE}")
  set(RUST_OUT "${OUT_DIR}/rust")
  _union_generate(rust "${RUST_OUT}")
  set(harness_out "${OUT_DIR}/rust-harness")
  file(MAKE_DIRECTORY "${harness_out}/src")
  configure_file("${probe_dir}/UnionAccessorsCargo.toml.in" "${harness_out}/Cargo.toml" @ONLY)
  configure_file("${probe_dir}/UnionAccessorsMain.rs" "${harness_out}/src/main.rs" COPYONLY)
  _union_run("Rust"
    "${CMAKE_COMMAND}" -E env "CARGO_TARGET_DIR=${OUT_DIR}/cargo-target" "RUSTFLAGS=-D warnings"
    "${CARGO_EXECUTABLE}" run --quiet --manifest-path "${harness_out}/Cargo.toml")
  list(APPEND legs "Rust")
else()
  message(STATUS "Rust: cargo not given; leg skipped")
endif()

# ---------------------------------------------------------------------- Go ----
if(DEFINED GO_EXECUTABLE AND EXISTS "${GO_EXECUTABLE}")
  set(go_out "${OUT_DIR}/go")
  _union_generate(go "${go_out}")
  file(MAKE_DIRECTORY "${go_out}/probe")
  configure_file("${probe_dir}/UnionAccessorsMain.go" "${go_out}/probe/main.go" COPYONLY)
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
  _union_run("Go" "${OUT_DIR}/probe_go")
  list(APPEND legs "Go")
else()
  message(STATUS "Go: go not given; leg skipped")
endif()

# -------------------------------------------------------------- TypeScript ----
if(DEFINED TSC_EXECUTABLE AND EXISTS "${TSC_EXECUTABLE}"
   AND DEFINED NODE_EXECUTABLE AND EXISTS "${NODE_EXECUTABLE}")
  set(ts_out "${OUT_DIR}/ts")
  _union_generate(ts "${ts_out}")
  configure_file("${probe_dir}/UnionAccessorsProbe.ts" "${ts_out}/probe.ts" COPYONLY)
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
  _union_run("TypeScript" "${NODE_EXECUTABLE}" "${ts_out}/js/probe.js")
  list(APPEND legs "TypeScript")
else()
  message(STATUS "TypeScript: tsc or node not given; leg skipped")
endif()

# ------------------------------------------------------------------ Python ----
if(DEFINED PYTHON_EXECUTABLE AND EXISTS "${PYTHON_EXECUTABLE}")
  set(py_out "${OUT_DIR}/python")
  _union_generate(python "${py_out}")
  _union_run("Python" "${PYTHON_EXECUTABLE}" "${probe_dir}/UnionAccessorsProbe.py" "${py_out}")
  list(APPEND legs "Python")
else()
  message(STATUS "Python: python3 not given; leg skipped")
endif()

list(JOIN legs ", " legs_text)
message(STATUS "union accessors passed; legs run: ${legs_text}")
