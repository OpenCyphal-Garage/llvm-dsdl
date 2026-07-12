cmake_minimum_required(VERSION 3.24)

foreach(var DSDLC UAVCAN_ROOT OUT_DIR)
  if(NOT DEFINED ${var} OR "${${var}}" STREQUAL "")
    message(FATAL_ERROR "Missing required variable: ${var}")
  endif()
endforeach()

if(NOT DEFINED RUST_PROFILE OR "${RUST_PROFILE}" STREQUAL "")
  set(RUST_PROFILE "std")
endif()
if(NOT "${RUST_PROFILE}" STREQUAL "std" AND
   NOT "${RUST_PROFILE}" STREQUAL "no-std-alloc")
  message(FATAL_ERROR "Invalid RUST_PROFILE value: ${RUST_PROFILE}")
endif()
if(NOT DEFINED RUST_RUNTIME_SPECIALIZATION OR
   "${RUST_RUNTIME_SPECIALIZATION}" STREQUAL "")
  set(RUST_RUNTIME_SPECIALIZATION "portable")
endif()
if(NOT "${RUST_RUNTIME_SPECIALIZATION}" STREQUAL "portable" AND
   NOT "${RUST_RUNTIME_SPECIALIZATION}" STREQUAL "fast")
  message(FATAL_ERROR
    "Invalid RUST_RUNTIME_SPECIALIZATION value: ${RUST_RUNTIME_SPECIALIZATION}")
endif()
if(NOT DEFINED RUST_MEMORY_MODE OR "${RUST_MEMORY_MODE}" STREQUAL "")
  set(RUST_MEMORY_MODE "max-inline")
endif()
if(NOT "${RUST_MEMORY_MODE}" STREQUAL "max-inline" AND
   NOT "${RUST_MEMORY_MODE}" STREQUAL "inline-then-pool")
  message(FATAL_ERROR "Invalid RUST_MEMORY_MODE value: ${RUST_MEMORY_MODE}")
endif()
if(NOT DEFINED RUST_INLINE_THRESHOLD_BYTES OR
   "${RUST_INLINE_THRESHOLD_BYTES}" STREQUAL "")
  set(RUST_INLINE_THRESHOLD_BYTES "256")
endif()
if(NOT RUST_INLINE_THRESHOLD_BYTES MATCHES "^[0-9]+$")
  message(FATAL_ERROR
    "Invalid RUST_INLINE_THRESHOLD_BYTES value: ${RUST_INLINE_THRESHOLD_BYTES}")
endif()
if(RUST_INLINE_THRESHOLD_BYTES LESS 1)
  message(FATAL_ERROR
    "RUST_INLINE_THRESHOLD_BYTES must be positive: ${RUST_INLINE_THRESHOLD_BYTES}")
endif()

if(NOT EXISTS "${DSDLC}")
  message(FATAL_ERROR "dsdlc executable not found: ${DSDLC}")
endif()
if(NOT EXISTS "${UAVCAN_ROOT}")
  message(FATAL_ERROR "uavcan root not found: ${UAVCAN_ROOT}")
endif()
if(NOT EXISTS "/bin/sh")
  message(FATAL_ERROR "Rust determinism check requires /bin/sh")
endif()

file(REMOVE_RECURSE "${OUT_DIR}")
file(MAKE_DIRECTORY "${OUT_DIR}")

set(out_a "${OUT_DIR}/run-a")
set(out_b "${OUT_DIR}/run-b")
file(MAKE_DIRECTORY "${out_a}")
file(MAKE_DIRECTORY "${out_b}")

