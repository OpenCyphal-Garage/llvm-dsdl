cmake_minimum_required(VERSION 3.24)

foreach(var DSDLC UAVCAN_ROOT OUT_DIR C_COMPILER)
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

# This lane's whole assertion is that every definition in the corpus produced an output, which is
# what catches a type going missing silently. The default generates only the newest version of each
# type, so the lane asks for the whole corpus explicitly -- otherwise it would be comparing the
# corpus against a deliberate subset of it, and would have to be weakened to a number nobody can
# check by eye.

execute_process(
  COMMAND
    "${DSDLC}" --target-language c --all-type-versions
      "${UAVCAN_ROOT}"
      --outdir "${OUT_DIR}"
  RESULT_VARIABLE gen_result
  OUTPUT_VARIABLE gen_stdout
  ERROR_VARIABLE gen_stderr
)
if(NOT gen_result EQUAL 0)
  message(STATUS "dsdlc stdout:\n${gen_stdout}")
  message(STATUS "dsdlc stderr:\n${gen_stderr}")
  message(FATAL_ERROR "uavcan generation failed")
endif()

file(GLOB_RECURSE dsdl_files "${UAVCAN_ROOT}/*.dsdl")
list(LENGTH dsdl_files dsdl_count)

file(GLOB_RECURSE generated_headers "${OUT_DIR}/*.h")
set(filtered_headers "")
foreach(h IN LISTS generated_headers)
  get_filename_component(name "${h}" NAME)
  if(NOT name STREQUAL "dsdl_runtime.h")
    list(APPEND filtered_headers "${h}")
  endif()
endforeach()
list(LENGTH filtered_headers header_count)

if(NOT dsdl_count EQUAL header_count)
  message(FATAL_ERROR
    "header count mismatch: dsdl=${dsdl_count}, generated=${header_count}")
endif()

set(stub_hits "")
foreach(h IN LISTS filtered_headers)
  file(READ "${h}" header_text)
  string(FIND "${header_text}" "dsdl_runtime_stub_" hit_pos)
  if(NOT hit_pos EQUAL -1)
    list(APPEND stub_hits "${h}")
  endif()
endforeach()
if(stub_hits)
  message(FATAL_ERROR
    "generated headers still reference dsdl_runtime_stub_: ${stub_hits}")
endif()

