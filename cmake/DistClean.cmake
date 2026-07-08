cmake_minimum_required(VERSION 3.24)

if(NOT DEFINED SOURCE_DIR OR SOURCE_DIR STREQUAL "" OR NOT IS_DIRECTORY "${SOURCE_DIR}")
  message(FATAL_ERROR "distclean requires SOURCE_DIR to point at the repository root")
endif()

if(NOT DEFINED BINARY_DIR OR BINARY_DIR STREQUAL "" OR NOT IS_DIRECTORY "${BINARY_DIR}")
  message(FATAL_ERROR "distclean requires BINARY_DIR to point at an existing CMake build directory")
endif()

get_filename_component(_source_dir_real "${SOURCE_DIR}" REALPATH)
get_filename_component(_binary_dir_real "${BINARY_DIR}" REALPATH)

if(_binary_dir_real STREQUAL _source_dir_real)
  message(FATAL_ERROR
    "Refusing to distclean in-source build directory: '${_binary_dir_real}'. "
    "Use an out-of-source build directory.")
endif()

if(_binary_dir_real STREQUAL "/" OR _binary_dir_real STREQUAL "")
  message(FATAL_ERROR "Refusing to distclean unsafe BINARY_DIR '${_binary_dir_real}'")
endif()

file(GLOB _distclean_binary_children
  LIST_DIRECTORIES true
  "${_binary_dir_real}/*"
)
file(GLOB _distclean_binary_children_hidden
  LIST_DIRECTORIES true
  "${_binary_dir_real}/.[!.]*"
  "${_binary_dir_real}/..?*"
)
list(APPEND _distclean_binary_children ${_distclean_binary_children_hidden})

foreach(path IN LISTS _distclean_binary_children)
  if(EXISTS "${path}" OR IS_SYMLINK "${path}")
    message(STATUS "distclean: removing ${path}")
    file(REMOVE_RECURSE "${path}")
  endif()
endforeach()

if(DEFINED REMOVE_VENV AND REMOVE_VENV)
  set(_venv_path "${SOURCE_DIR}/.venv")
  if(EXISTS "${_venv_path}" OR IS_SYMLINK "${_venv_path}")
    message(STATUS "distclean: removing ${_venv_path}")
    file(REMOVE_RECURSE "${_venv_path}")
  endif()
endif()
