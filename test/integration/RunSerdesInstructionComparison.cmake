# What the generated serialisers cost, in instructions, for three
# implementations of the same ten types: the peer's C, our C, and the object we
# emit ourselves.
#
# The differential parity lane establishes that the implementations produce the
# same bytes. This lane asks what each executes to produce them, over the same
# types and the same payloads, and reports the ratios.
#
# The object column is what makes this more than a comparison with the peer. Our
# C and our object are the same plans down two lowering routes -- one through a
# C compiler, one through dsdlc's own LLVM pipeline at O2 -- so the difference
# between those two columns is the routes, while the difference from the peer is
# the plans. Note what else differs between them: the C column is built by
# C_COMPILER and the object column by the LLVM dsdlc is linked against, so
# pointing C_COMPILER at clang is what narrows the first comparison to the
# routes alone.
#
# Instructions rather than seconds, for the reason the Rust instruction gate
# gives: a second is a property of the machine that measured it, an instruction
# count is a property of the code. Under cachegrind the count is exact and
# repeatable, so a ratio between two implementations measured on one machine
# says the same thing on the next one.
#
# The measurement is a two-point difference. Each (case, implementation,
# operation) runs twice, at zero iterations and at ITERATIONS, and the
# difference over the trip count is the per-iteration cost. Everything fixed --
# process start, the loader, the harness's fixture search, its printing --
# appears in both runs and cancels, which is what makes per-function symbol
# attribution unnecessary: our serialisers are out-of-line functions in `.c`
# files and the peer's are `static inline` in its headers, so no per-symbol read
# of a profile could compare them without picking apart two different call
# trees. A `noop` implementation of the same loop measures the scaffolding, and
# the runner subtracts it from every implementation.
#
# Our two implementations define the same entry points, so they cannot be linked
# into one binary. The runner builds one driver per implementation, each with the
# same harness and the same peer, and measures the peer and the empty loop in
# both -- they are the same code either side, so a disagreement means the two
# drivers are not measuring the same thing and the numbers cannot be compared.
#
# This is REPORTED, NOT ENFORCED. Nunavut is a peer used for corroboration, not
# an oracle, and that holds for performance as much as for bytes: a gate on the
# peer ratio would make its instruction count the definition of correct speed.
# The numbers are deterministic, so a threshold on OUR side can be built from
# them whenever there is a reason to -- the report carries the raw counts.
#
# What it does not measure: the peer's bodies are visible to the optimiser at a
# real call site and ours are not, and both are behind an indirect call here.
# Locality is invisible to an instruction count, as is anything the surrounding
# program would do differently with one API or the other.
cmake_minimum_required(VERSION 3.24)

foreach(var DSDLC UAVCAN_ROOT OUT_DIR C_COMPILER PYTHON_EXECUTABLE SOURCE_ROOT
            NUNAVUT_REPO PYDSDL_REPO REPORT_JSON)
  if(NOT DEFINED ${var} OR "${${var}}" STREQUAL "")
    message(FATAL_ERROR "Missing required variable: ${var}")
  endif()
endforeach()

foreach(path "${DSDLC}" "${UAVCAN_ROOT}" "${C_COMPILER}" "${PYTHON_EXECUTABLE}")
  if(NOT EXISTS "${path}")
    message(FATAL_ERROR "not found: ${path}")
  endif()
endforeach()
if(NOT EXISTS "${NUNAVUT_REPO}/src/nunavut")
  message(FATAL_ERROR "nunavut source tree not found: ${NUNAVUT_REPO}")
endif()
if(NOT EXISTS "${PYDSDL_REPO}/pydsdl")
  message(FATAL_ERROR "pydsdl source tree not found: ${PYDSDL_REPO}")
endif()

# Valgrind does not exist on every host this suite runs on -- not on macOS
# arm64, which it has never supported. A skip is the answer there; a failure
# would say the comparison regressed when the tool is absent.
find_program(VALGRIND_EXECUTABLE valgrind)
if(NOT VALGRIND_EXECUTABLE)
  message(STATUS "valgrind unavailable; skipping the C serdes instruction comparison")
  # 77 is the SKIP_RETURN_CODE this test is registered with.
  cmake_language(EXIT 77)
endif()

if(NOT DEFINED ITERATIONS OR "${ITERATIONS}" STREQUAL "")
  set(ITERATIONS 1000)
