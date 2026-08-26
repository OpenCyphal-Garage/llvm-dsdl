cmake_minimum_required(VERSION 3.24)

foreach(var DSDLC UAVCAN_ROOT OUT_DIR CXX_COMPILER)
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
    "${DSDLC}" --target-language cpp --all-type-versions
      "${UAVCAN_ROOT}"
      --cpp-profile both
      --outdir "${OUT_DIR}"
  RESULT_VARIABLE gen_result
  OUTPUT_VARIABLE gen_stdout
  ERROR_VARIABLE gen_stderr
)
if(NOT gen_result EQUAL 0)
  message(STATUS "dsdlc stdout:\n${gen_stdout}")
  message(STATUS "dsdlc stderr:\n${gen_stderr}")
  message(FATAL_ERROR "uavcan C++ generation failed")
endif()

execute_process(
  COMMAND
    "${DSDLC}" --target-language cpp --all-type-versions
      "${UAVCAN_ROOT}"
      --cpp-profile autosar
      --outdir "${OUT_DIR}/autosar"
  RESULT_VARIABLE autosar_gen_result
  OUTPUT_VARIABLE autosar_gen_stdout
  ERROR_VARIABLE autosar_gen_stderr
)
if(NOT autosar_gen_result EQUAL 0)
  message(STATUS "dsdlc autosar stdout:\n${autosar_gen_stdout}")
  message(STATUS "dsdlc autosar stderr:\n${autosar_gen_stderr}")
  message(FATAL_ERROR "uavcan C++ AUTOSAR generation failed")
endif()