file(GLOB_RECURSE generated_impls "${OUT_DIR}/*.c")
set(generic_lowering_hits "")
set(missing_capacity_call_hits "")
set(missing_capacity_helper_hits "")
set(found_union_tag_call 0)
set(found_union_tag_helper 0)
set(found_union_tag_io_call 0)
set(found_union_tag_io_helper 0)
set(found_scalar_unsigned_call 0)
set(found_scalar_unsigned_helper 0)
set(found_scalar_signed_call 0)
set(found_scalar_signed_helper 0)
set(found_scalar_float_call 0)
set(found_scalar_float_helper 0)
set(found_array_len_prefix_call 0)
set(found_array_len_prefix_helper 0)
set(found_array_len_validate_call 0)
set(found_array_len_validate_helper 0)
set(found_delimiter_header_io 0)
set(found_delimiter_header_write 0)
set(found_delimiter_validate_call 0)
set(found_delimiter_validate_helper 0)
set(array_length_fallback_hits "")
set(scalar_saturation_fallback_hits "")
set(delimiter_fallback_hits "")
set(scalar_deser_fallback_hits "")
set(array_length_prefix_fallback_hits "")
set(union_tag_io_fallback_hits "")
foreach(c_file IN LISTS generated_impls)
  file(READ "${c_file}" impl_text)
  string(FIND "${impl_text}" "Generic bitstream mapping" hit_pos)
  if(NOT hit_pos EQUAL -1)
    list(APPEND generic_lowering_hits "${c_file}")
  endif()
  string(FIND "${impl_text}" "_err_capacity = llvmdsdl_plan_capacity_check__"
         capacity_call_pos)
  if(capacity_call_pos EQUAL -1)
    list(APPEND missing_capacity_call_hits "${c_file}")
  endif()
  string(FIND "${impl_text}" "int8_t llvmdsdl_plan_capacity_check__"
         capacity_helper_pos)
  if(capacity_helper_pos EQUAL -1)
    list(APPEND missing_capacity_helper_hits "${c_file}")
  endif()
  string(FIND "${impl_text}" "_err_union_tag = llvmdsdl_plan_validate_union_tag__"
         union_tag_call_pos)
  if(NOT union_tag_call_pos EQUAL -1)
    set(found_union_tag_call 1)
  endif()
  string(FIND "${impl_text}" "int8_t llvmdsdl_plan_validate_union_tag__"
         union_tag_helper_pos)
  if(NOT union_tag_helper_pos EQUAL -1)
    set(found_union_tag_helper 1)
  endif()
  string(FIND "${impl_text}" "= (uint64_t)llvmdsdl_plan_union_tag__"
         union_tag_io_call_pos)
  if(NOT union_tag_io_call_pos EQUAL -1)
    set(found_union_tag_io_call 1)
  endif()
  string(FIND "${impl_text}" "int64_t llvmdsdl_plan_union_tag__"
         union_tag_io_helper_pos)
  if(NOT union_tag_io_helper_pos EQUAL -1)
    set(found_union_tag_io_helper 1)
  endif()
  string(FIND "${impl_text}" "= (uint64_t)llvmdsdl_plan_scalar_unsigned__"
         scalar_call_pos)
  if(NOT scalar_call_pos EQUAL -1)
    set(found_scalar_unsigned_call 1)
  endif()
  string(FIND "${impl_text}" "int64_t llvmdsdl_plan_scalar_unsigned__"
         scalar_helper_pos)
  if(NOT scalar_helper_pos EQUAL -1)
    set(found_scalar_unsigned_helper 1)
  endif()
  string(FIND "${impl_text}" "= (int64_t)llvmdsdl_plan_scalar_signed__"
         scalar_signed_call_pos)
  if(NOT scalar_signed_call_pos EQUAL -1)
    set(found_scalar_signed_call 1)
  endif()
  string(FIND "${impl_text}" "int64_t llvmdsdl_plan_scalar_signed__"
         scalar_signed_helper_pos)
  if(NOT scalar_signed_helper_pos EQUAL -1)
    set(found_scalar_signed_helper 1)
  endif()
  string(FIND "${impl_text}" "= llvmdsdl_plan_scalar_float__"
         scalar_float_call_pos)
  if(NOT scalar_float_call_pos EQUAL -1)
    set(found_scalar_float_call 1)
  endif()
  string(FIND "${impl_text}" "double llvmdsdl_plan_scalar_float__"
         scalar_float_helper_pos)
  if(NOT scalar_float_helper_pos EQUAL -1)
    set(found_scalar_float_helper 1)
  endif()
  string(FIND "${impl_text}" "llvmdsdl_plan_array_length_prefix__"
         array_len_prefix_call_pos)
  if(NOT array_len_prefix_call_pos EQUAL -1)
    set(found_array_len_prefix_call 1)
  endif()
  string(FIND "${impl_text}" "int64_t llvmdsdl_plan_array_length_prefix__"
         array_len_prefix_helper_pos)
  if(NOT array_len_prefix_helper_pos EQUAL -1)
    set(found_array_len_prefix_helper 1)
  endif()
  string(FIND "${impl_text}" "_err_lenchk_"
         array_lenchk_call_pos)
  if(NOT array_lenchk_call_pos EQUAL -1)
    set(found_array_len_validate_call 1)
  endif()
  string(FIND "${impl_text}" "int8_t llvmdsdl_plan_validate_array_length__"
         array_lenchk_helper_pos)
  if(NOT array_lenchk_helper_pos EQUAL -1)
    set(found_array_len_validate_helper 1)
  endif()
  string(FIND "${impl_text}"
         "dsdl_runtime_get_u32(buffer, capacity_bytes, offset_bits, 32U)"
         delimiter_get_pos)
  if(NOT delimiter_get_pos EQUAL -1)
    set(found_delimiter_header_io 1)
  endif()
  string(FIND "${impl_text}"
         "dsdl_runtime_set_uxx(buffer, capacity_bytes, _delim_start_bytes_"
         delimiter_set_pos)
  if(NOT delimiter_set_pos EQUAL -1)
    set(found_delimiter_header_write 1)
  endif()
  string(FIND "${impl_text}"
         "_delim_chk_"
         delimiter_chk_var_pos)
  if(NOT delimiter_chk_var_pos EQUAL -1)
    set(found_delimiter_validate_call 1)
  endif()
  string(FIND "${impl_text}"
         "int8_t llvmdsdl_plan_validate_delimiter_header__"
         delimiter_helper_pos)
  if(NOT delimiter_helper_pos EQUAL -1)
    set(found_delimiter_validate_helper 1)
  endif()
  string(FIND "${impl_text}"
         "_size_bytes_"
         size_bytes_pos)
  if(NOT size_bytes_pos EQUAL -1)
    string(FIND "${impl_text}" "> _remaining_bytes_" delimiter_fallback_pos)
    if(NOT delimiter_fallback_pos EQUAL -1)
      list(APPEND delimiter_fallback_hits "${c_file}")
    endif()
  endif()
  string(FIND "${impl_text}"
         "DSDL_RUNTIME_ERROR_REPRESENTATION_BAD_ARRAY_LENGTH"
         array_len_fallback_pos)
  if(NOT array_len_fallback_pos EQUAL -1)
    list(APPEND array_length_fallback_hits "${c_file}")
  endif()
  string(FIND "${impl_text}" "_sat_" scalar_sat_fallback_pos)
  if(NOT scalar_sat_fallback_pos EQUAL -1)
    list(APPEND scalar_saturation_fallback_hits "${c_file}")
  endif()
  string(FIND "${impl_text}" "= _raw_" scalar_deser_unsigned_fallback_pos)
  if(NOT scalar_deser_unsigned_fallback_pos EQUAL -1)
    list(APPEND scalar_deser_fallback_hits "${c_file}")
  endif()
  string(FIND "${impl_text}" "= _raws_" scalar_deser_signed_fallback_pos)
  if(NOT scalar_deser_signed_fallback_pos EQUAL -1)
    list(APPEND scalar_deser_fallback_hits "${c_file}")
  endif()
  string(FIND "${impl_text}" "= (float)(_rawf_" scalar_deser_float_fallback_pos)
  if(NOT scalar_deser_float_fallback_pos EQUAL -1)
    list(APPEND scalar_deser_fallback_hits "${c_file}")
  endif()
  string(FIND "${impl_text}" "= (double)(_rawf_" scalar_deser_double_fallback_pos)
  if(NOT scalar_deser_double_fallback_pos EQUAL -1)
    list(APPEND scalar_deser_fallback_hits "${c_file}")
  endif()
  string(FIND "${impl_text}"
         ".count = (size_t)dsdl_runtime_get_"
         array_len_prefix_fallback_pos)
  if(NOT array_len_prefix_fallback_pos EQUAL -1)
    list(APPEND array_length_prefix_fallback_hits "${c_file}")
  endif()
  string(FIND "${impl_text}"
         "_tag_value = dsdl_runtime_get_"
         union_tag_io_fallback_pos)
  if(NOT union_tag_io_fallback_pos EQUAL -1)
    list(APPEND union_tag_io_fallback_hits "${c_file}")
  endif()
