#===----------------------------------------------------------------------===#
#
# Part of the OpenCyphal project, under the MIT licence
# SPDX-License-Identifier: MIT
#
#===----------------------------------------------------------------------===#
#
# Compile gate for the adversarial naming corpus.
#
# test/lit/fixtures_naming names its types, fields and constants after keywords, after each other's
# case foldings, and after the per-type constants the backends emit. lit checks the identifiers that
# come out; this checks that what comes out is accepted by the language.
#
# Every backend is generated, not only the ones a given build selects. The frontend's output-name
# collision check is keyed to the languages an invocation emits, so a hazard confined to one backend
# reaches a user only when they select it; running the corpus through all six here is what keeps that
# from being the first time anyone finds out.
#
# The C and C++ halves are mandatory -- their compilers built this project. cargo, go, tsc and
# python3 are optional and skip with a message, matching the other native lanes.

# -Wreserved-identifier is Clang-only and is not implied by -Wall or -Wextra, so it has to be asked
# for and can only be asked of Clang. It is the flag that makes an identifier in a language's reserved
# namespace an error rather than a silent conformance debt.
#
set(_reserved_c "")
set(_reserved_cxx "")
if(C_COMPILER_ID MATCHES "Clang")
  set(_reserved_c "-Wreserved-identifier")
endif()
if(CXX_COMPILER_ID MATCHES "Clang")
  set(_reserved_cxx "-Wreserved-identifier")
endif()

foreach(_required DSDLC FIXTURE_ROOT WORK_DIR C_COMPILER CXX_COMPILER)
  if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
    message(FATAL_ERROR "${_required} must be provided")
  endif()
endforeach()

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

function(llvmdsdl_skip language reason)
  message(STATUS "naming corpus compile gate: skipping ${language} (${reason})")
endfunction()

# ---------------------------------------------------------------------------------------------------
# C -- every generated translation unit, plus a header-only probe.

llvmdsdl_run_or_fail("dsdlc C generation"
  "${DSDLC}" --target-language c "${FIXTURE_ROOT}" --outdir "${WORK_DIR}/c")

file(GLOB_RECURSE _c_units "${WORK_DIR}/c/*.c")
if(_c_units STREQUAL "")
  message(FATAL_ERROR "no C translation units were generated from the naming corpus")
endif()
foreach(_unit IN LISTS _c_units)
  get_filename_component(_stem "${_unit}" NAME_WE)
  llvmdsdl_run_or_fail("C compile of ${_stem}.c under -Werror"
    "${C_COMPILER}" -std=c11 -Wall -Wextra ${_reserved_c} -Werror
                    -I "${WORK_DIR}/c" -c "${_unit}" -o "${WORK_DIR}/c_${_stem}.o")
endforeach()

# ---------------------------------------------------------------------------------------------------
# C++ -- a single translation unit including every generated header, so that a type whose members
# collide with the generated statics is compiled rather than merely emitted.

llvmdsdl_run_or_fail("dsdlc C++ generation"
  "${DSDLC}" --target-language cpp "${FIXTURE_ROOT}" --cpp-profile std --outdir "${WORK_DIR}/cpp")

file(GLOB_RECURSE _cpp_headers RELATIVE "${WORK_DIR}/cpp" "${WORK_DIR}/cpp/*.hpp")
if(_cpp_headers STREQUAL "")
  message(FATAL_ERROR "no C++ headers were generated from the naming corpus")
endif()
set(_cpp_probe "")
foreach(_header IN LISTS _cpp_headers)
  string(APPEND _cpp_probe "#include \"${_header}\"\n")
endforeach()
string(APPEND _cpp_probe "int main() { return 0; }\n")
file(WRITE "${WORK_DIR}/cpp_probe.cpp" "${_cpp_probe}")

llvmdsdl_run_or_fail("C++ compile of the naming corpus under -Werror"
  "${CXX_COMPILER}" -std=c++20 -Wall -Wextra ${_reserved_cxx} -Werror
                    -I "${WORK_DIR}/cpp" -c "${WORK_DIR}/cpp_probe.cpp"
                    -o "${WORK_DIR}/cpp_probe.o")

# ---------------------------------------------------------------------------------------------------
# C++ again, under the PMR profile. The profile adds two members to every struct, and a claimed-name
# omission there is invisible to the std run above -- which is how both of them came to be missing.

llvmdsdl_run_or_fail("dsdlc C++ PMR generation"
  "${DSDLC}" --target-language cpp "${FIXTURE_ROOT}" --cpp-profile pmr --outdir "${WORK_DIR}/cpp_pmr")

file(GLOB_RECURSE _pmr_headers RELATIVE "${WORK_DIR}/cpp_pmr" "${WORK_DIR}/cpp_pmr/*.hpp")
if(_pmr_headers STREQUAL "")
  message(FATAL_ERROR "no C++ headers were generated from the naming corpus under the PMR profile")
