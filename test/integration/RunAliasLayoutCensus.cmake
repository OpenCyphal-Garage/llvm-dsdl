#===----------------------------------------------------------------------===#
##
## @file
## Checks the two layout verdicts against the catalogue and against the compiler.
##
## MODE=census counts how many sections hold each verdict. The counts are the feature's size, so a
## predicate change that moves one has to move this file with it.
##
## MODE=reality compiles the generated types and asks the host compiler what it actually laid out.
## `host_image` claims a structure is a byte image of its own wire form; here that claim is tested
## by deserialising into the structure and comparing its bytes with the wire's. A claim the compiler
## contradicts is a defect. A refusal the compiler disputes is not: refusing is the safe direction,
## and a delimited type is refused on purpose, because its payload may be longer or shorter than
## this version's layout and this check only ever presents it with its own length.
##
#===----------------------------------------------------------------------===#

cmake_minimum_required(VERSION 3.22)

foreach(required DSDLC OUT_DIR MODE)
  if(NOT DEFINED ${required})
    message(FATAL_ERROR "${required} must be defined")
  endif()
endforeach()

file(REMOVE_RECURSE "${OUT_DIR}")
file(MAKE_DIRECTORY "${OUT_DIR}")

# `obj` emits the headers the census reads and the objects the reality probe links.
execute_process(
  COMMAND "${DSDLC}" --target-language obj --outdir "${OUT_DIR}/gen" +uavcan
  RESULT_VARIABLE generate_result
  OUTPUT_VARIABLE generate_stdout
  ERROR_VARIABLE generate_stderr
)
if(NOT generate_result EQUAL 0)
  message(STATUS "${generate_stdout}")
  message(STATUS "${generate_stderr}")
  message(FATAL_ERROR "failed to generate the uavcan catalogue")
endif()

# Collect one record per section that states a verdict: its type name, the header that declares it,
# both verdicts and both reasons.
file(GLOB_RECURSE catalogue_headers "${OUT_DIR}/gen/*.h")
list(SORT catalogue_headers)

set(type_names "")
set(wire_flat_count 0)
set(host_image_count 0)
set(unknown_reasons "")

foreach(header IN LISTS catalogue_headers)
  file(RELATIVE_PATH header_relative "${OUT_DIR}/gen" "${header}")
  file(READ "${header}" header_text)

  file(STRINGS "${header}" wire_flat_lines REGEX "^#define [A-Za-z0-9_]+_WIRE_FLAT_ (true|false)$")
  foreach(line IN LISTS wire_flat_lines)
    string(REGEX REPLACE "^#define ([A-Za-z0-9_]+)_WIRE_FLAT_ (true|false)$" "\\1" type_name "${line}")
    string(REGEX REPLACE "^#define ([A-Za-z0-9_]+)_WIRE_FLAT_ (true|false)$" "\\2" wire_flat "${line}")

    string(REGEX MATCH "#define ${type_name}_HOST_IMAGE_ (true|false)" host_image_match "${header_text}")
    string(REGEX REPLACE ".*_HOST_IMAGE_ (true|false)" "\\1" host_image "${host_image_match}")
    string(REGEX MATCH "#define ${type_name}_WIRE_FLAT_REASON_ \"([a-z-]+)\"" reason_match "${header_text}")
    string(REGEX REPLACE ".*_WIRE_FLAT_REASON_ \"([a-z-]+)\"" "\\1" wire_flat_reason "${reason_match}")

    if(wire_flat STREQUAL "true")
      math(EXPR wire_flat_count "${wire_flat_count} + 1")
    endif()
    if(host_image STREQUAL "true")
      math(EXPR host_image_count "${host_image_count} + 1")
    endif()
    # Lowering states one of the pair for every section, so this spelling means one went missing.
    if(wire_flat_reason STREQUAL "unknown")
      list(APPEND unknown_reasons "${type_name}")
    endif()

    list(APPEND type_names "${type_name}")
    set("record_${type_name}_header" "${header_relative}")
    set("record_${type_name}_host_image" "${host_image}")
    set("record_${type_name}_wire_flat_reason" "${wire_flat_reason}")
    # Only a section with a structure can be laid out, and only those the probe can measure.
    string(FIND "${header_text}" "typedef struct ${type_name} {" struct_position)
    if(struct_position EQUAL -1)
      set("record_${type_name}_has_struct" "no")
    else()
      set("record_${type_name}_has_struct" "yes")
    endif()
  endforeach()
endforeach()

list(LENGTH type_names section_count)
message(STATUS "sections stating a verdict: ${section_count}")
message(STATUS "wire_flat holds: ${wire_flat_count}")
message(STATUS "host_image holds: ${host_image_count}")

if(unknown_reasons)
  message(FATAL_ERROR
    "these sections carry neither verdict nor reason, so the lowering dropped one: ${unknown_reasons}")
endif()

if(MODE STREQUAL "census")
  if(NOT section_count EQUAL EXPECTED_SECTIONS)
    message(FATAL_ERROR "expected ${EXPECTED_SECTIONS} sections, counted ${section_count}")
  endif()
  if(NOT wire_flat_count EQUAL EXPECTED_WIRE_FLAT)
    message(FATAL_ERROR
      "wire_flat holds for ${wire_flat_count} sections, expected ${EXPECTED_WIRE_FLAT}. "
      "A predicate change moves this number; update it here with the reason in the commit.")
  endif()
  if(NOT host_image_count EQUAL EXPECTED_HOST_IMAGE)
    message(FATAL_ERROR
      "host_image holds for ${host_image_count} sections, expected ${EXPECTED_HOST_IMAGE}. "
      "A predicate or storage-width change moves this number; update it here with the reason.")
  endif()
  # host_image is the stricter of the two and cannot hold where wire_flat does not.
  if(host_image_count GREATER wire_flat_count)
    message(FATAL_ERROR "host_image holds more often than wire_flat")
  endif()
  message(STATUS "census holds")
  return()
