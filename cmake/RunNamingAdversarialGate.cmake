#===----------------------------------------------------------------------===#
#
# Part of the OpenCyphal project, under the MIT licence
# SPDX-License-Identifier: MIT
#
#===----------------------------------------------------------------------===#
#
# Compiles the generated-name adversarial corpus in every backend.
#
# `Discovery` rejects a corpus in which two DSDL names reach one generated identifier. The class
# this gate is for is the other one: a DSDL name reaching an identifier the backend emits for every
# type -- a trait its bodies name unqualified, a module its crate root declares, a global the
# standard headers put beside a namespace, an accessor composed from another field, a union's
# synthetic tag. No lane saw that class, because reaching it needs a name pair the regulated corpus
# has no instance of, so the defects arrived one at a time through review.
#
# The corpus is written by test/integration/generate_naming_adversarial_corpus.py, whose axes are
# the defects that reached review rather than hazards imagined at a desk. This gate generates it in
# each backend and hands the result to that language's own compiler. A language whose toolchain is
# absent is skipped and said so; the C and C++ halves are mandatory, since their compilers built
# this project.
#
# Two passes. The versioned pass carries the version in the type name, which is what lets a module
# hold several versions of one definition: C and C++ refuse the unversioned form for that with a
# sentinel, and Go cannot generate it at all, one package being one namespace. The unversioned pass
# takes the newest of each definition, which is the default a consumer gets.

cmake_minimum_required(VERSION 3.24)

foreach(var DSDLC PYTHON_EXECUTABLE CORPUS_SCRIPT WORK_DIR C_COMPILER CXX_COMPILER)
  if(NOT DEFINED ${var} OR "${${var}}" STREQUAL "")
    message(FATAL_ERROR "Missing required variable: ${var}")
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
  message(STATUS "naming adversarial gate: skipping ${language} (${reason})")
endfunction()