endif()
# The flags are the measurement's subject as much as the generated code is: at
# -O0 both C sides are measured as written rather than as built, and the peer's
# inline bodies in particular are a different program once the optimiser has seen
# them. NDEBUG for the same reason -- the runtime's guards are asserts, a release
# caller does not carry them, and they do not fall evenly on the two C sides.
# These reach the C compiler only; the object column is dsdlc's own pipeline.
#
# Spelled as one string and split here, like DSDLC_EXTRA_ARGS below. A CMake list
# arrives from `add_test` as separate arguments, and `-DNDEBUG` among them reads
# as a second cache variable to define.
if(NOT DEFINED OPTIMISATION_FLAGS OR "${OPTIMISATION_FLAGS}" STREQUAL "")
  set(optimisation_flags -O2 -DNDEBUG)
else()
  separate_arguments(optimisation_flags NATIVE_COMMAND "${OPTIMISATION_FLAGS}")
endif()

set(dsdlc_extra_args "")
if(DEFINED DSDLC_EXTRA_ARGS AND NOT "${DSDLC_EXTRA_ARGS}" STREQUAL "")
  separate_arguments(dsdlc_extra_args NATIVE_COMMAND "${DSDLC_EXTRA_ARGS}")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/DifferentialCorpus.cmake")
llvmdsdl_differential_corpus(
  OUT_DIR "${OUT_DIR}"
  DSDLC_EXTRA_ARGS ${dsdlc_extra_args}
  EXTRA_C_FLAGS ${optimisation_flags}
  OBJECTS_VAR c_objects
  BUILD_DIR_VAR build_out
  NUNAVUT_OBJECT_VAR nunavut_object
)
llvmdsdl_differential_obj_corpus(
  OUT_DIR "${OUT_DIR}"
  DSDLC_EXTRA_ARGS ${dsdlc_extra_args}
  EXTRA_C_FLAGS ${optimisation_flags}
  OBJECTS_VAR obj_objects
)

set(main_c "${SOURCE_ROOT}/test/integration/SerdesInstructionComparisonMain.c")
if(NOT EXISTS "${main_c}")
  message(FATAL_ERROR "harness source missing: ${main_c}")
endif()

set(main_obj "${build_out}/serdes_instruction_comparison_main.o")
execute_process(
  COMMAND
    "${C_COMPILER}"
      ${LLVMDSDL_DIFFERENTIAL_BASE_C_FLAGS}
      ${optimisation_flags}
      -I "${SOURCE_ROOT}/test/integration"
      -c "${main_c}"
      -o "${main_obj}"
  RESULT_VARIABLE cc_result
  OUTPUT_VARIABLE cc_stdout
  ERROR_VARIABLE cc_stderr
)
if(NOT cc_result EQUAL 0)
  message(STATUS "compiler stdout:\n${cc_stdout}")
  message(STATUS "compiler stderr:\n${cc_stderr}")
  message(FATAL_ERROR "failed to compile the instruction comparison harness")
endif()

# One driver per implementation of ours, each carrying the same harness and the
# same peer. The two cannot be one binary: the compiled C and the emitted
# objects define the same entry points.
function(llvmdsdl_link_driver name)
  set(exe "${build_out}/${name}")
  execute_process(
    COMMAND
      "${C_COMPILER}"
        ${optimisation_flags}
        "${main_obj}"
        ${ARGN}
        -o "${exe}"
    RESULT_VARIABLE link_result
    OUTPUT_VARIABLE link_stdout
    ERROR_VARIABLE link_stderr
  )
  if(NOT link_result EQUAL 0)
    message(STATUS "link stdout:\n${link_stdout}")
    message(STATUS "link stderr:\n${link_stderr}")
    message(FATAL_ERROR "failed to link ${name}")
  endif()
endfunction()

llvmdsdl_link_driver(serdes_comparison_c ${c_objects})
llvmdsdl_link_driver(serdes_comparison_obj "${nunavut_object}" ${obj_objects})
set(driver_c "${build_out}/serdes_comparison_c")
set(driver_obj "${build_out}/serdes_comparison_obj")

# The harness reads the iteration count from a fixed number of digits, so that
# parsing it costs the same at zero as at the full count and drops out of the
# difference along with everything else fixed.
set(iteration_digits 9)

