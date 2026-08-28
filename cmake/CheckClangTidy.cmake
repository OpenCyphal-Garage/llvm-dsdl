#===----------------------------------------------------------------------===#
##
## @file
## CMake script that validates project sources with clang-tidy.
##
#===----------------------------------------------------------------------===#

if(NOT DEFINED CLANG_TIDY OR CLANG_TIDY STREQUAL "" OR
   CLANG_TIDY MATCHES "-NOTFOUND$")
  message(FATAL_ERROR
    "clang-tidy executable was not provided. "
    "Install clang-tidy and re-run target check-clang-tidy.")
endif()

if(NOT DEFINED LLVMDSDL_SOURCE_DIR OR LLVMDSDL_SOURCE_DIR STREQUAL "")
  message(FATAL_ERROR "LLVMDSDL_SOURCE_DIR must be provided.")
endif()

if(NOT DEFINED LLVMDSDL_BINARY_DIR OR LLVMDSDL_BINARY_DIR STREQUAL "")
  message(FATAL_ERROR "LLVMDSDL_BINARY_DIR must be provided.")
endif()

set(_llvmdsdl_compile_commands
  "${LLVMDSDL_BINARY_DIR}/compile_commands.json")
if(NOT EXISTS "${_llvmdsdl_compile_commands}")
  message(FATAL_ERROR
    "compile_commands.json not found at:\n"
    "  ${_llvmdsdl_compile_commands}\n"
    "Configure the build directory first with "
    "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON.")
endif()

file(READ "${_llvmdsdl_compile_commands}" _llvmdsdl_compile_db)
string(JSON _llvmdsdl_entry_count LENGTH "${_llvmdsdl_compile_db}")

if(_llvmdsdl_entry_count EQUAL 0)
  message(STATUS "No compile commands found for clang-tidy check.")
  return()
endif()

math(EXPR _llvmdsdl_last_index "${_llvmdsdl_entry_count} - 1")

string(REGEX REPLACE "([][+.*()^$?{}|\\\\])" "\\\\\\1"
  _llvmdsdl_source_dir_regex "${LLVMDSDL_SOURCE_DIR}")

set(_llvmdsdl_tidy_files)
set(_llvmdsdl_tidy_excluded_files
  "${LLVMDSDL_SOURCE_DIR}/lib/IR/DSDLDialect.cpp"
)
foreach(_index RANGE 0 ${_llvmdsdl_last_index})
  string(JSON _file GET "${_llvmdsdl_compile_db}" ${_index} file)
  if(_file STREQUAL "")
    continue()
  endif()

  if(IS_ABSOLUTE "${_file}")
    set(_abs_file "${_file}")
  else()
    string(JSON _entry_dir GET "${_llvmdsdl_compile_db}" ${_index} directory)
    if(_entry_dir STREQUAL "")
      continue()
    endif()
    cmake_path(ABSOLUTE_PATH _file
      BASE_DIRECTORY "${_entry_dir}"
      OUTPUT_VARIABLE _abs_file)
  endif()

  cmake_path(NORMAL_PATH _abs_file OUTPUT_VARIABLE _abs_file)

  if(NOT _abs_file MATCHES "^${_llvmdsdl_source_dir_regex}/")
    continue()
  endif()

  if(_abs_file MATCHES "^${_llvmdsdl_source_dir_regex}/submodules/")
    continue()
  endif()

  if(_abs_file MATCHES "^${_llvmdsdl_source_dir_regex}/examples/")
    continue()
  endif()

  # runtime/ is the C runtime and the Python accelerator: snake_case names, macros, unions for
  # punning, C casts. The C++ ruleset in .clang-tidy describes none of it, and the header filter
  # below leaves those headers out for the same reason. Linting them needs a C ruleset of its own.
  if(_abs_file MATCHES "^${_llvmdsdl_source_dir_regex}/runtime/")
    continue()
  endif()

  list(FIND _llvmdsdl_tidy_excluded_files "${_abs_file}" _llvmdsdl_excluded_index)
  if(NOT _llvmdsdl_excluded_index EQUAL -1)
    continue()
  endif()

  if(NOT _abs_file MATCHES "\\.(c|cc|cpp|cxx)$")
    continue()
  endif()

  list(APPEND _llvmdsdl_tidy_files "${_abs_file}")
endforeach()

list(REMOVE_DUPLICATES _llvmdsdl_tidy_files)
list(SORT _llvmdsdl_tidy_files)

if(NOT _llvmdsdl_tidy_files)
  message(STATUS
    "No project C/C++ source files found in compile commands for clang-tidy.")
  return()
endif()

# Everything clang-tidy needs beyond the compile database. `-p`, --warnings-as-errors and the
# header filters are supplied by the driver.
set(_llvmdsdl_tidy_extra_args_before)

if(APPLE)
  execute_process(
    COMMAND xcrun --show-sdk-path
    RESULT_VARIABLE _sdk_result
    OUTPUT_VARIABLE _sdk_path
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
  )
  if(_sdk_result EQUAL 0 AND NOT _sdk_path STREQUAL "")
    list(APPEND _llvmdsdl_tidy_extra_args_before -isysroot "${_sdk_path}")
  endif()
endif()

# CMake script mode runs execute_process calls one at a time, so the walk is handed to a driver
# that can use every core. The selection above stays here; the driver only executes it.
set(_llvmdsdl_tidy_file_list "${LLVMDSDL_BINARY_DIR}/clang-tidy-files.txt")
string(JOIN "\n" _llvmdsdl_tidy_file_lines ${_llvmdsdl_tidy_files})
file(WRITE "${_llvmdsdl_tidy_file_list}" "${_llvmdsdl_tidy_file_lines}\n")

find_program(LLVMDSDL_PYTHON3_FOR_TIDY NAMES python3 python)
if(NOT LLVMDSDL_PYTHON3_FOR_TIDY)
  message(FATAL_ERROR "python3 was not found; it drives the parallel clang-tidy walk.")
endif()

# runtime/ is deliberately absent from the header filter. Those headers are the C runtime API --
# snake_case names, macros, unions for punning, C casts -- and the C++ ruleset in .clang-tidy
# describes none of it. Linting them needs a C-appropriate ruleset of its own.
set(_llvmdsdl_tidy_driver_args
  "${LLVMDSDL_SOURCE_DIR}/tools/run_clang_tidy.py"
  --clang-tidy "${CLANG_TIDY}"
  --build-dir "${LLVMDSDL_BINARY_DIR}"
  --file-list "${_llvmdsdl_tidy_file_list}"
  "--header-filter=^${_llvmdsdl_source_dir_regex}/(include|lib|test|tools)/"
  "--exclude-header-filter=^${_llvmdsdl_source_dir_regex}/(submodules|examples|build)/"
)
foreach(_extra IN LISTS _llvmdsdl_tidy_extra_args_before)
  list(APPEND _llvmdsdl_tidy_driver_args "--extra-arg-before=${_extra}")
endforeach()

execute_process(
  COMMAND "${LLVMDSDL_PYTHON3_FOR_TIDY}" ${_llvmdsdl_tidy_driver_args}
  RESULT_VARIABLE _llvmdsdl_tidy_result
)
if(NOT _llvmdsdl_tidy_result EQUAL 0)
  message(FATAL_ERROR "clang-tidy check failed.")
endif()
