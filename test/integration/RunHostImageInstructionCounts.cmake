# Instruction-count gate for the folded host-image bodies.
#
# The fold replaces a host-image body's field work with one move, and what that bought was first
# measured by reading the object target's entry points off `llvm-objdump`. This lane keeps those
# numbers: the fixtures are generated as objects for each pinned triple, every baselined symbol is
# disassembled, and its instruction count must equal the baseline. A static count is a property of
# the emitted code -- exact, the same on every host that can target the triple, and readable
# without a simulator -- so this gates where the cachegrind lanes skip, Apple Silicon included.
#
# What a static count does not see: how often an instruction runs. A branch the fold adds costs
# the same here whether it is taken or not. The cachegrind lanes answer that where they run.
#
# The count moves when the compiler moves, so the baseline is keyed by LLVM major as well as by
# triple. A key with no entry skips and prints the values to paste; a deliberate change to the
# lowering re-baselines in the commit that makes it.
cmake_minimum_required(VERSION 3.24)

foreach(var DSDLC OUT_DIR LLVM_OBJDUMP FIXTURE_ROOT BASELINE_JSON LLVM_VERSION_MAJOR)
  if(NOT DEFINED ${var} OR "${${var}}" STREQUAL "")
    message(FATAL_ERROR "Missing required variable: ${var}")
  endif()
endforeach()

file(READ "${BASELINE_JSON}" baseline_json)
string(JSON triple_count LENGTH "${baseline_json}" meta triples)
string(JSON symbol_count LENGTH "${baseline_json}" meta symbols)
math(EXPR triple_last "${triple_count} - 1")
math(EXPR symbol_last "${symbol_count} - 1")
set(key "llvm-${LLVM_VERSION_MAJOR}")

file(REMOVE_RECURSE "${OUT_DIR}")
file(MAKE_DIRECTORY "${OUT_DIR}")

