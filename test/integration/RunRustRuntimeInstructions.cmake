# Instruction-count regression gate for the generated Rust serializer.
#
# The sibling benchmark measures seconds, which is the honest unit for "how fast is this" and a
# hopeless one for "did this get slower". A second is a property of the machine that measured it:
# the same binary, unchanged, spread 1.41x across twenty runs on a contended box, which is why that
# benchmark's thresholds have never been enforceable anywhere but one developer's desk.
#
# An instruction count is a property of the code. Measured under cachegrind, three idle runs and
# three runs on a deliberately oversubscribed container reported the generated `deserialize` at
# 8,172,350,000 instructions -- the same number, to the digit, six times. That is what a regression
# gate needs, and no amount of budget-widening gets wall-clock there.
#
# What this therefore does NOT protect against, stated so nobody assumes otherwise: a change that
# executes the same instructions with worse locality is invisible here. Cachegrind can simulate the
# cache too (--cache-sim=yes, also deterministic) if that gap ever matters; it costs more runtime and
# is not enabled.
#
# Two consequences of counting instructions rather than time:
#
#   * The count is per ARCHITECTURE. amd64 and arm64 execute different instruction streams for the
#     same source, so the baseline file is keyed by processor and a machine with no entry skips
#     rather than pretending some other machine's number applies to it.
#   * The count moves when the COMPILER moves. A rustc bump changes it, and that is a deliberate,
#     visible re-baseline rather than a flake -- and it tells you what the upgrade cost.
#
# Iteration counts here are the script's own and deliberately small: cachegrind runs ~77x slower than
# native, and the count is exact, so there is nothing to gain from a long run. They are also
# independent of the benchmark's iteration counts on purpose, so that tuning the timing benchmark
# does not silently re-baseline this gate.
cmake_minimum_required(VERSION 3.24)

foreach(var DSDLC OUT_DIR RUST_BENCH_ROOT CARGO_EXECUTABLE BASELINE_JSON)
  if(NOT DEFINED ${var} OR "${${var}}" STREQUAL "")
    message(FATAL_ERROR "Missing required variable: ${var}")
  endif()
endforeach()

# Valgrind does not exist on every host this suite runs on -- notably not on Apple Silicon, which it
# has never supported. A skip is the right answer there; a failure would say the code regressed when
# what actually happened is that the tool is absent.
find_program(VALGRIND_EXECUTABLE valgrind)
find_program(CG_ANNOTATE_EXECUTABLE cg_annotate)
if(NOT VALGRIND_EXECUTABLE OR NOT CG_ANNOTATE_EXECUTABLE)
  message(STATUS "valgrind/cg_annotate unavailable; skipping the Rust instruction-count gate")
  # 77 is the SKIP_RETURN_CODE this test is registered with.
  cmake_language(EXIT 77)
endif()

if(NOT DEFINED BENCH_ITERATIONS_SMALL OR "${BENCH_ITERATIONS_SMALL}" STREQUAL "")
  set(BENCH_ITERATIONS_SMALL 2000)
endif()
if(NOT DEFINED BENCH_ITERATIONS_MEDIUM OR "${BENCH_ITERATIONS_MEDIUM}" STREQUAL "")
  set(BENCH_ITERATIONS_MEDIUM 1000)
endif()
if(NOT DEFINED BENCH_ITERATIONS_LARGE OR "${BENCH_ITERATIONS_LARGE}" STREQUAL "")
  set(BENCH_ITERATIONS_LARGE 500)
endif()

# The memory mode is fixed rather than swept. Both modes share the generated serializer; sweeping
# them would double a 77x-slowed run to re-measure the same functions.
set(mode "max-inline")

# Both the generator flag below and the harness template read this, and they have to agree: the
# template turns it into a `const` and an empty substitution is a syntax error rather than a default.
set(RUST_INLINE_THRESHOLD_BYTES 256)