foreach(profile std pmr autosar)
  if(profile STREQUAL "autosar")
    set(profile_root "${OUT_DIR}/${profile}")
    set(cxx_std_flag -std=c++14)
    set(cxx_warning_flags
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wsign-conversion
        -Werror)
  else()
    set(profile_root "${OUT_DIR}/${profile}")
    set(cxx_std_flag -std=c++23)
    set(cxx_warning_flags
        -Wall
        -Wextra
        -Werror)
  endif()

  foreach(required
      "${profile_root}/dsdl_runtime.h"
      "${profile_root}/dsdl_runtime.hpp")
    if(NOT EXISTS "${required}")
      message(FATAL_ERROR "Missing required generated file: ${required}")
    endif()
  endforeach()

  file(GLOB_RECURSE dsdl_files "${UAVCAN_ROOT}/*.dsdl")
  list(LENGTH dsdl_files dsdl_count)

  file(GLOB_RECURSE generated_headers "${profile_root}/*.hpp")
  set(filtered_headers "")
  foreach(h IN LISTS generated_headers)
    get_filename_component(name "${h}" NAME)
    if(NOT name STREQUAL "dsdl_runtime.hpp")
      list(APPEND filtered_headers "${h}")
    endif()
  endforeach()
  list(LENGTH filtered_headers header_count)

  set(found_mlir_union_helper FALSE)
  set(found_mlir_union_validate_helper FALSE)
  set(found_mlir_union_validate_call FALSE)
  set(found_mlir_capacity_check_helper FALSE)
  set(found_mlir_capacity_check_call FALSE)
  set(found_mlir_array_prefix_helper FALSE)
  set(found_mlir_array_prefix_call FALSE)
  set(found_mlir_array_validate_helper FALSE)
  set(found_mlir_array_validate_call FALSE)
  set(found_mlir_scalar_unsigned_helper FALSE)
  set(found_mlir_scalar_unsigned_call FALSE)
  set(found_mlir_scalar_signed_helper FALSE)
  set(found_mlir_scalar_signed_call FALSE)
  set(found_mlir_scalar_float_helper FALSE)
  set(found_mlir_scalar_float_call FALSE)
  set(found_mlir_delimiter_validate_helper FALSE)
  set(found_mlir_delimiter_validate_call FALSE)
  set(found_backend_fallback_signature FALSE)
  set(found_backend_array_length_fallback_signature FALSE)
  set(found_backend_delimiter_fallback_signature FALSE)
  set(found_backend_scalar_deser_fallback_signature FALSE)
  foreach(h IN LISTS filtered_headers)
    file(READ "${h}" header_text)
    if(header_text MATCHES "mlir_llvmdsdl_plan_union_tag_")
      set(found_mlir_union_helper TRUE)
    endif()
    if(header_text MATCHES "mlir_llvmdsdl_plan_validate_union_tag_")
      set(found_mlir_union_validate_helper TRUE)
    endif()
    if(header_text MATCHES "_err_union_tag[0-9_]* = mlir_llvmdsdl_plan_validate_union_tag_")
      set(found_mlir_union_validate_call TRUE)
    endif()
    if(header_text MATCHES "mlir_llvmdsdl_plan_capacity_check_")
      set(found_mlir_capacity_check_helper TRUE)
    endif()
    if(header_text MATCHES "_err_capacity[0-9_]* = mlir_llvmdsdl_plan_capacity_check_")
      set(found_mlir_capacity_check_call TRUE)
    endif()
    if(header_text MATCHES "mlir_llvmdsdl_plan_array_length_prefix_")
      set(found_mlir_array_prefix_helper TRUE)
    endif()
    if(header_text MATCHES "dsdl_runtime_set_uxx\\([^\\n]*mlir_llvmdsdl_plan_array_length_prefix_")
      set(found_mlir_array_prefix_call TRUE)
    endif()
    if(header_text MATCHES "mlir_llvmdsdl_plan_validate_array_length_")
      set(found_mlir_array_validate_helper TRUE)
    endif()
    if(header_text MATCHES "_len_rc[0-9_]* = mlir_llvmdsdl_plan_validate_array_length_")
      set(found_mlir_array_validate_call TRUE)
    endif()
    if(header_text MATCHES "mlir_llvmdsdl_plan_scalar_unsigned_")
      set(found_mlir_scalar_unsigned_helper TRUE)
    endif()
    if(header_text MATCHES "= static_cast<[^>]+>\\(mlir_llvmdsdl_plan_scalar_unsigned_")
      set(found_mlir_scalar_unsigned_call TRUE)
    endif()
    if(header_text MATCHES "mlir_llvmdsdl_plan_scalar_signed_")
      set(found_mlir_scalar_signed_helper TRUE)
    endif()
    if(header_text MATCHES "= static_cast<[^>]+>\\(mlir_llvmdsdl_plan_scalar_signed_")
      set(found_mlir_scalar_signed_call TRUE)
    endif()
    if(header_text MATCHES "mlir_llvmdsdl_plan_scalar_float_")
      set(found_mlir_scalar_float_helper TRUE)
    endif()
    if(header_text MATCHES "= static_cast<(float|double)>\\(mlir_llvmdsdl_plan_scalar_float_")
      set(found_mlir_scalar_float_call TRUE)
    endif()
    if(header_text MATCHES "mlir_llvmdsdl_plan_validate_delimiter_header_")
      set(found_mlir_delimiter_validate_helper TRUE)
    endif()
    if(header_text MATCHES "_rc[0-9_]* = mlir_llvmdsdl_plan_validate_delimiter_header_")
      set(found_mlir_delimiter_validate_call TRUE)
    endif()
    if(header_text MATCHES "std::uint64_t _sat[0-9_]* = " OR
       header_text MATCHES "std::int64_t _sat[0-9_]* = ")
      set(found_backend_fallback_signature TRUE)
    endif()
    if(header_text MATCHES "\\.size\\(\\) > [0-9]+U")
      set(found_backend_array_length_fallback_signature TRUE)
    endif()
    if(header_text MATCHES "_size_bytes[0-9_]* > _remaining")
      set(found_backend_delimiter_fallback_signature TRUE)
    endif()
    if(header_text MATCHES "out_obj->[A-Za-z0-9_]+ = static_cast<[^>]+>\\(dsdl_runtime_get_(u|i|f)")
      set(found_backend_scalar_deser_fallback_signature TRUE)
    endif()
  endforeach()

  if(NOT found_mlir_union_helper)
    message(FATAL_ERROR
      "Missing MLIR union-tag helper bindings in generated C++ (${profile}) headers")
  endif()
  if(NOT found_mlir_union_validate_helper)
    message(FATAL_ERROR
      "Missing MLIR union-tag-validate helper bindings in generated C++ (${profile}) headers")
  endif()
  if(NOT found_mlir_union_validate_call)
    message(FATAL_ERROR
      "Missing MLIR union-tag-validate helper call sites in generated C++ (${profile}) headers")
  endif()
  if(NOT found_mlir_capacity_check_helper)
    message(FATAL_ERROR
      "Missing MLIR capacity-check helper bindings in generated C++ (${profile}) headers")
  endif()
  if(NOT found_mlir_capacity_check_call)
    message(FATAL_ERROR
      "Missing MLIR capacity-check helper call sites in generated C++ (${profile}) headers")
  endif()
  if(NOT found_mlir_array_prefix_helper)
    message(FATAL_ERROR
      "Missing MLIR array-prefix helper bindings in generated C++ (${profile}) headers")
  endif()
  if(NOT found_mlir_array_prefix_call)
    message(FATAL_ERROR
      "Missing MLIR array-prefix helper call sites in generated C++ (${profile}) headers")
  endif()
  if(NOT found_mlir_array_validate_helper)
    message(FATAL_ERROR
      "Missing MLIR array-validate helper bindings in generated C++ (${profile}) headers")
  endif()
  if(NOT found_mlir_array_validate_call)
    message(FATAL_ERROR
      "Missing MLIR array-validate helper call sites in generated C++ (${profile}) headers")
  endif()
  if(NOT found_mlir_scalar_unsigned_helper)
    message(FATAL_ERROR
      "Missing MLIR scalar-unsigned helper bindings in generated C++ (${profile}) headers")
  endif()
  if(NOT found_mlir_scalar_unsigned_call)
    message(FATAL_ERROR
      "Missing MLIR scalar-unsigned helper call sites in generated C++ (${profile}) headers")
  endif()
  if(NOT found_mlir_scalar_signed_helper)
    message(FATAL_ERROR
      "Missing MLIR scalar-signed helper bindings in generated C++ (${profile}) headers")
  endif()
  if(NOT found_mlir_scalar_signed_call)
    message(FATAL_ERROR
      "Missing MLIR scalar-signed helper call sites in generated C++ (${profile}) headers")
  endif()
  if(NOT found_mlir_scalar_float_helper)
    message(FATAL_ERROR
      "Missing MLIR scalar-float helper bindings in generated C++ (${profile}) headers")
  endif()
  if(NOT found_mlir_scalar_float_call)
    message(FATAL_ERROR
      "Missing MLIR scalar-float helper call sites in generated C++ (${profile}) headers")
  endif()
  if(NOT found_mlir_delimiter_validate_helper)
    message(FATAL_ERROR
      "Missing MLIR delimiter-validate helper bindings in generated C++ (${profile}) headers")
  endif()
  if(NOT found_mlir_delimiter_validate_call)
    message(FATAL_ERROR
      "Missing MLIR delimiter-validate helper call sites in generated C++ (${profile}) headers")
  endif()
  if(found_backend_fallback_signature)
    message(FATAL_ERROR
      "Found backend fallback saturation signatures in generated C++ (${profile}) headers")
  endif()
  if(found_backend_array_length_fallback_signature)
    message(FATAL_ERROR
      "Found backend inline array-length fallback signatures in generated C++ (${profile}) headers")
  endif()
  if(found_backend_delimiter_fallback_signature)
    message(FATAL_ERROR
      "Found backend inline delimiter fallback signatures in generated C++ (${profile}) headers")
  endif()
  if(found_backend_scalar_deser_fallback_signature)
    message(FATAL_ERROR
      "Found backend scalar-deserialize fallback signatures in generated C++ (${profile}) headers")
  endif()

  if(NOT dsdl_count EQUAL header_count)
    message(FATAL_ERROR
      "C++ header count mismatch (${profile}): dsdl=${dsdl_count}, generated=${header_count}")
  endif()

  set(scratch_dir "${profile_root}/.compile-check")
  file(MAKE_DIRECTORY "${scratch_dir}")

  set(index 0)
  foreach(h IN LISTS filtered_headers)
    math(EXPR index "${index} + 1")
    file(RELATIVE_PATH rel_header "${profile_root}" "${h}")

    set(tu "${scratch_dir}/tu_${index}.cpp")
    set(obj "${scratch_dir}/tu_${index}.o")
    file(WRITE "${tu}" "#include \"${rel_header}\"\nint main() { return 0; }\n")

    execute_process(
      COMMAND
        "${CXX_COMPILER}"
          ${cxx_std_flag}
          ${cxx_warning_flags}
          -I
          "${profile_root}"
          "${tu}"
          -c
          -o "${obj}"
      RESULT_VARIABLE cxx_result
      OUTPUT_VARIABLE cxx_stdout
      ERROR_VARIABLE cxx_stderr
    )
    if(NOT cxx_result EQUAL 0)
      message(STATUS "Failed header (${profile}): ${rel_header}")
      message(STATUS "compiler stdout:\n${cxx_stdout}")
      message(STATUS "compiler stderr:\n${cxx_stderr}")
      message(FATAL_ERROR "Generated C++ header compile check failed")
    endif()
  endforeach()
endforeach()

message(STATUS "uavcan C++ generation check passed for std, pmr, and autosar profiles")