endif()

if(NOT MODE STREQUAL "reality")
  message(FATAL_ERROR "MODE must be 'census' or 'reality'")
endif()

if(NOT DEFINED C_COMPILER OR C_COMPILER STREQUAL "")
  message(STATUS "no C compiler; skipping the layout reality check")
  return()
endif()

# Build a probe that asks the compiler what it laid out. Each section fills a wire buffer with a
# deterministic pattern, deserialises it, and reports whether the structure's bytes are that buffer.
set(probe_includes "")
set(probe_body "")
set(measured_types "")
foreach(type_name IN LISTS type_names)
  if(NOT record_${type_name}_has_struct STREQUAL "yes")
    continue()
  endif()
  list(APPEND measured_types "${type_name}")
  set(probe_includes "${probe_includes}#include \"${record_${type_name}_header}\"\n")
  # One string per section: a `set()` taking several arguments would join them as a list, and the
  # separator it joins with is the one C ends its statements with.
  #
  # The wire buffer is sized from the section rather than shared across them: the catalogue's
  # largest serialises to 9262 bytes, and a buffer that holds fewer is written past.
  string(CONCAT probe_body "${probe_body}"
      "  {\n"
      "    ${type_name} object;\n"
      "    memset(&object, 0, sizeof object);\n"
      "    static uint8_t wire[${type_name}_SERIALIZATION_BUFFER_SIZE_BYTES_];\n"
      "    const size_t wire_bytes = ${type_name}_SERIALIZATION_BUFFER_SIZE_BYTES_;\n"
      "    for (size_t i = 0; i < wire_bytes; ++i) { wire[i] = next_byte(); }\n"
      "    size_t consumed = wire_bytes;\n"
      "    (void) ${type_name}__deserialize_(&object, wire, &consumed);\n"
      "    const int is_image = (sizeof object == wire_bytes) && (memcmp(&object, wire, wire_bytes) == 0);\n"
      "    printf(\"%s %d\\n\", \"${type_name}\", is_image);\n"
      "  }\n")
endforeach()

file(WRITE "${OUT_DIR}/layout_probe.c"
  "/* Written by RunAliasLayoutCensus.cmake. */\n"
  "#include <stdio.h>\n#include <stdint.h>\n#include <string.h>\n"
  "${probe_includes}"
  "static uint32_t state = 2463534242u;\n"
  "static uint8_t next_byte(void)\n"
  "{\n"
  "  state ^= state << 13; state ^= state >> 17; state ^= state << 5;\n"
  "  return (uint8_t) state;\n"
  "}\n"
  "int main(void)\n{\n"
  "${probe_body}"
  "  return 0;\n}\n")

file(GLOB_RECURSE catalogue_objects "${OUT_DIR}/gen/*.o")
if(NOT catalogue_objects)
  message(FATAL_ERROR "the object backend emitted no objects to link")
endif()

execute_process(
  COMMAND "${C_COMPILER}" -std=c11 -w -I "${OUT_DIR}/gen"
          "${OUT_DIR}/layout_probe.c" ${catalogue_objects}
          -o "${OUT_DIR}/layout_probe"
  RESULT_VARIABLE compile_result
  OUTPUT_VARIABLE compile_stdout
  ERROR_VARIABLE compile_stderr
)
if(NOT compile_result EQUAL 0)
  message(STATUS "${compile_stdout}")
  message(STATUS "${compile_stderr}")
  message(FATAL_ERROR "failed to build the layout probe")
endif()

execute_process(
  COMMAND "${OUT_DIR}/layout_probe"
  RESULT_VARIABLE probe_result
  OUTPUT_VARIABLE probe_stdout
  ERROR_VARIABLE probe_stderr
)
if(NOT probe_result EQUAL 0)
  message(STATUS "${probe_stderr}")
  message(FATAL_ERROR "the layout probe did not run")
endif()

string(REGEX MATCHALL "[^\r\n]+" probe_lines "${probe_stdout}")
set(contradicted "")
set(disputed_refusals "")
set(agreements 0)
foreach(line IN LISTS probe_lines)
  string(REGEX REPLACE "^([A-Za-z0-9_]+) ([01])$" "\\1" type_name "${line}")
  string(REGEX REPLACE "^([A-Za-z0-9_]+) ([01])$" "\\2" measured "${line}")
  set(claimed "${record_${type_name}_host_image}")
  if((claimed STREQUAL "true" AND measured EQUAL 1) OR (claimed STREQUAL "false" AND measured EQUAL 0))
    math(EXPR agreements "${agreements} + 1")
  elseif(claimed STREQUAL "true")
    list(APPEND contradicted "${type_name}")
  else()
    list(APPEND disputed_refusals "${type_name} (${record_${type_name}_wire_flat_reason})")
    # Refusing is the safe direction, and a delimited type is refused on purpose.
    if(NOT record_${type_name}_wire_flat_reason STREQUAL "not-sealed")
      list(APPEND contradicted "${type_name}")
    endif()
  endif()
endforeach()

list(LENGTH measured_types measured_count)
message(STATUS "measured ${measured_count} sections, ${agreements} agree with the compiler")
if(disputed_refusals)
  message(STATUS "refused but laid out as an image anyway, all delimited: ${disputed_refusals}")
endif()

if(contradicted)
  message(FATAL_ERROR
    "host_image disagrees with the compiler for: ${contradicted}. A section claiming to be a byte "
    "image that is not is a defect; so is a refusal this check cannot account for.")
endif()

message(STATUS "every host_image claim holds on this target")