file(REMOVE_RECURSE "${OUT_DIR}")
file(MAKE_DIRECTORY "${OUT_DIR}")

execute_process(
  COMMAND
    "${DSDLC}" --target-language rust
      # The shared harness template names versioned types, like the timing benchmark's does.
      --versioned-type-names
      "${RUST_BENCH_ROOT}"
      --outdir "${OUT_DIR}"
      --rust-crate-name "llvmdsdl_runtime_bench"
      --rust-profile "std"
      --rust-runtime-specialization "portable"
      --rust-memory-mode "${mode}"
      --rust-inline-threshold-bytes "${RUST_INLINE_THRESHOLD_BYTES}"
  RESULT_VARIABLE gen_result
  OUTPUT_VARIABLE gen_stdout
  ERROR_VARIABLE gen_stderr
)
if(NOT gen_result EQUAL 0)
  message(STATUS "dsdlc stdout:\n${gen_stdout}")
  message(STATUS "dsdlc stderr:\n${gen_stderr}")
  message(FATAL_ERROR "Rust instruction-count generation failed")
endif()

set(RUST_BENCH_MODE "${mode}")
file(MAKE_DIRECTORY "${OUT_DIR}/src/bin")
configure_file(
  "${CMAKE_CURRENT_LIST_DIR}/RustRuntimeBenchMain.rs.in"
  "${OUT_DIR}/src/bin/runtime_bench.rs"
  @ONLY
)

execute_process(
  COMMAND
    "${CMAKE_COMMAND}" -E env "CARGO_TARGET_DIR=${OUT_DIR}/cargo-target"
      "${CARGO_EXECUTABLE}" build --quiet --release --manifest-path "${OUT_DIR}/Cargo.toml"
  RESULT_VARIABLE build_result
  OUTPUT_VARIABLE build_stdout
  ERROR_VARIABLE build_stderr
)
if(NOT build_result EQUAL 0)
  message(STATUS "cargo stdout:\n${build_stdout}")
  message(STATUS "cargo stderr:\n${build_stderr}")
  message(FATAL_ERROR "Rust instruction-count harness failed to build")
endif()

# --cache-sim/--branch-sim off: their counts are simulations of one particular cache geometry, which
# is a different (and much noisier to interpret) question than how many instructions ran.
execute_process(
  COMMAND
    "${CMAKE_COMMAND}" -E env "LLVMDSDL_RUST_BENCH_REPORT_JSON=${OUT_DIR}/bench.json"
      "${VALGRIND_EXECUTABLE}" --tool=cachegrind --cache-sim=no --branch-sim=no
        "--cachegrind-out-file=${OUT_DIR}/cachegrind.out"
        "${OUT_DIR}/cargo-target/release/runtime_bench"
  RESULT_VARIABLE cg_result
  OUTPUT_VARIABLE cg_stdout
  ERROR_VARIABLE cg_stderr
)
if(NOT cg_result EQUAL 0)
  message(STATUS "cachegrind stdout:\n${cg_stdout}")
  message(STATUS "cachegrind stderr:\n${cg_stderr}")
  message(FATAL_ERROR "cachegrind run failed")
endif()

execute_process(
  COMMAND "${CG_ANNOTATE_EXECUTABLE}" "${OUT_DIR}/cachegrind.out"
  RESULT_VARIABLE ann_result
  OUTPUT_VARIABLE ann_stdout
  ERROR_VARIABLE ann_stderr
)
if(NOT ann_result EQUAL 0)
  message(STATUS "cg_annotate stderr:\n${ann_stderr}")
  message(FATAL_ERROR "cg_annotate failed")
endif()
file(WRITE "${OUT_DIR}/cg_annotate.txt" "${ann_stdout}")