endforeach()
if(generic_lowering_hits)
  message(FATAL_ERROR
    "generated C implementations unexpectedly used generic lowering: ${generic_lowering_hits}")
endif()
if(missing_capacity_call_hits)
  message(FATAL_ERROR
    "generated C implementations missing capacity-helper call: ${missing_capacity_call_hits}")
endif()
if(missing_capacity_helper_hits)
  message(FATAL_ERROR
    "generated C implementations missing lowered capacity-helper body: ${missing_capacity_helper_hits}")
endif()
if(NOT found_union_tag_call)
  message(FATAL_ERROR
    "generated C implementations did not use lowered union-tag validation helper")
endif()
if(NOT found_union_tag_helper)
  message(FATAL_ERROR
    "generated C implementations did not emit lowered union-tag validation helper body")
endif()
if(NOT found_union_tag_io_call)
  message(FATAL_ERROR
    "generated C implementations did not use lowered union-tag IO helper")
endif()
if(NOT found_union_tag_io_helper)
  message(FATAL_ERROR
    "generated C implementations did not emit lowered union-tag IO helper body")
endif()
if(NOT found_scalar_unsigned_call)
  message(FATAL_ERROR
    "generated C implementations did not use lowered scalar unsigned helper")
endif()
if(NOT found_scalar_unsigned_helper)
  message(FATAL_ERROR
    "generated C implementations did not emit lowered scalar unsigned helper body")