foreach(_pass versioned unversioned)
  if(_pass STREQUAL "versioned")
    set(_scheme --all-type-versions --versioned-type-names)
    set(_corpus_args "")
  else()
    set(_scheme "")
    set(_corpus_args --no-version-multiplicity)
  endif()
  set(_out "${WORK_DIR}/${_pass}")
  set(_corpus "${WORK_DIR}/${_pass}-corpus")

  llvmdsdl_run_or_fail("adversarial corpus generation (${_pass})"
    "${PYTHON_EXECUTABLE}" "${CORPUS_SCRIPT}" --outdir "${_corpus}" ${_corpus_args})

  # Each root namespace is its own directory, and several are named for what a backend's own root
  # declares, so the list is read off the corpus rather than written down a second time.
  file(GLOB _roots LIST_DIRECTORIES true "${_corpus}/*")
  set(_root_args "")
  foreach(_root IN LISTS _roots)
    if(IS_DIRECTORY "${_root}")
      list(APPEND _root_args "${_root}")
    endif()
  endforeach()
  if(_root_args STREQUAL "")
    message(FATAL_ERROR "adversarial corpus (${_pass}) produced no root namespaces")
  endif()

  # -----------------------------------------------------------------------------------------------
  # C -- every generated translation unit under warnings-as-errors.

  llvmdsdl_run_or_fail("dsdlc C generation (${_pass})"
    "${DSDLC}" --target-language c ${_scheme} ${_root_args} --outdir "${_out}/c")
  file(GLOB_RECURSE _c_units "${_out}/c/*.c")
  if(_c_units STREQUAL "")
    message(FATAL_ERROR "C generation (${_pass}) produced no translation units")
  endif()
  foreach(_unit IN LISTS _c_units)
    llvmdsdl_run_or_fail("generated C compile (${_pass}): ${_unit}"
      "${C_COMPILER}" -std=c11 -Wall -Wextra -Werror -I "${_out}/c" -c "${_unit}" -o "${_unit}.o")
  endforeach()

  # -----------------------------------------------------------------------------------------------
  # C++ -- one translation unit including every header, so two namespaces meet in one scope.

  llvmdsdl_run_or_fail("dsdlc C++ generation (${_pass})"
    "${DSDLC}" --target-language cpp --cpp-profile std ${_scheme} ${_root_args} --outdir "${_out}/cpp")
  file(GLOB_RECURSE _cpp_headers RELATIVE "${_out}/cpp" "${_out}/cpp/*.hpp")
  set(_cpp_probe "")
  foreach(_header IN LISTS _cpp_headers)
    if(NOT _header STREQUAL "dsdl_runtime.hpp")
      string(APPEND _cpp_probe "#include \"${_header}\"\n")
    endif()
  endforeach()
  string(APPEND _cpp_probe "int main() { return 0; }\n")
  file(WRITE "${_out}/cpp_probe.cpp" "${_cpp_probe}")
  llvmdsdl_run_or_fail("generated C++ compile (${_pass})"
    "${CXX_COMPILER}" -std=c++23 -Wall -Wextra -Werror -I "${_out}/cpp" -c "${_out}/cpp_probe.cpp"
                      -o "${_out}/cpp_probe.o")

  # -----------------------------------------------------------------------------------------------
  # Rust -- both profiles. rustc's naming lints are on by default, and the no-std profile is where a
  # crate-root name meets `extern crate alloc`.

  if(CARGO_EXECUTABLE AND NOT CARGO_EXECUTABLE MATCHES "NOTFOUND")
    llvmdsdl_run_or_fail("dsdlc Rust generation (${_pass})"
      "${DSDLC}" --target-language rust ${_scheme} ${_root_args}
                 --rust-crate-name naming_adversarial --outdir "${_out}/rust")
    foreach(_features "" "--no-default-features")
      llvmdsdl_run_or_fail("generated Rust check (${_pass} ${_features})"
        "${CMAKE_COMMAND}" -E env "CARGO_TARGET_DIR=${_out}/rust/target" "RUSTFLAGS=-D warnings"
          "${CARGO_EXECUTABLE}" check --quiet --manifest-path "${_out}/rust/Cargo.toml" ${_features})
    endforeach()
  else()
    llvmdsdl_skip("Rust" "cargo unavailable")
  endif()

  # -----------------------------------------------------------------------------------------------
  # Go -- one directory is one package, so two types sharing a name stop the package building.

  if(GO_EXECUTABLE AND NOT GO_EXECUTABLE MATCHES "NOTFOUND")
    llvmdsdl_run_or_fail("dsdlc Go generation (${_pass})"
      "${DSDLC}" --target-language go ${_scheme} ${_root_args}
                 --go-module llvmdsdl/naming_adversarial --outdir "${_out}/go")
    llvmdsdl_run_or_fail("generated Go build (${_pass})"
      "${CMAKE_COMMAND}" -E env "GOFLAGS=-mod=mod" "GOCACHE=${_out}/go/.gocache"
        "GOMODCACHE=${_out}/go/.gomodcache" "${GO_EXECUTABLE}" build -buildvcs=false ./...
      WORKING_DIRECTORY "${_out}/go")
    llvmdsdl_run_or_fail("generated Go vet (${_pass})"
      "${CMAKE_COMMAND}" -E env "GOFLAGS=-mod=mod" "GOCACHE=${_out}/go/.gocache"
        "GOMODCACHE=${_out}/go/.gomodcache" "${GO_EXECUTABLE}" vet ./...
      WORKING_DIRECTORY "${_out}/go")
  else()
    llvmdsdl_skip("Go" "go unavailable")
  endif()

  # -----------------------------------------------------------------------------------------------
  # TypeScript.

  if(TSC_EXECUTABLE AND NOT TSC_EXECUTABLE MATCHES "NOTFOUND")
    llvmdsdl_run_or_fail("dsdlc TypeScript generation (${_pass})"
      "${DSDLC}" --target-language ts ${_scheme} ${_root_args}
                 --ts-module naming_adversarial --outdir "${_out}/ts")
    file(WRITE "${_out}/ts/tsconfig-adversarial.json"
"{
  \"compilerOptions\": {
    \"target\": \"ES2022\",
    \"module\": \"ES2022\",
    \"moduleResolution\": \"bundler\",
    \"strict\": true,
    \"noUnusedLocals\": true,
    \"noEmit\": true,
    \"skipLibCheck\": true
  },
  \"include\": [\"**/*.ts\"]
}
")
    llvmdsdl_run_or_fail("generated TypeScript type-check (${_pass})"
      "${TSC_EXECUTABLE}" -p "${_out}/ts/tsconfig-adversarial.json" --pretty false)
  else()
    llvmdsdl_skip("TypeScript" "tsc unavailable")
  endif()

  # -----------------------------------------------------------------------------------------------
  # Python -- byte-compiled, then every module imported, which is where a name shadowing another
  # module's shows up.

  llvmdsdl_run_or_fail("dsdlc Python generation (${_pass})"
    "${DSDLC}" --target-language python ${_scheme} ${_root_args} --outdir "${_out}/py")
  llvmdsdl_run_or_fail("generated Python byte-compile (${_pass})"
    "${PYTHON_EXECUTABLE}" -m compileall -q "${_out}/py")
  llvmdsdl_run_or_fail("generated Python import (${_pass})"
    "${PYTHON_EXECUTABLE}" -c
"import pkgutil, importlib, sys
sys.path.insert(0, sys.argv[1])
bad = []
for module in pkgutil.walk_packages([sys.argv[1] + '/dsdl_gen'], 'dsdl_gen.'):
    try:
        importlib.import_module(module.name)
    except Exception as exc:
        bad.append(f'{module.name}: {type(exc).__name__}: {exc}')
if bad:
    raise SystemExit('generated Python failed to import:\\n' + '\\n'.join(bad))
"
    "${_out}/py")
endforeach()

message(STATUS "naming adversarial gate: every backend accepted the corpus")
