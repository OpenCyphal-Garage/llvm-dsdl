cmake_minimum_required(VERSION 3.24)

# The obj backend is the only one whose output is not purely text: it compiles the generated
# sources and archives the objects. `ar` records each member's mtime, uid and gid by default, so two
# runs a second apart produced two different archives while every generated source was identical --
# invisible to a text-only check, and the reason this backend gets its own gate.
foreach(var DSDLC UAVCAN_ROOT OUT_DIR)
  if(NOT DEFINED ${var} OR "${${var}}" STREQUAL "")
    message(FATAL_ERROR "Missing required variable: ${var}")
  endif()
endforeach()

if(NOT EXISTS "${DSDLC}")
  message(FATAL_ERROR "dsdlc executable not found: ${DSDLC}")
endif()

if(NOT EXISTS "${UAVCAN_ROOT}")
  message(FATAL_ERROR "uavcan root not found: ${UAVCAN_ROOT}")
endif()

file(REMOVE_RECURSE "${OUT_DIR}")
file(MAKE_DIRECTORY "${OUT_DIR}")

set(out_a "${OUT_DIR}/run-a")
set(out_b "${OUT_DIR}/run-b")
file(MAKE_DIRECTORY "${out_a}")
file(MAKE_DIRECTORY "${out_b}")

if(NOT EXISTS "/bin/sh")
  message(FATAL_ERROR "parallel determinism check requires /bin/sh")
endif()

# Determinism gate: the two concurrent generations run under deliberately different
# environments (locale, timezone, Python hash seed) so that byte-identical output proves
# the codegen is environment-independent -- catching any future locale/TZ/hash-order
# dependence that a same-environment double run would miss.
set(parallel_script "${OUT_DIR}/run-obj-parallel.sh")
file(
  WRITE
  "${parallel_script}"
  "#!/bin/sh\n"
  "set -eu\n"
  "env LC_ALL=C TZ=UTC PYTHONHASHSEED=0 \"${DSDLC}\" --target-language obj --target-endianness little \"${UAVCAN_ROOT}\" --outdir \"${out_a}\" >\"${OUT_DIR}/run-a.stdout\" 2>\"${OUT_DIR}/run-a.stderr\" &\n"
  "pid_a=$!\n"
  "env LC_ALL=C.UTF-8 TZ=Asia/Tokyo PYTHONHASHSEED=13 \"${DSDLC}\" --target-language obj --target-endianness little \"${UAVCAN_ROOT}\" --outdir \"${out_b}\" >\"${OUT_DIR}/run-b.stdout\" 2>\"${OUT_DIR}/run-b.stderr\" &\n"
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
  message(FATAL_ERROR "uavcan obj concurrent determinism generation failed")
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
    "Generated obj file lists differ between deterministic runs. "
    "See ${OUT_DIR}/run-a-manifest.txt and ${OUT_DIR}/run-b-manifest.txt.")
endif()

foreach(rel IN LISTS run_a_files)
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E compare_files "${out_a}/${rel}" "${out_b}/${rel}"
    RESULT_VARIABLE cmp_result
  )
  if(NOT cmp_result EQUAL 0)
    message(FATAL_ERROR "Generated obj artefact differs between runs: ${rel}")
  endif()
endforeach()

list(LENGTH run_a_files file_count)
if(file_count EQUAL 0)
  message(FATAL_ERROR "obj determinism check found zero generated files")
endif()

message(STATUS
  "uavcan obj concurrent determinism check passed: ${file_count} files identical across two runs")