endif()
if(NOT found_scalar_signed_call)
  message(FATAL_ERROR
    "generated C implementations did not use lowered scalar signed helper")
endif()
if(NOT found_scalar_signed_helper)
  message(FATAL_ERROR
    "generated C implementations did not emit lowered scalar signed helper body")
endif()
if(NOT found_scalar_float_call)
  message(FATAL_ERROR
    "generated C implementations did not use lowered scalar float helper")
endif()
if(NOT found_scalar_float_helper)
  message(FATAL_ERROR
    "generated C implementations did not emit lowered scalar float helper body")
endif()
if(NOT found_array_len_prefix_call)
  message(FATAL_ERROR
    "generated C implementations did not use lowered array-length-prefix IO helper")
endif()
if(NOT found_array_len_prefix_helper)
  message(FATAL_ERROR
    "generated C implementations did not emit lowered array-length-prefix IO helper body")
endif()
if(NOT found_array_len_validate_call)
  message(FATAL_ERROR
    "generated C implementations did not use lowered array-length validation helper")
endif()
if(NOT found_array_len_validate_helper)
  message(FATAL_ERROR
    "generated C implementations did not emit lowered array-length validation helper body")
endif()
if(NOT found_delimiter_header_io)
  message(FATAL_ERROR
    "generated C implementations did not emit delimiter-header read path")
endif()
if(NOT found_delimiter_header_write)
  message(FATAL_ERROR
    "generated C implementations did not emit delimiter-header write path")
endif()
if(NOT found_delimiter_validate_call)
  message(FATAL_ERROR
    "generated C implementations did not call lowered delimiter-header validation helper")
endif()
if(NOT found_delimiter_validate_helper)
  message(FATAL_ERROR
    "generated C implementations did not emit lowered delimiter-header validation helper body")
endif()
if(array_length_fallback_hits)
  message(FATAL_ERROR
    "generated C implementations still contain array-length fallback logic: ${array_length_fallback_hits}")
endif()
if(scalar_saturation_fallback_hits)
  message(FATAL_ERROR
    "generated C implementations still contain scalar saturation fallback logic: ${scalar_saturation_fallback_hits}")
endif()
if(delimiter_fallback_hits)
  message(FATAL_ERROR
    "generated C implementations still contain inline delimiter fallback checks: ${delimiter_fallback_hits}")
endif()
if(scalar_deser_fallback_hits)
  message(FATAL_ERROR
    "generated C implementations still contain scalar deserialize fallback assignments: ${scalar_deser_fallback_hits}")
endif()
if(array_length_prefix_fallback_hits)
  message(FATAL_ERROR
    "generated C implementations still contain array-length-prefix fallback assignments: ${array_length_prefix_fallback_hits}")
endif()
if(union_tag_io_fallback_hits)
  message(FATAL_ERROR
    "generated C implementations still contain union-tag IO fallback assignments: ${union_tag_io_fallback_hits}")
endif()

set(scratch_dir "${OUT_DIR}/.compile-check")
file(MAKE_DIRECTORY "${scratch_dir}")

set(index 0)
foreach(h IN LISTS filtered_headers)
  math(EXPR index "${index} + 1")
  file(RELATIVE_PATH rel_header "${OUT_DIR}" "${h}")

  set(tu "${scratch_dir}/tu_${index}.c")
  set(obj "${scratch_dir}/tu_${index}.o")
  file(WRITE "${tu}" "#include \"${rel_header}\"\nint main(void) { return 0; }\n")

  execute_process(
    COMMAND
      "${C_COMPILER}"
        -std=c11
        -Wall
        -Wextra
        -Werror
        -I
        "${OUT_DIR}"
        "${tu}"
        -c
        -o "${obj}"
    RESULT_VARIABLE cc_result
    OUTPUT_VARIABLE cc_stdout
    ERROR_VARIABLE cc_stderr
  )
  if(NOT cc_result EQUAL 0)
    message(STATUS "Failed header: ${rel_header}")
    message(STATUS "compiler stdout:\n${cc_stdout}")
    message(STATUS "compiler stderr:\n${cc_stderr}")
    message(FATAL_ERROR "Generated header compile check failed")
  endif()
endforeach()

message(STATUS
  "uavcan generation check passed: ${dsdl_count} DSDL -> ${header_count} headers")
