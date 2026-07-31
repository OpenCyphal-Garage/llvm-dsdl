cmake_minimum_required(VERSION 3.24)

# Build a relocatable directory containing every llvm-dsdl tool and the shared
# libraries they need, with their dynamic-link references rewritten to point
# inside the bundle. The result runs from anywhere, on a machine that has neither
# Homebrew nor apt.llvm.org installed.
#
# Layout is bin/ + lib/ rather than one flat directory, because this tree is what
# the macOS release tarball ships: a flat pile of executables and dylibs is a
# poor thing to hand someone, and separating them lets the executables carry a
# single relative reference (../lib) instead of depending on their own directory
# also being the library directory.

foreach(var TOOLS OUTPUT_DIR)
  if(NOT DEFINED ${var} OR "${${var}}" STREQUAL "")
    message(FATAL_ERROR "Missing required variable: ${var}")
  endif()
endforeach()

if(NOT DEFINED LLVMDSDL_SOURCE_DIR OR "${LLVMDSDL_SOURCE_DIR}" STREQUAL "")
  get_filename_component(LLVMDSDL_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
endif()

foreach(tool IN LISTS TOOLS)
  if(NOT EXISTS "${tool}")
    message(FATAL_ERROR "Tool executable not found: ${tool}")
  endif()
endforeach()

function(_llvmdsdl_collect_macos_deps target_file out_deps)
  execute_process(
    COMMAND otool -L "${target_file}"
    RESULT_VARIABLE otool_result
    OUTPUT_VARIABLE otool_stdout
    ERROR_VARIABLE otool_stderr
  )
  if(NOT otool_result EQUAL 0)
    message(FATAL_ERROR "otool -L failed for ${target_file}: ${otool_stderr}")
  endif()

  string(REPLACE "\n" ";" dep_lines "${otool_stdout}")
  set(deps "")
  foreach(dep_line IN LISTS dep_lines)
    string(STRIP "${dep_line}" dep_line)
    if(dep_line STREQUAL "" OR dep_line MATCHES ":$")
      continue()
    endif()
    string(REGEX REPLACE "^([^ ]+).*" "\\1" dep_path "${dep_line}")
    if(dep_path MATCHES "^/System/Library/" OR dep_path MATCHES "^/usr/lib/")
      continue()
    endif()
    list(APPEND deps "${dep_path}")
  endforeach()
  list(REMOVE_DUPLICATES deps)
  set(${out_deps} "${deps}" PARENT_SCOPE)
endfunction()

function(_llvmdsdl_codesign_macos target_file)
  if(NOT EXISTS "${target_file}" OR IS_SYMLINK "${target_file}")
    return()
  endif()

  if(NOT DEFINED CODESIGN_EXECUTABLE)
    find_program(CODESIGN_EXECUTABLE codesign)
    if(NOT CODESIGN_EXECUTABLE)
      message(FATAL_ERROR
        "macOS self-contained tool bundling requires 'codesign'.")
    endif()
  endif()

  # Ad-hoc, because a Developer ID signature would need a paid certificate and
  # notarisation. Rewriting a Mach-O invalidates any existing signature, and an
  # invalid signature is worse than an ad-hoc one: the loader refuses it
  # outright. See docs/development/release-packaging.md §5 for what this means for
  # Gatekeeper (short version: fine via `tar`, blocked via Finder).
  execute_process(
    COMMAND "${CODESIGN_EXECUTABLE}" --force --sign - --timestamp=none "${target_file}"
    RESULT_VARIABLE codesign_result
    OUTPUT_QUIET
    ERROR_VARIABLE codesign_stderr
  )
  if(NOT codesign_result EQUAL 0)
    message(FATAL_ERROR
      "codesign failed for ${target_file}: ${codesign_stderr}")
  endif()
endfunction()

function(_llvmdsdl_is_linux_system_dep dep_path out_result)
  if(dep_path MATCHES "^/lib/" OR dep_path MATCHES "^/lib64/" OR
     dep_path MATCHES "^/usr/lib/" OR dep_path MATCHES "^/usr/lib64/" OR
     dep_path MATCHES "^/usr/libexec/")
    set(${out_result} TRUE PARENT_SCOPE)
  else()
    set(${out_result} FALSE PARENT_SCOPE)
  endif()
endfunction()

set(bundle_dir "${OUTPUT_DIR}")
set(bundle_bin "${bundle_dir}/bin")
set(bundle_lib "${bundle_dir}/lib")
file(REMOVE_RECURSE "${bundle_dir}")
file(MAKE_DIRECTORY "${bundle_bin}")
file(MAKE_DIRECTORY "${bundle_lib}")

set(notice_files "")
foreach(notice_rel
        "LICENSE.md"
        "THIRD_PARTY_NOTICES.md"
        "LICENSES/LLVM-Apache-2.0-with-LLVM-exception.txt")
  set(notice_src "${LLVMDSDL_SOURCE_DIR}/${notice_rel}")
  if(EXISTS "${notice_src}")
    file(COPY "${notice_src}" DESTINATION "${bundle_dir}")
    get_filename_component(notice_name "${notice_src}" NAME)
    list(APPEND notice_files "${bundle_dir}/${notice_name}")
  else()
    message(WARNING "Notice/license file not found: ${notice_src}")
  endif()
endforeach()

set(bundled_executables "")
foreach(tool IN LISTS TOOLS)
  file(COPY "${tool}" DESTINATION "${bundle_bin}")
  get_filename_component(tool_name "${tool}" NAME)
  set(dst "${bundle_bin}/${tool_name}")
  file(CHMOD "${dst}"
       PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
                   GROUP_READ GROUP_EXECUTE
                   WORLD_READ WORLD_EXECUTE)
  list(APPEND bundled_executables "${dst}")
endforeach()

file(GET_RUNTIME_DEPENDENCIES
  EXECUTABLES ${TOOLS}
  RESOLVED_DEPENDENCIES_VAR resolved_deps
  UNRESOLVED_DEPENDENCIES_VAR unresolved_deps
)

if(unresolved_deps)
  message(WARNING "Unresolved runtime dependencies: ${unresolved_deps}")
endif()

set(copied_deps "")

if(APPLE)
  foreach(dep IN LISTS resolved_deps)
    if(EXISTS "${dep}")
      file(COPY "${dep}" DESTINATION "${bundle_lib}" FOLLOW_SYMLINK_CHAIN)
    endif()
  endforeach()

  file(GLOB copied_deps "${bundle_lib}/*.dylib")

  # Libraries first: an executable's references are rewritten against whatever is
  # present in lib/, so the set has to be complete before the executables are
  # walked. A library's siblings live beside it, hence @loader_path; an
  # executable reaches them one directory up, hence @executable_path/../lib.
  foreach(item IN LISTS copied_deps)
    get_filename_component(item_name "${item}" NAME)
    execute_process(
      COMMAND install_name_tool -id "@loader_path/${item_name}" "${item}"
      RESULT_VARIABLE id_result
      OUTPUT_QUIET
      ERROR_VARIABLE id_stderr
    )
    if(NOT id_result EQUAL 0)
      message(FATAL_ERROR "install_name_tool -id failed for ${item}: ${id_stderr}")
    endif()

    _llvmdsdl_collect_macos_deps("${item}" item_deps)
    foreach(dep IN LISTS item_deps)
      get_filename_component(dep_name "${dep}" NAME)
      if(EXISTS "${bundle_lib}/${dep_name}")
        execute_process(
          COMMAND install_name_tool -change "${dep}" "@loader_path/${dep_name}" "${item}"
          RESULT_VARIABLE ch_result
          OUTPUT_QUIET
          ERROR_VARIABLE ch_stderr
        )
        if(NOT ch_result EQUAL 0)
          message(FATAL_ERROR
            "install_name_tool -change failed for ${item}: ${dep} -> @loader_path/${dep_name}\n${ch_stderr}")
        endif()
      endif()
    endforeach()
  endforeach()

  foreach(item IN LISTS bundled_executables)
    _llvmdsdl_collect_macos_deps("${item}" item_deps)
    foreach(dep IN LISTS item_deps)
      get_filename_component(dep_name "${dep}" NAME)
      if(EXISTS "${bundle_lib}/${dep_name}")
        execute_process(
          COMMAND install_name_tool -change "${dep}" "@executable_path/../lib/${dep_name}" "${item}"
          RESULT_VARIABLE ch_result
          OUTPUT_QUIET
          ERROR_VARIABLE ch_stderr
        )
        if(NOT ch_result EQUAL 0)
          message(FATAL_ERROR
            "install_name_tool -change failed for ${item}: ${dep} -> @executable_path/../lib/${dep_name}\n${ch_stderr}")
        endif()
      endif()
    endforeach()
  endforeach()

  # Signing last: every install_name_tool write invalidates the signature.
  foreach(item IN LISTS copied_deps)
    _llvmdsdl_codesign_macos("${item}")
  endforeach()
  foreach(item IN LISTS bundled_executables)
    _llvmdsdl_codesign_macos("${item}")
  endforeach()
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
  find_program(PATCHELF_EXECUTABLE patchelf)
  if(NOT PATCHELF_EXECUTABLE)
    message(FATAL_ERROR
      "Linux self-contained tool bundling requires 'patchelf'. "
      "Install patchelf and re-run the Release bundle target.")
  endif()

  foreach(dep IN LISTS resolved_deps)
    if(NOT EXISTS "${dep}")
      continue()
    endif()
    _llvmdsdl_is_linux_system_dep("${dep}" is_system_dep)
    if(is_system_dep)
      continue()
    endif()
    file(COPY "${dep}" DESTINATION "${bundle_lib}" FOLLOW_SYMLINK_CHAIN)
  endforeach()

  file(GLOB copied_deps "${bundle_lib}/*.so" "${bundle_lib}/*.so.*")

  # Executables look one directory up; libraries look beside themselves.
  foreach(item IN LISTS bundled_executables copied_deps)
    if(item IN_LIST bundled_executables)
      set(rpath "$ORIGIN/../lib")
    else()
      set(rpath "$ORIGIN")
    endif()
    execute_process(
      COMMAND "${PATCHELF_EXECUTABLE}" --set-rpath "${rpath}" "${item}"
      RESULT_VARIABLE rpath_result
      OUTPUT_QUIET
      ERROR_VARIABLE rpath_stderr
    )
    if(NOT rpath_result EQUAL 0)
      message(FATAL_ERROR "patchelf --set-rpath failed for ${item}: ${rpath_stderr}")
    endif()

    execute_process(
      COMMAND "${PATCHELF_EXECUTABLE}" --print-needed "${item}"
      RESULT_VARIABLE needed_result
      OUTPUT_VARIABLE needed_stdout
      ERROR_VARIABLE needed_stderr
    )
    if(NOT needed_result EQUAL 0)
      message(FATAL_ERROR "patchelf --print-needed failed for ${item}: ${needed_stderr}")
    endif()

    string(REPLACE "\n" ";" needed_lines "${needed_stdout}")
    foreach(needed IN LISTS needed_lines)
      string(STRIP "${needed}" needed)
      if(needed STREQUAL "")
        continue()
      endif()
      if(needed MATCHES "^/")
        get_filename_component(needed_name "${needed}" NAME)
        if(EXISTS "${bundle_lib}/${needed_name}")
          execute_process(
            COMMAND "${PATCHELF_EXECUTABLE}" --replace-needed "${needed}" "${needed_name}" "${item}"
            RESULT_VARIABLE replace_result
            OUTPUT_QUIET
            ERROR_VARIABLE replace_stderr
          )
          if(NOT replace_result EQUAL 0)
            message(FATAL_ERROR
              "patchelf --replace-needed failed for ${item}: ${needed} -> ${needed_name}\n${replace_stderr}")
          endif()
        endif()
      endif()
    endforeach()
  endforeach()
else()
  message(FATAL_ERROR
    "BundleSelfContainedTools.cmake currently supports macOS and Linux only.")
endif()

set(manifest "${bundle_dir}/MANIFEST.txt")
file(WRITE "${manifest}" "Self-contained llvm-dsdl tools bundle\n\n")
file(APPEND "${manifest}" "Bundled executables (bin/):\n")
foreach(item IN LISTS bundled_executables)
  get_filename_component(item_name "${item}" NAME)
  file(APPEND "${manifest}" "  ${item_name}\n")
endforeach()
file(APPEND "${manifest}" "\n")
if(notice_files)
  file(APPEND "${manifest}" "Bundled notice/license files:\n")
  foreach(notice_file IN LISTS notice_files)
    get_filename_component(notice_name "${notice_file}" NAME)
    file(APPEND "${manifest}" "  ${notice_name}\n")
  endforeach()
  file(APPEND "${manifest}" "\n")
endif()
file(APPEND "${manifest}" "Bundled shared libraries (lib/):\n")
foreach(dep IN LISTS copied_deps)
  get_filename_component(dep_name "${dep}" NAME)
  file(APPEND "${manifest}" "  ${dep_name}\n")
endforeach()

file(APPEND "${manifest}" "\nRuntime links after rewrite:\n")
foreach(item IN LISTS bundled_executables)
  if(APPLE)
    execute_process(COMMAND otool -L "${item}"
                    OUTPUT_VARIABLE link_out RESULT_VARIABLE link_result)
  else()
    execute_process(COMMAND "${PATCHELF_EXECUTABLE}" --print-rpath "${item}"
                    OUTPUT_VARIABLE link_out RESULT_VARIABLE link_result)
  endif()
  if(link_result EQUAL 0)
    get_filename_component(item_name "${item}" NAME)
    file(APPEND "${manifest}" "\n${item_name}:\n${link_out}\n")
  endif()
endforeach()

message(STATUS "Wrote self-contained tool bundle: ${bundle_dir}")
