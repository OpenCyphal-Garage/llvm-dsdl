#===----------------------------------------------------------------------===#
##
## @file
## CMake script that validates project source formatting with clang-format.
##
#===----------------------------------------------------------------------===#

if(NOT DEFINED CLANG_FORMAT OR CLANG_FORMAT STREQUAL "" OR
   CLANG_FORMAT MATCHES "-NOTFOUND$")
  message(FATAL_ERROR
    "clang-format executable was not provided. "
    "Install clang-format and re-run target check-format.")
endif()

if(NOT DEFINED LLVMDSDL_SOURCE_DIR OR LLVMDSDL_SOURCE_DIR STREQUAL "")
  message(FATAL_ERROR "LLVMDSDL_SOURCE_DIR must be provided.")
endif()

string(REGEX REPLACE "([][+.*()^$?{}|\\\\])" "\\\\\\1"
  _llvmdsdl_source_dir_regex "${LLVMDSDL_SOURCE_DIR}")

set(_llvmdsdl_format_dirs
  include
  lib
  runtime
  test
  tools
)

set(_llvmdsdl_format_files)
foreach(_dir IN LISTS _llvmdsdl_format_dirs)
  if(NOT EXISTS "${LLVMDSDL_SOURCE_DIR}/${_dir}")
    continue()
  endif()
  file(GLOB_RECURSE _dir_files
    LIST_DIRECTORIES FALSE
    "${LLVMDSDL_SOURCE_DIR}/${_dir}/*.c"
    "${LLVMDSDL_SOURCE_DIR}/${_dir}/*.cc"
    "${LLVMDSDL_SOURCE_DIR}/${_dir}/*.cpp"
    "${LLVMDSDL_SOURCE_DIR}/${_dir}/*.cxx"
    "${LLVMDSDL_SOURCE_DIR}/${_dir}/*.h"
    "${LLVMDSDL_SOURCE_DIR}/${_dir}/*.hh"
    "${LLVMDSDL_SOURCE_DIR}/${_dir}/*.hpp"
    "${LLVMDSDL_SOURCE_DIR}/${_dir}/*.inc"
    "${LLVMDSDL_SOURCE_DIR}/${_dir}/*.td"
  )
  list(APPEND _llvmdsdl_format_files ${_dir_files})
endforeach()

list(REMOVE_DUPLICATES _llvmdsdl_format_files)
list(SORT _llvmdsdl_format_files)

set(_llvmdsdl_filtered_format_files)
foreach(_file IN LISTS _llvmdsdl_format_files)
  if(_file MATCHES "^${_llvmdsdl_source_dir_regex}/examples/")
    continue()
  endif()
  # A golden is dsdlc output compared byte for byte; formatting it would change what it holds.
  if(_file MATCHES "^${_llvmdsdl_source_dir_regex}/test/lit/golden/")
    continue()
  endif()
  list(APPEND _llvmdsdl_filtered_format_files "${_file}")
endforeach()
set(_llvmdsdl_format_files "${_llvmdsdl_filtered_format_files}")

if(NOT _llvmdsdl_format_files)
  message(STATUS "No source files found for clang-format check.")
  return()
endif()

execute_process(
  COMMAND "${CLANG_FORMAT}" --help
  RESULT_VARIABLE _help_result
  OUTPUT_VARIABLE _help_stdout
  ERROR_VARIABLE _help_stderr
)
if(NOT _help_result EQUAL 0)
  message(FATAL_ERROR
    "Failed to query clang-format help output:\n${_help_stderr}")
endif()

set(_supports_werror FALSE)
if(_help_stdout MATCHES "--Werror")
  set(_supports_werror TRUE)
endif()

if(_supports_werror)
  set(_check_mode "werror")
else()
  set(_check_mode "xml")
  message(STATUS
    "clang-format does not advertise --Werror; falling back to "
    "--output-replacements-xml checks.")
endif()

# Validate style configuration once to avoid repeating the same parse failure
# for every file in the project.
list(GET _llvmdsdl_format_files 0 _probe_file)
if(_check_mode STREQUAL "werror")
  execute_process(
    COMMAND "${CLANG_FORMAT}" --dry-run --Werror --style=file "${_probe_file}"
    RESULT_VARIABLE _probe_result
    OUTPUT_VARIABLE _probe_stdout
    ERROR_VARIABLE _probe_stderr
  )
  if(NOT _probe_result EQUAL 0 AND
     _probe_stderr MATCHES "Error reading .*\\.clang-format")
    message(FATAL_ERROR
      "clang-format could not parse .clang-format:\n${_probe_stderr}")
  endif()
else()
  execute_process(
    COMMAND "${CLANG_FORMAT}" --style=file --output-replacements-xml
            "${_probe_file}"
    RESULT_VARIABLE _probe_result
    OUTPUT_VARIABLE _probe_stdout
    ERROR_VARIABLE _probe_stderr
  )
  if(NOT _probe_result EQUAL 0)
    message(FATAL_ERROR
      "clang-format could not parse .clang-format:\n${_probe_stderr}")
  endif()
endif()

set(_failed_files)
foreach(_file IN LISTS _llvmdsdl_format_files)
  if(_check_mode STREQUAL "werror")
    execute_process(
      COMMAND "${CLANG_FORMAT}" --dry-run --Werror --style=file "${_file}"
      RESULT_VARIABLE _result
      OUTPUT_VARIABLE _stdout
      ERROR_VARIABLE _stderr
    )
    if(NOT _result EQUAL 0)
      list(APPEND _failed_files "${_file}")
      if(NOT DEFINED _first_error AND NOT _stderr STREQUAL "")
        set(_first_error "${_stderr}")
      endif()
    endif()
  else()
    execute_process(
      COMMAND "${CLANG_FORMAT}" --style=file --output-replacements-xml
              "${_file}"
      RESULT_VARIABLE _result
      OUTPUT_VARIABLE _stdout
      ERROR_VARIABLE _stderr
    )
    if(NOT _result EQUAL 0)
      list(APPEND _failed_files "${_file}")
      if(NOT DEFINED _first_error AND NOT _stderr STREQUAL "")
        set(_first_error "${_stderr}")
      endif()
    elseif(_stdout MATCHES "<replacement ")
      list(APPEND _failed_files "${_file}")
      if(NOT DEFINED _first_error)
        set(_first_error
          "clang-format reported formatting replacements are needed.")
      endif()
    endif()
  endif()
endforeach()

if(_failed_files)
  list(LENGTH _failed_files _failed_count)
  list(JOIN _failed_files "\n  " _failed_list)
  message(FATAL_ERROR
    "clang-format check failed for ${_failed_count} file(s):\n"
    "  ${_failed_list}\n"
    "First clang-format error:\n${_first_error}\n"
    "Run clang-format -i on the listed files.")
endif()

list(LENGTH _llvmdsdl_format_files _checked_count)
message(STATUS
  "clang-format check passed for ${_checked_count} file(s).")
