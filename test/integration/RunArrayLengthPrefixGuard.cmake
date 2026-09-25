#===----------------------------------------------------------------------===#
#
# Part of the OpenCyphal project, under the MIT licence
# SPDX-License-Identifier: MIT
#
#===----------------------------------------------------------------------===#
#
# Array-length prefixes above the declared capacity and beyond the index width of the target,
# decoded by every backend.
#
# Generates the array_length_prefix fixtures for each language whose toolchain was given and runs
# one driver per language on the host. Every driver reports the same twelve cases with the same
# markers, so one check reads them all. Where a 32-bit toolchain can run code the driver runs
# there as well: Go for linux/386 on an x86 Linux host, and C and C++ under -m32 when the compiler
# links and runs a 32-bit program. Rust is type-checked for each 32-bit target rustup has
# installed. REQUIRE_32BIT_EXECUTION=ON fails the lane when no 32-bit leg executed.
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

set(fixture_root "${SOURCE_ROOT}/test/integration/fixtures/array_length_prefix/prefixguard")
set(driver_dir "${SOURCE_ROOT}/test/integration")
if(NOT EXISTS "${fixture_root}")
  message(FATAL_ERROR "array_length_prefix fixture root not found: ${fixture_root}")
endif()

file(REMOVE_RECURSE "${OUT_DIR}")
file(MAKE_DIRECTORY "${OUT_DIR}")

# A single-version fixture corpus, so either scheme generates. The default follows the tool's.
include("${SOURCE_ROOT}/cmake/HarnessTypeNameTokens.cmake")
llvmdsdl_harness_naming_scheme(C_DEFAULT "unversioned" OTHER_DEFAULT "unversioned")