endif()
set(_pmr_probe "")
foreach(_header IN LISTS _pmr_headers)
  string(APPEND _pmr_probe "#include \"${_header}\"\n")
endforeach()
string(APPEND _pmr_probe "int main() { return 0; }\n")
file(WRITE "${WORK_DIR}/cpp_pmr_probe.cpp" "${_pmr_probe}")

llvmdsdl_run_or_fail("C++ PMR compile of the naming corpus under -Werror"
  "${CXX_COMPILER}" -std=c++20 -Wall -Wextra ${_reserved_cxx} -Werror
                    -I "${WORK_DIR}/cpp_pmr" -c "${WORK_DIR}/cpp_pmr_probe.cpp"
                    -o "${WORK_DIR}/cpp_pmr_probe.o")

# ---------------------------------------------------------------------------------------------------
# Rust

llvmdsdl_run_or_fail("dsdlc Rust generation"
  "${DSDLC}" --target-language rust "${FIXTURE_ROOT}" --rust-profile std
             --rust-crate-name naming_corpus --outdir "${WORK_DIR}/rust")

if(CARGO_EXECUTABLE AND NOT CARGO_EXECUTABLE MATCHES "NOTFOUND")
  llvmdsdl_run_or_fail("generated Rust crate build"
    "${CMAKE_COMMAND}" -E env "CARGO_TARGET_DIR=${WORK_DIR}/rust/target"
      "${CARGO_EXECUTABLE}" build --quiet --manifest-path "${WORK_DIR}/rust/Cargo.toml")
else()
  llvmdsdl_skip("Rust" "cargo unavailable")
endif()

# ---------------------------------------------------------------------------------------------------
# Go -- one directory is one package, so this is the backend where two types sharing a name, or a
# field sharing a name with a generated method, stops the whole package building.

llvmdsdl_run_or_fail("dsdlc Go generation"
  "${DSDLC}" --target-language go "${FIXTURE_ROOT}" --go-module llvmdsdl/naming_corpus
             --outdir "${WORK_DIR}/go")

if(GO_EXECUTABLE AND NOT GO_EXECUTABLE MATCHES "NOTFOUND")
  # -buildvcs=false: the module is written inside the repository, and go would otherwise consult git
  # for a stamp this throwaway build has no use for.
  llvmdsdl_run_or_fail("generated Go package build"
    "${CMAKE_COMMAND}" -E env
      "GOFLAGS=-mod=mod" "GOCACHE=${WORK_DIR}/go/.gocache" "GOMODCACHE=${WORK_DIR}/go/.gomodcache"
      "${GO_EXECUTABLE}" build -buildvcs=false ./...
    WORKING_DIRECTORY "${WORK_DIR}/go")
else()
  llvmdsdl_skip("Go" "go unavailable")
endif()

# ---------------------------------------------------------------------------------------------------
# TypeScript

llvmdsdl_run_or_fail("dsdlc TypeScript generation"
  "${DSDLC}" --target-language ts "${FIXTURE_ROOT}" --ts-module naming_corpus
             --outdir "${WORK_DIR}/ts")

if(TSC_EXECUTABLE AND NOT TSC_EXECUTABLE MATCHES "NOTFOUND")
  file(WRITE "${WORK_DIR}/ts/tsconfig-naming-corpus.json"
"{
  \"compilerOptions\": {
    \"target\": \"ES2020\",
    \"module\": \"commonjs\",
    \"strict\": true,
    \"noEmit\": true,
    \"skipLibCheck\": true
  },
  \"include\": [\"**/*.ts\"]
}
")
  llvmdsdl_run_or_fail("generated TypeScript type-check"
    "${TSC_EXECUTABLE}" -p "${WORK_DIR}/ts/tsconfig-naming-corpus.json" --pretty false)
else()
  llvmdsdl_skip("TypeScript" "tsc unavailable")
endif()

# ---------------------------------------------------------------------------------------------------
# Python -- compiled rather than imported: a syntax error is what a bad identifier produces, and
# byte-compiling every module reaches all of them without needing the runtime on sys.path.

llvmdsdl_run_or_fail("dsdlc Python generation"
  "${DSDLC}" --target-language python "${FIXTURE_ROOT}" --py-package naming_corpus
             --outdir "${WORK_DIR}/py")

if(PYTHON_EXECUTABLE AND NOT PYTHON_EXECUTABLE MATCHES "NOTFOUND")
  llvmdsdl_run_or_fail("generated Python byte-compile"
    "${PYTHON_EXECUTABLE}" -m compileall -q "${WORK_DIR}/py")
else()
  llvmdsdl_skip("Python" "python3 unavailable")
endif()

message(STATUS "naming corpus compile gate passed")