function(llvmdsdl_pad_count value out_var)
  string(LENGTH "${value}" length)
  if(length GREATER iteration_digits)
    message(FATAL_ERROR "iteration count ${value} exceeds ${iteration_digits} digits")
  endif()
  math(EXPR padding "${iteration_digits} - ${length}")
  set(text "${value}")
  if(padding GREATER 0)
    string(REPEAT "0" ${padding} zeros)
    set(text "${zeros}${value}")
  endif()
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

## Runs one measurement under cachegrind and returns the program's instruction
## count along with the fixture the harness reported.
function(llvmdsdl_measure driver tag case_index mode operation count
                          ir_var name_var fixture_var source_var digest_var)
  llvmdsdl_pad_count("${count}" padded)
  set(profile "${build_out}/cachegrind-${tag}-${case_index}-${mode}-${operation}-${count}.out")
  # --cache-sim/--branch-sim off: those are simulations of one particular cache
  # geometry, which is a different question from how many instructions ran.
  execute_process(
    COMMAND
      "${VALGRIND_EXECUTABLE}" --tool=cachegrind --cache-sim=no --branch-sim=no
        "--cachegrind-out-file=${profile}"
        "${driver}" "${case_index}" "${mode}" "${operation}" "${padded}"
    RESULT_VARIABLE run_result
    OUTPUT_VARIABLE run_stdout
    ERROR_VARIABLE run_stderr
  )
  if(NOT run_result EQUAL 0)
    message(STATUS "harness stdout:\n${run_stdout}")
    message(STATUS "harness stderr:\n${run_stderr}")
    message(FATAL_ERROR
      "measurement failed: ${tag} case ${case_index} ${mode} ${operation} at ${count} iterations")
  endif()

  if(NOT run_stderr MATCHES "I +refs: +([0-9][0-9,]*)")
    message(STATUS "cachegrind output:\n${run_stderr}")
    message(FATAL_ERROR "could not read the instruction count from cachegrind's summary")
  endif()
  string(REPLACE "," "" instructions "${CMAKE_MATCH_1}")
  set(${ir_var} "${instructions}" PARENT_SCOPE)

  if(NOT run_stdout MATCHES
     "FIXTURE ([^ \n]+) wire_bytes=([0-9]+) source=([a-z]+) digest=([0-9a-f]+)")
    message(STATUS "harness stdout:\n${run_stdout}")
    message(FATAL_ERROR "the harness did not report which fixture it measured")
  endif()
  set(${name_var} "${CMAKE_MATCH_1}" PARENT_SCOPE)
  set(${fixture_var} "${CMAKE_MATCH_2}" PARENT_SCOPE)
  set(${source_var} "${CMAKE_MATCH_3}" PARENT_SCOPE)
  set(${digest_var} "${CMAKE_MATCH_4}" PARENT_SCOPE)
endfunction()

## Renders a count in thousandths as a decimal.
function(llvmdsdl_format_milli value out_var)
  math(EXPR whole "${value} / 1000")
  math(EXPR fraction "${value} % 1000")
  string(LENGTH "${fraction}" length)
  math(EXPR padding "3 - ${length}")
  if(padding GREATER 0)
    string(REPEAT "0" ${padding} zeros)
    set(fraction "${zeros}${fraction}")
  endif()
  set(${out_var} "${whole}.${fraction}" PARENT_SCOPE)
endfunction()

execute_process(
  COMMAND "${VALGRIND_EXECUTABLE}" --version
  OUTPUT_VARIABLE valgrind_version
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
)
cmake_host_system_information(RESULT arch QUERY OS_PLATFORM)
string(REPLACE ";" " " base_flags_text "${LLVMDSDL_DIFFERENTIAL_BASE_C_FLAGS}")
string(REPLACE ";" " " optimisation_text "${optimisation_flags}")
string(REPLACE ";" " " dsdlc_extra_text "${dsdlc_extra_args}")

list(LENGTH LLVMDSDL_DIFFERENTIAL_DEFINITIONS definition_count)
message(STATUS
  "C serdes instruction comparison: ${arch}, ${ITERATIONS} iterations, "
  "${base_flags_text} ${optimisation_text}")
if(NOT "${dsdlc_extra_text}" STREQUAL "")
  message(STATUS "  dsdlc extra arguments: ${dsdlc_extra_text}")