# Determinism gate: the two concurrent generations run under deliberately different
# environments (locale, timezone, Python hash seed) so that byte-identical output proves
# the codegen is environment-independent -- catching any future locale/TZ/hash-order
# dependence that a same-environment double run would miss.
set(parallel_script "${OUT_DIR}/run-rust-parallel.sh")
file(WRITE
  "${parallel_script}"
  "#!/bin/sh\n"
  "set -eu\n"
  "env LC_ALL=C TZ=UTC PYTHONHASHSEED=0 \"${DSDLC}\" --target-language rust \"${UAVCAN_ROOT}\" --outdir \"${out_a}\" --rust-crate-name \"uavcan_dsdl_generated\" --rust-profile \"${RUST_PROFILE}\" --rust-runtime-specialization \"${RUST_RUNTIME_SPECIALIZATION}\" --rust-memory-mode \"${RUST_MEMORY_MODE}\" --rust-inline-threshold-bytes \"${RUST_INLINE_THRESHOLD_BYTES}\" >\"${OUT_DIR}/run-a.stdout\" 2>\"${OUT_DIR}/run-a.stderr\" &\n"
  "pid_a=$!\n"
  "env LC_ALL=C.UTF-8 TZ=Asia/Tokyo PYTHONHASHSEED=13 \"${DSDLC}\" --target-language rust \"${UAVCAN_ROOT}\" --outdir \"${out_b}\" --rust-crate-name \"uavcan_dsdl_generated\" --rust-profile \"${RUST_PROFILE}\" --rust-runtime-specialization \"${RUST_RUNTIME_SPECIALIZATION}\" --rust-memory-mode \"${RUST_MEMORY_MODE}\" --rust-inline-threshold-bytes \"${RUST_INLINE_THRESHOLD_BYTES}\" >\"${OUT_DIR}/run-b.stdout\" 2>\"${OUT_DIR}/run-b.stderr\" &\n"
  "pid_b=$!\n"
  "wait \"$pid_a\"\n"
  "wait \"$pid_b\"\n")

execute_process(
  COMMAND "/bin/sh" "${parallel_script}"
  RESULT_VARIABLE gen_result
  OUTPUT_VARIABLE gen_stdout
  ERROR_VARIABLE gen_stderr
)
if(NOT gen_result EQUAL 0)
  file(READ "${OUT_DIR}/run-a.stdout" run_a_stdout)
  file(READ "${OUT_DIR}/run-a.stderr" run_a_stderr)
  file(READ "${OUT_DIR}/run-b.stdout" run_b_stdout)
  file(READ "${OUT_DIR}/run-b.stderr" run_b_stderr)
  message(STATUS "parallel launch stdout:\n${gen_stdout}")
  message(STATUS "parallel launch stderr:\n${gen_stderr}")
  message(STATUS "run-a stdout:\n${run_a_stdout}")
  message(STATUS "run-a stderr:\n${run_a_stderr}")
  message(STATUS "run-b stdout:\n${run_b_stdout}")
  message(STATUS "run-b stderr:\n${run_b_stderr}")
  message(FATAL_ERROR "uavcan Rust concurrent determinism generation failed")
endif()

file(GLOB_RECURSE run_a_entries RELATIVE "${out_a}" "${out_a}/*")
file(GLOB_RECURSE run_b_entries RELATIVE "${out_b}" "${out_b}/*")

set(run_a_files "")
foreach(entry IN LISTS run_a_entries)
  if(NOT IS_DIRECTORY "${out_a}/${entry}")
    list(APPEND run_a_files "${entry}")
  endif()
endforeach()

set(run_b_files "")
foreach(entry IN LISTS run_b_entries)
  if(NOT IS_DIRECTORY "${out_b}/${entry}")
    list(APPEND run_b_files "${entry}")
  endif()
endforeach()

list(SORT run_a_files)
list(SORT run_b_files)

string(JOIN "\n" run_a_manifest ${run_a_files})
string(JOIN "\n" run_b_manifest ${run_b_files})
if(NOT run_a_manifest STREQUAL run_b_manifest)
  file(WRITE "${OUT_DIR}/run-a-manifest.txt" "${run_a_manifest}\n")
  file(WRITE "${OUT_DIR}/run-b-manifest.txt" "${run_b_manifest}\n")
  message(FATAL_ERROR
    "Generated Rust file lists differ between deterministic runs. "
    "See ${OUT_DIR}/run-a-manifest.txt and ${OUT_DIR}/run-b-manifest.txt.")
endif()

foreach(rel IN LISTS run_a_files)
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E compare_files "${out_a}/${rel}" "${out_b}/${rel}"
    RESULT_VARIABLE cmp_result
  )
  if(NOT cmp_result EQUAL 0)
    message(FATAL_ERROR "Generated Rust file differs between runs: ${rel}")
  endif()
endforeach()

list(LENGTH run_a_files file_count)
if(file_count EQUAL 0)
  message(FATAL_ERROR "Rust determinism check found zero generated files")
endif()

message(STATUS
  "uavcan Rust concurrent determinism check passed (${RUST_PROFILE}, ${RUST_RUNTIME_SPECIALIZATION}, ${RUST_MEMORY_MODE}): "
  "${file_count} files identical across two runs")