execute_process(
  COMMAND "${DSDLC}" --version
  RESULT_VARIABLE dsdlc_version_result
  OUTPUT_VARIABLE dsdlc_version_stdout
  ERROR_VARIABLE dsdlc_version_stderr
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(NOT dsdlc_version_result EQUAL 0)
  message(FATAL_ERROR "dsdlc --version failed:\n${dsdlc_version_stdout}\n${dsdlc_version_stderr}")
endif()
string(REGEX REPLACE "^dsdlc[ \t]+([0-9]+\\.[0-9]+\\.[0-9]+)$" "\\1" LLVMDSDL_TOOL_VERSION "${dsdlc_version_stdout}")
if(NOT LLVMDSDL_TOOL_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
  message(FATAL_ERROR "unexpected dsdlc --version output: '${dsdlc_version_stdout}'")
endif()

cmake_host_system_information(RESULT host_os QUERY OS_NAME)
cmake_host_system_information(RESULT host_platform QUERY OS_PLATFORM)

set(executed_32bit FALSE)
set(legs "")

# ------------------------------------------------------------------ shared ----

# Generates the fixtures for one language. ARGN holds the language's own options.
function(_guard_generate language out_dir)
  execute_process(
    COMMAND "${DSDLC}" --target-language ${language} "${fixture_root}" --outdir "${out_dir}" ${ARGN}
    RESULT_VARIABLE gen_result
    OUTPUT_VARIABLE gen_stdout
    ERROR_VARIABLE gen_stderr
  )
  if(NOT gen_result EQUAL 0)
    message(STATUS "dsdlc stdout:\n${gen_stdout}")
    message(STATUS "dsdlc stderr:\n${gen_stderr}")
    message(FATAL_ERROR "array_length_prefix ${language} generation failed")
  endif()
endfunction()

# Runs one command, keeps its stdout in run_stdout, and fails the lane on a non-zero exit.
function(_guard_run label)
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
  set(run_stdout "${run_out}" PARENT_SCOPE)
endfunction()

# The cases every driver rejects on every target, and the two decodes that have to succeed.
set(guard_required_markers
  "PASS prefix32_rejects_above_capacity prefix=65537"
  "PASS prefix32_rejects_above_capacity prefix=2147483648"
  "PASS prefix32_rejects_above_capacity prefix=4294967295"
  "PASS prefix32_accepts_capacity prefix=65536"
  "PASS prefix64_rejects_above_capacity prefix=8589934593"
  "PASS prefix64_rejects_above_capacity prefix=8589934595"
  "PASS prefix64_rejects_above_capacity prefix=9223372036854775808"
  "PASS prefix64_rejects_above_capacity prefix=18446744073709551615"
  "PASS prefix64_accepts_small_length prefix=3"
)

# Checks one driver's output. A 32-bit index has to reject the lengths beyond it; a wider one
# reports them skipped.
function(_guard_check label output narrow)
  foreach(marker IN LISTS guard_required_markers)
    string(FIND "${output}" "${marker}" marker_pos)
    if(marker_pos EQUAL -1)
      message(FATAL_ERROR "${label}: required marker missing: ${marker}")
    endif()
  endforeach()
  if(narrow)
    set(beyond_verdict "PASS")
  else()
    set(beyond_verdict "(PASS|SKIP)")
  endif()
  foreach(prefix 4294967296 4294967299 8589934592)
    if(NOT output MATCHES "${beyond_verdict} prefix64_rejects_beyond_index prefix=${prefix}")
      message(FATAL_ERROR
        "${label}: required marker missing: ${beyond_verdict} prefix64_rejects_beyond_index prefix=${prefix}")
    endif()
  endforeach()
  if(NOT output MATCHES "PASS [a-z]+-array-length-prefix-guard cases=12 passed=[0-9]+ skipped=[0-9]+ failed=0")
    message(FATAL_ERROR "${label}: summary line missing or not a pass")
  endif()
endfunction()

# Whether the compiler links and runs a 32-bit program. FLAGS is the compile line; SOURCE the
# probe.
function(_guard_probe_32bit compiler source flags out_var)
  set(${out_var} FALSE PARENT_SCOPE)
  execute_process(
    COMMAND "${compiler}" ${flags} "${source}" -o "${source}.bin"
    RESULT_VARIABLE probe_build
    OUTPUT_QUIET ERROR_QUIET
  )
  if(NOT probe_build EQUAL 0)
    return()
  endif()
  execute_process(COMMAND "${source}.bin" RESULT_VARIABLE probe_run OUTPUT_QUIET ERROR_QUIET)
  if(probe_run EQUAL 0)
    set(${out_var} TRUE PARENT_SCOPE)
  endif()
endfunction()

# ----------------------------------------------------------------------- C ----
set(c_out "${OUT_DIR}/c")
_guard_generate(c "${c_out}" ${c_scheme_args})
configure_file("${driver_dir}/ArrayLengthPrefixGuardDriver.c" "${OUT_DIR}/c_driver.c" @ONLY)
file(GLOB c_sources "${c_out}/prefixguard/*.c")

function(_guard_c label bin narrow)
  execute_process(
    COMMAND "${C_COMPILER}" -std=c11 -O2 -Wall -Wextra ${ARGN} -I "${c_out}" "${OUT_DIR}/c_driver.c" ${c_sources}
      -o "${bin}"
    RESULT_VARIABLE build_result
    OUTPUT_VARIABLE build_stdout
    ERROR_VARIABLE build_stderr
  )
  if(NOT build_result EQUAL 0)
    message(FATAL_ERROR "${label} build failed:\n${build_stdout}\n${build_stderr}")
  endif()
  _guard_run("${label}" "${bin}")
  _guard_check("${label}" "${run_stdout}" ${narrow})
endfunction()

_guard_c("C (host)" "${OUT_DIR}/c_guard" FALSE)
list(APPEND legs "C host")

file(WRITE "${OUT_DIR}/probe32.c"
  "#include <stdint.h>\nint main(void) { return sizeof(size_t) == 4U ? 0 : 1; }\n")
_guard_probe_32bit("${C_COMPILER}" "${OUT_DIR}/probe32.c" "-m32" c_m32_runs)
if(c_m32_runs)
  _guard_c("C (-m32)" "${OUT_DIR}/c_guard_m32" TRUE -m32)
  set(executed_32bit TRUE)
  list(APPEND legs "C -m32")
else()
  message(STATUS "C -m32: the compiler does not link and run a 32-bit program here")
endif()

# --------------------------------------------------------------------- C++ ----
set(cpp_out "${OUT_DIR}/cpp")
_guard_generate(cpp "${cpp_out}" ${other_scheme_args})
configure_file("${driver_dir}/ArrayLengthPrefixGuardDriver.cpp" "${OUT_DIR}/cpp_driver.cpp" @ONLY)

function(_guard_cpp label bin narrow)
  execute_process(
    COMMAND "${CXX_COMPILER}" -std=c++20 -O2 -Wall -Wextra ${ARGN} -I "${cpp_out}/std" "${OUT_DIR}/cpp_driver.cpp"
      -o "${bin}"
    RESULT_VARIABLE build_result
    OUTPUT_VARIABLE build_stdout
    ERROR_VARIABLE build_stderr
  )
  if(NOT build_result EQUAL 0)
    message(FATAL_ERROR "${label} build failed:\n${build_stdout}\n${build_stderr}")
  endif()
  _guard_run("${label}" "${bin}")
  _guard_check("${label}" "${run_stdout}" ${narrow})
endfunction()

_guard_cpp("C++ (host)" "${OUT_DIR}/cpp_guard" FALSE)
list(APPEND legs "C++ host")

file(WRITE "${OUT_DIR}/probe32.cpp"
  "#include <cstddef>\n#include <vector>\nint main() { std::vector<bool> v(3); return sizeof(std::size_t) == 4U && v.size() == 3U ? 0 : 1; }\n")
_guard_probe_32bit("${CXX_COMPILER}" "${OUT_DIR}/probe32.cpp" "-m32" cpp_m32_runs)
if(cpp_m32_runs)
  _guard_cpp("C++ (-m32)" "${OUT_DIR}/cpp_guard_m32" TRUE -m32)
  set(executed_32bit TRUE)
  list(APPEND legs "C++ -m32")
else()
  message(STATUS "C++ -m32: the compiler does not link and run a 32-bit program here")
endif()

# -------------------------------------------------------------------- Rust ----
if(DEFINED CARGO_EXECUTABLE AND EXISTS "${CARGO_EXECUTABLE}")
  set(rust_out "${OUT_DIR}/rust")
  _guard_generate(rust "${rust_out}" ${other_scheme_args} --rust-crate-name prefixguard_generated)
  set(harness_out "${OUT_DIR}/rust-harness")
  file(MAKE_DIRECTORY "${harness_out}/src")
  set(RUST_OUT "${rust_out}")
  configure_file("${driver_dir}/ArrayLengthPrefixGuardCargo.toml.in" "${harness_out}/Cargo.toml" @ONLY)
  configure_file("${driver_dir}/ArrayLengthPrefixGuardLib.rs" "${harness_out}/src/lib.rs" @ONLY)
  configure_file("${driver_dir}/ArrayLengthPrefixGuardMain.rs" "${harness_out}/src/main.rs" @ONLY)
  set(cargo_target_dir "${OUT_DIR}/cargo-target")

  _guard_run("Rust (host)"
    "${CMAKE_COMMAND}" -E env "CARGO_TARGET_DIR=${cargo_target_dir}"
    "${CARGO_EXECUTABLE}" run --quiet --manifest-path "${harness_out}/Cargo.toml")
  _guard_check("Rust (host)" "${run_stdout}" FALSE)
  list(APPEND legs "Rust host")

  # Type-check the library, and through it the generated crate and runtime, with a 32-bit usize.
  # rustup ships beside cargo; a cargo installed without it has no target list to consult.
  get_filename_component(cargo_dir "${CARGO_EXECUTABLE}" DIRECTORY)
  find_program(RUSTUP_EXECUTABLE rustup HINTS "${cargo_dir}")
  if(RUSTUP_EXECUTABLE)
    execute_process(
      COMMAND "${RUSTUP_EXECUTABLE}" target list --installed
      RESULT_VARIABLE rustup_result
      OUTPUT_VARIABLE rustup_stdout
      ERROR_VARIABLE rustup_stderr
    )
    if(NOT rustup_result EQUAL 0)
      message(FATAL_ERROR "rustup target list --installed failed:\n${rustup_stdout}\n${rustup_stderr}")
    endif()
    string(REGEX MATCHALL "[^\r\n]+" installed_targets "${rustup_stdout}")
    foreach(target i686-unknown-linux-gnu thumbv7em-none-eabihf thumbv6m-none-eabi)
      if(target IN_LIST installed_targets)
        _guard_run("Rust check (${target})"
          "${CMAKE_COMMAND}" -E env "CARGO_TARGET_DIR=${cargo_target_dir}"
          "${CARGO_EXECUTABLE}" check --quiet --lib --no-default-features --target "${target}"
            --manifest-path "${harness_out}/Cargo.toml")
        list(APPEND legs "Rust ${target} type-check")
      endif()
    endforeach()
  else()
    message(STATUS "Rust 32-bit type-check: rustup is not beside cargo; no target list to consult")
  endif()
else()
  message(STATUS "Rust: cargo not given; leg skipped")
endif()

# ---------------------------------------------------------------------- Go ----
if(DEFINED GO_EXECUTABLE AND EXISTS "${GO_EXECUTABLE}")
  set(go_out "${OUT_DIR}/go")
  _guard_generate(go "${go_out}" ${other_scheme_args} --go-module prefixguard_generated)
  file(MAKE_DIRECTORY "${go_out}/arraylengthprefix")
  configure_file("${driver_dir}/ArrayLengthPrefixGuardMain.go" "${go_out}/arraylengthprefix/main.go" @ONLY)
  set(go_env "GOCACHE=${OUT_DIR}/.gocache" "GOMODCACHE=${OUT_DIR}/.gomodcache" "GOFLAGS=-mod=mod")

  function(_guard_go_build label bin)
    execute_process(
      COMMAND "${CMAKE_COMMAND}" -E env ${go_env} ${ARGN}
        "${GO_EXECUTABLE}" build -buildvcs=false -o "${bin}" ./arraylengthprefix/
      WORKING_DIRECTORY "${go_out}"
      RESULT_VARIABLE build_result
      OUTPUT_VARIABLE build_stdout
      ERROR_VARIABLE build_stderr
    )
    if(NOT build_result EQUAL 0)
      message(FATAL_ERROR "${label} build failed:\n${build_stdout}\n${build_stderr}")
    endif()
  endfunction()

  _guard_go_build("Go (host)" "${OUT_DIR}/go_guard")
  _guard_run("Go (host)" "${OUT_DIR}/go_guard")
  _guard_check("Go (host)" "${run_stdout}" FALSE)
  list(APPEND legs "Go host")

  # linux/386 builds everywhere; an x86 Linux host runs the binary as well.
  _guard_go_build("Go (linux/386)" "${OUT_DIR}/go_guard_386" "GOOS=linux" "GOARCH=386")
  if(host_os STREQUAL "Linux" AND host_platform MATCHES "^(x86_64|amd64|i[3-6]86)$")
    _guard_run("Go (linux/386)" "${OUT_DIR}/go_guard_386")
    _guard_check("Go (linux/386)" "${run_stdout}" TRUE)
    set(executed_32bit TRUE)
    list(APPEND legs "Go linux/386")
  else()
    message(STATUS "Go linux/386: built, not run on ${host_os}/${host_platform}")
    list(APPEND legs "Go linux/386 build")
  endif()
else()
  message(STATUS "Go: go not given; leg skipped")
endif()

# -------------------------------------------------------------- TypeScript ----
if(DEFINED TSC_EXECUTABLE AND EXISTS "${TSC_EXECUTABLE}"
   AND DEFINED NODE_EXECUTABLE AND EXISTS "${NODE_EXECUTABLE}")
  set(ts_out "${OUT_DIR}/ts")
  _guard_generate(ts "${ts_out}" ${other_scheme_args})
  configure_file("${driver_dir}/ArrayLengthPrefixGuardDriver.ts" "${ts_out}/driver.ts" @ONLY)
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
  _guard_run("TypeScript (host)" "${NODE_EXECUTABLE}" "${ts_out}/js/driver.js")
  # An Array holds at most 2^32 - 1 elements on every host, so the lengths beyond a 32-bit index
  # are rejected here rather than skipped.
  _guard_check("TypeScript (host)" "${run_stdout}" TRUE)
  list(APPEND legs "TypeScript host")
else()
  message(STATUS "TypeScript: tsc or node not given; leg skipped")
endif()

# ------------------------------------------------------------------ Python ----
if(DEFINED PYTHON_EXECUTABLE AND EXISTS "${PYTHON_EXECUTABLE}")
  set(py_out "${OUT_DIR}/python")
  set(py_package "prefixguard_generated")
  _guard_generate(python "${py_out}" ${other_scheme_args} --py-package "${py_package}")
  configure_file("${driver_dir}/ArrayLengthPrefixGuardDriver.py" "${OUT_DIR}/python_driver.py" @ONLY)
  _guard_run("Python (pure)" "${PYTHON_EXECUTABLE}" "${OUT_DIR}/python_driver.py" "${py_out}" "${py_package}" pure)
  _guard_check("Python (pure)" "${run_stdout}" FALSE)
  list(APPEND legs "Python pure")
  if(DEFINED ACCEL_MODULE AND NOT "${ACCEL_MODULE}" STREQUAL "" AND EXISTS "${ACCEL_MODULE}")
    file(COPY "${ACCEL_MODULE}" DESTINATION "${py_out}/${py_package}")
    _guard_run("Python (accel)" "${PYTHON_EXECUTABLE}" "${OUT_DIR}/python_driver.py" "${py_out}" "${py_package}" accel)
    _guard_check("Python (accel)" "${run_stdout}" FALSE)
    list(APPEND legs "Python accel")
  endif()
else()
  message(STATUS "Python: python3 not given; leg skipped")
endif()

# ----------------------------------------------------------------- verdict ----
list(JOIN legs ", " legs_text)
if(REQUIRE_32BIT_EXECUTION AND NOT executed_32bit)
  message(FATAL_ERROR
    "no 32-bit leg executed, and REQUIRE_32BIT_EXECUTION is on; legs run: ${legs_text}")
endif()
message(STATUS "array-length prefix guard passed; legs run: ${legs_text}")