endif()

# Ten cases from nine definitions: ExecuteCommand contributes a request and a
# response. The harness rejects an index past its last case, which is how this
# count is held to the table it indexes.
set(case_count 10)
execute_process(
  COMMAND "${driver_c}" "${case_count}" dsdlc encode 000000000
  RESULT_VARIABLE overrun_result
  OUTPUT_QUIET
  ERROR_QUIET
)
if(overrun_result EQUAL 0)
  message(FATAL_ERROR
    "the harness accepted case index ${case_count}, so it now carries more cases than this "
    "lane measures. Raise case_count here.")
endif()

# Each entry is <report key>|<driver>|<mode>. The peer and the empty loop are
# measured in both drivers: they are the same code either side, so a difference
# would mean the two are not measuring the same thing and the comparison between
# them would be meaningless.
set(measurements
    "c|${driver_c}|dsdlc"
    "nnvg|${driver_c}|nnvg"
    "noop|${driver_c}|noop"
    "obj|${driver_obj}|dsdlc"
    "nnvgFromObjDriver|${driver_obj}|nnvg"
    "noopFromObjDriver|${driver_obj}|noop")

set(report_cases "")

math(EXPR last_case "${case_count} - 1")
foreach(case_index RANGE 0 ${last_case})
  set(case_name "")
  set(wire_bytes "")
  set(wire_source "")
  set(wire_digest "")
  set(case_operations "")
  set(case_summaries "")
  foreach(operation encode decode)
    set(raw_by_key "")
    foreach(spec IN LISTS measurements)
      string(REPLACE "|" ";" parts "${spec}")
      list(GET parts 0 key)
      list(GET parts 1 driver)
      list(GET parts 2 mode)

      llvmdsdl_measure("${driver}" "${key}" "${case_index}" "${mode}" "${operation}" 0
                       ir_idle name fixture source digest)
      llvmdsdl_measure("${driver}" "${key}" "${case_index}" "${mode}" "${operation}" "${ITERATIONS}"
                       ir_full name_full fixture_full source_full digest_full)
      if(NOT "${digest}" STREQUAL "${digest_full}")
        message(FATAL_ERROR
          "case ${case_index} ${key} ${operation} measured two different fixtures at its two "
          "iteration counts, so their difference is not a per-iteration cost.")
      endif()

      if(NOT "${wire_digest}" STREQUAL "" AND NOT "${digest}" STREQUAL "${wire_digest}")
        message(FATAL_ERROR
          "case ${case_index}: ${key} was measured on fixture ${digest} (${fixture} bytes, "
          "${source}) while an earlier measurement used ${wire_digest} (${wire_bytes} bytes, "
          "${wire_source}). The implementations are not being compared on the same payload.")
      endif()
      set(case_name "${name}")
      set(wire_bytes "${fixture}")
      set(wire_source "${source}")
      set(wire_digest "${digest}")

      math(EXPR per_milli "((${ir_full} - ${ir_idle}) * 1000) / ${ITERATIONS}")
      if(per_milli LESS_EQUAL 0)
        message(FATAL_ERROR
          "case ${case_index} ${key} ${operation} measured ${per_milli} thousandths of an "
          "instruction per iteration. A loop that costs nothing was optimised away, and the "
          "difference this lane subtracts is not a measurement of anything.")
      endif()
      list(APPEND raw_by_key "\"${key}\": {\"irAtZero\": ${ir_idle}, \"irAtFull\": ${ir_full}}")
      set(per_milli_${key} "${per_milli}")
    endforeach()

    # The peer and the empty loop are the same code in both drivers, so the two
    # figures for each are a check that the drivers are measuring the same thing.
    # What pins that exactly is the fixture digest, compared above; this is a
    # bound on the rest. Two separately linked binaries do not execute the same
    # count for the same source: the buffers are pinned to one alignment, but
    # each driver still lays its code out its own way and reaches a library
    # routine from its own call site, which cost an instruction in 167 on x86_64
    # and two in 121 under `-flto`. A different payload or a driver linked
    # against the wrong objects misses by far more than a twentieth.
    foreach(shared nnvg noop)
      set(from_c_driver "${per_milli_${shared}}")
      set(from_obj_driver "${per_milli_${shared}FromObjDriver}")
      math(EXPR spread "${from_c_driver} - ${from_obj_driver}")
      if(spread LESS 0)
        math(EXPR spread "0 - ${spread}")
      endif()
      # A twentieth of the figure, or one instruction, whichever is the larger.
      math(EXPR allowed "${from_c_driver} / 20")
      if(allowed LESS 1000)
        set(allowed 1000)
      endif()
      if(spread GREATER allowed)
        message(FATAL_ERROR
          "case ${case_index} ${operation}: ${shared} costs ${from_c_driver} thousandths of an "
          "instruction in the C driver and ${from_obj_driver} in the object driver, further "
          "apart than the ${allowed} this allows. Beyond layout, so the two drivers are not "
          "measuring the same thing.")
      endif()
    endforeach()

    # Each implementation carries the scaffolding of the driver it was measured
    # in, so that is the figure subtracted from it.
    set(scaffolding_c "${per_milli_noop}")
    set(scaffolding_obj "${per_milli_noopFromObjDriver}")
    set(scaffolding_nnvg "${per_milli_noop}")
    foreach(key c obj nnvg)
      math(EXPR net_${key} "${per_milli_${key}} - ${scaffolding_${key}}")
      if(net_${key} LESS_EQUAL 0)
        message(FATAL_ERROR
          "case ${case_index} ${key} ${operation} costs no more than the empty loop around it, "
          "so either the call was elided or the scaffolding measurement is wrong.")
      endif()
    endforeach()

    math(EXPR ratio_c_permille "(${net_c} * 1000) / ${net_nnvg}")
    math(EXPR ratio_obj_permille "(${net_obj} * 1000) / ${net_nnvg}")
    math(EXPR ratio_obj_c_permille "(${net_obj} * 1000) / ${net_c}")
    llvmdsdl_format_milli("${net_c}" c_text)
    llvmdsdl_format_milli("${net_obj}" obj_text)
    llvmdsdl_format_milli("${net_nnvg}" nnvg_text)
    llvmdsdl_format_milli("${ratio_c_permille}" ratio_c_text)
    llvmdsdl_format_milli("${ratio_obj_permille}" ratio_obj_text)
    llvmdsdl_format_milli("${ratio_obj_c_permille}" ratio_obj_c_text)

    string(REPLACE ";" ", " raw_text "${raw_by_key}")
    list(APPEND case_operations "\"${operation}\": {${raw_text}}")
    set(summary
        "  ${operation}: nnvg ${nnvg_text} Ir, c ${c_text} (${ratio_c_text}), obj ${obj_text} (${ratio_obj_text}), obj/c ${ratio_obj_c_text}")
    list(APPEND case_summaries "${summary}")
  endforeach()

  message(STATUS
    "${case_name} (${wire_bytes} wire bytes, ${wire_source} fixture ${wire_digest})")
  foreach(line IN LISTS case_summaries)
    message(STATUS "${line}")
  endforeach()

  string(REPLACE ";" ", " case_operations_text "${case_operations}")
  list(APPEND report_cases
    "    {\"index\": ${case_index}, \"name\": \"${case_name}\", \"wireBytes\": ${wire_bytes}, \"fixtureSource\": \"${wire_source}\", \"fixtureDigest\": \"${wire_digest}\", ${case_operations_text}}")
endforeach()

string(REPLACE ";" ",\n" report_cases_text "${report_cases}")
file(WRITE "${REPORT_JSON}"
"{
  \"kind\": \"c-serdes-instruction-comparison\",
  \"meta\": {
    \"arch\": \"${arch}\",
    \"iterations\": ${ITERATIONS},
    \"definitions\": ${definition_count},
    \"cCompiler\": \"${C_COMPILER}\",
    \"cFlags\": \"${base_flags_text} ${optimisation_text}\",
    \"dsdlcExtraArguments\": \"${dsdlc_extra_text}\",
    \"valgrind\": \"${valgrind_version}\",
    \"implementations\": \"c: dsdlc's C compiled by cCompiler at cFlags. obj: dsdlc's own objects, its LLVM pipeline at O2. nnvg: the peer's C, compiled like ours.\",
    \"perIteration\": \"(irAtFull - irAtZero) / iterations; the noop figure is the loop around the call and is subtracted from every implementation\"
  },
  \"cases\": [
${report_cases_text}
  ]
}
")
message(STATUS "Wrote ${REPORT_JSON}")