# Per-function attribution is what makes this a gate on the GENERATED code rather than on the harness
# around it: `main`, the JSON write and process startup are all separate entries and none of them are
# read here.
function(llvmdsdl_extract_ir annotation symbol out_var)
  # cg_annotate prints `  <n,nnn,nnn> (pct%)  <file>:<function>`; the counts carry thousands commas.
  if(annotation MATCHES "([0-9][0-9,]*)[^\n]*[ \t]+[^\n]*${symbol}[ \t]*\n")
    string(REPLACE "," "" _value "${CMAKE_MATCH_1}")
    set(${out_var} "${_value}" PARENT_SCOPE)
  else()
    set(${out_var} "" PARENT_SCOPE)
  endif()
endfunction()

llvmdsdl_extract_ir("${ann_stdout}" "::deserialize" deserialize_ir)
llvmdsdl_extract_ir("${ann_stdout}" "::serialize" serialize_ir)

if("${deserialize_ir}" STREQUAL "" OR "${serialize_ir}" STREQUAL "")
  message(STATUS "cg_annotate output:\n${ann_stdout}")
  message(FATAL_ERROR
    "could not find the generated serialize/deserialize symbols in the cachegrind profile; the "
    "generated symbol names or the cg_annotate output format changed")
endif()

# CMAKE_SYSTEM_PROCESSOR is not set in script mode, so ask the host directly.
cmake_host_system_information(RESULT arch QUERY OS_PLATFORM)
message(STATUS "Rust instruction counts (${arch}, ${mode}, "
               "iterations ${BENCH_ITERATIONS_SMALL}/${BENCH_ITERATIONS_MEDIUM}/${BENCH_ITERATIONS_LARGE}):")
message(STATUS "  deserialize = ${deserialize_ir}")
message(STATUS "  serialize   = ${serialize_ir}")

file(READ "${BASELINE_JSON}" baseline_json)
string(JSON budget_percent ERROR_VARIABLE budget_error GET "${baseline_json}" meta budget_percent)
if(budget_error OR "${budget_percent}" STREQUAL "")
  set(budget_percent 1)
endif()

string(JSON arch_entry ERROR_VARIABLE arch_error GET "${baseline_json}" counts "${arch}")
if(arch_error OR "${arch_entry}" STREQUAL "")
  # Deliberately a skip and not a pass: there is no number for this processor, so there is nothing to
  # compare against and saying "ok" would be a lie. The message carries what to paste.
  message(STATUS
    "No instruction-count baseline for '${arch}'. Add one to ${BASELINE_JSON}:\n"
    "    \"${arch}\": {\n"
    "      \"deserialize\": ${deserialize_ir},\n"
    "      \"serialize\": ${serialize_ir}\n"
    "    }")
  cmake_language(EXIT 77)
endif()

set(failures "")
foreach(symbol deserialize serialize)
  string(JSON expected GET "${arch_entry}" "${symbol}")
  if("${symbol}" STREQUAL "deserialize")
    set(observed "${deserialize_ir}")
  else()
    set(observed "${serialize_ir}")
  endif()
  math(EXPR limit "${expected} + (${expected} / 100) * ${budget_percent}")
  if(observed GREATER limit)
    math(EXPR over_permille "((${observed} - ${expected}) * 1000) / ${expected}")
    string(APPEND failures
      "  ${symbol}: ${observed} instructions, baseline ${expected}, "
      "limit ${limit} (+${budget_percent}%) -- ${over_permille} permille over baseline\n")
  endif()
endforeach()

if(NOT "${failures}" STREQUAL "")
  message(STATUS "Instruction-count regressions:\n${failures}")
  message(FATAL_ERROR
    "the generated Rust serializer executes measurably more instructions than its baseline. This is "
    "a deterministic measurement, so it is a real change in the emitted code and not noise. If the "
    "change is intended -- an optimisation traded off, or a compiler upgrade -- re-baseline "
    "${BASELINE_JSON} in the same commit that causes it.")
endif()

message(STATUS "Rust instruction counts within +${budget_percent}% of baseline for ${arch}")