# Every triple is measured before any is judged, so a skip can print the whole block to paste.
set(paste "")
set(failures "")
set(missing FALSE)
foreach(t RANGE ${triple_last})
  string(JSON triple GET "${baseline_json}" meta triples ${t})
  set(obj_dir "${OUT_DIR}/${triple}")
  execute_process(
    COMMAND "${DSDLC}" --target-language obj --target-triple "${triple}" "${FIXTURE_ROOT}" --outdir "${obj_dir}"
    RESULT_VARIABLE gen_result
    OUTPUT_VARIABLE gen_stdout
    ERROR_VARIABLE gen_stderr
  )
  if(NOT gen_result EQUAL 0)
    message(STATUS "dsdlc stdout:\n${gen_stdout}")
    message(STATUS "dsdlc stderr:\n${gen_stderr}")
    message(FATAL_ERROR "object generation for ${triple} failed")
  endif()
  file(GLOB_RECURSE objects "${obj_dir}/*.o")
  if("${objects}" STREQUAL "")
    message(FATAL_ERROR "object generation for ${triple} produced no objects under ${obj_dir}")
  endif()

  # The view fixture, generated twice: held as views, and as the decoded copies the plain run makes.
  # A symbol prefixed `views:` or `plain:` is counted in the matching objects; the rest in the
  # host-image objects above.
  set(objects_views "")
  set(objects_plain "")
  if(DEFINED VIEW_ROOT AND NOT "${VIEW_ROOT}" STREQUAL "")
    foreach(scope views plain)
      set(scope_dir "${obj_dir}-${scope}")
      set(scope_args "")
      if(scope STREQUAL "views")
        set(scope_args --aliasable-views)
      endif()
      execute_process(
        COMMAND "${DSDLC}" --target-language obj --target-triple "${triple}" ${scope_args} "${VIEW_ROOT}"
          -I "${VIEW_INCLUDE}" --outdir "${scope_dir}"
        RESULT_VARIABLE gen_result
        OUTPUT_VARIABLE gen_stdout
        ERROR_VARIABLE gen_stderr
      )
      if(NOT gen_result EQUAL 0)
        message(STATUS "dsdlc stdout:\n${gen_stdout}")
        message(STATUS "dsdlc stderr:\n${gen_stderr}")
        message(FATAL_ERROR "${scope} object generation for ${triple} failed")
      endif()
      file(GLOB_RECURSE objects_${scope} "${scope_dir}/*.o")
      if("${objects_${scope}}" STREQUAL "")
        message(FATAL_ERROR "${scope} object generation for ${triple} produced no objects under ${scope_dir}")
      endif()
    endforeach()
  endif()

  string(APPEND paste "      \"${triple}\": {\n")
  foreach(s RANGE ${symbol_last})
    string(JSON symbol GET "${baseline_json}" meta symbols ${s})
    set(scoped_objects "${objects}")
    set(bare "${symbol}")
    if(symbol MATCHES "^(views|plain):(.+)$")
      set(scoped_objects "${objects_${CMAKE_MATCH_1}}")
      set(bare "${CMAKE_MATCH_2}")
      if("${scoped_objects}" STREQUAL "")
        message(FATAL_ERROR "${symbol} is baselined but no VIEW_ROOT was given to generate it from")
      endif()
    endif()
    execute_process(
      COMMAND "${LLVM_OBJDUMP}" -d --no-show-raw-insn "--disassemble-symbols=${bare}" ${scoped_objects}
      RESULT_VARIABLE dis_result
      OUTPUT_VARIABLE dis_stdout
      ERROR_VARIABLE dis_stderr
    )
    if(NOT dis_result EQUAL 0)
      message(STATUS "llvm-objdump stderr:\n${dis_stderr}")
      message(FATAL_ERROR "llvm-objdump failed on ${symbol} for ${triple}")
    endif()
    # One instruction per line of the form `   <address>: <mnemonic> ...`; the file and symbol
    # headers carry no colon after a bare address. A symbol no object holds disassembles to
    # nothing, and llvm-objdump only warns, so an empty count is the failure here.
    string(REGEX MATCHALL "\n[ \t]+[0-9a-f]+:" instructions "\n${dis_stdout}")
    list(LENGTH instructions observed)
    if(observed EQUAL 0)
      message(STATUS "llvm-objdump stderr:\n${dis_stderr}")
      message(FATAL_ERROR "no instructions found for ${symbol} in the ${triple} objects; the entry point's name changed, or it was not emitted")
    endif()
    message(STATUS "${triple} ${symbol}: ${observed}")
    if(s EQUAL symbol_last)
      string(APPEND paste "        \"${symbol}\": ${observed}\n")
    else()
      string(APPEND paste "        \"${symbol}\": ${observed},\n")
    endif()

    string(JSON expected ERROR_VARIABLE lookup_error GET "${baseline_json}" counts "${key}" "${triple}" "${symbol}")
    if(lookup_error OR "${expected}" STREQUAL "")
      set(missing TRUE)
    elseif(NOT observed EQUAL expected)
      string(APPEND failures "  ${triple} ${symbol}: ${observed} instructions, baseline ${expected}\n")
    endif()
  endforeach()
  if(t EQUAL triple_last)
    string(APPEND paste "      }\n")
  else()
    string(APPEND paste "      },\n")
  endif()
endforeach()

if(missing)
  # A skip, not a pass: there is no number for this compiler, so there is nothing to compare
  # against. The block below is the entry to add.
  message(STATUS
    "No instruction-count baseline for '${key}'. Add one to ${BASELINE_JSON}:\n"
    "    \"${key}\": {\n${paste}    }")
  cmake_language(EXIT 77)
endif()

if(NOT "${failures}" STREQUAL "")
  message(STATUS "Instruction-count changes:\n${failures}")
  message(STATUS "Measured:\n    \"${key}\": {\n${paste}    }")
  message(FATAL_ERROR
    "a generated entry point's instruction count differs from its baseline. The count is a "
    "property of the emitted code, so this is a change in the lowering or in the compiler, not "
    "noise. If it is intended, re-baseline ${BASELINE_JSON} in the same commit that causes it.")
endif()

message(STATUS "host-image instruction counts match the ${key} baseline for every pinned triple")
