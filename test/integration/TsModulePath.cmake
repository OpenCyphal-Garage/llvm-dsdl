#===----------------------------------------------------------------------===#
#
# Part of the OpenCyphal project, under the MIT licence
# SPDX-License-Identifier: MIT
#
#===----------------------------------------------------------------------===#
#
# How a TypeScript harness reaches a generated module through the package's index.
#
# Each directory's `index.ts` re-exports its namespaces and modules under their own names, so the
# module written to `fixtures/vendor/union_tag_1_0.ts` is `fixtures.vendor.union_tag_1_0` to a
# consumer that imports `fixtures` from `./index`.
#
# Usage, from a harness script:
#
#   include("${CMAKE_CURRENT_LIST_DIR}/TsModulePath.cmake")
#   llvmdsdl_ts_module("${ts_out}" "union_tag_1_0" ts_type_module)
#   file(WRITE "${ts_out}/smoke.ts"
#     "import { ${TS_INDEX_IMPORTS} } from \"./index\";\n"
#     "const bytes = ${ts_type_module}.UnionTag_1_0.serialize(value);\n")

# @brief Sets @p out_var to the path the generated module @p stem is reached by from the index of
#        @p out_dir, and adds its root namespace to TS_INDEX_IMPORTS, the names a harness imports
#        from `./index`.
#
# A stem is a module's file name without its extension, and names one module of the output.
function(llvmdsdl_ts_module out_dir stem out_var)
  file(GLOB_RECURSE generated RELATIVE "${out_dir}" "${out_dir}/*.ts")
  set(matches "")
  foreach(path IN LISTS generated)
    if(path MATCHES "(^|/)${stem}\\.ts$")
      list(APPEND matches "${path}")
    endif()
  endforeach()
  list(LENGTH matches count)
  if(NOT count EQUAL 1)
    message(FATAL_ERROR "expected one generated TypeScript module ${stem} under ${out_dir}, found ${count}: ${matches}")
  endif()
  string(REGEX REPLACE "\\.ts$" "" module "${matches}")
  string(REPLACE "/" "." dotted "${module}")
  string(REGEX MATCH "^[^/]+" root "${module}")

  set(imports ${TS_INDEX_IMPORT_ROOTS})
  list(APPEND imports "${root}")
  list(REMOVE_DUPLICATES imports)
  list(JOIN imports ", " joined)
  set(TS_INDEX_IMPORT_ROOTS "${imports}" PARENT_SCOPE)
  set(TS_INDEX_IMPORTS "${joined}" PARENT_SCOPE)
  set(${out_var} "${dotted}" PARENT_SCOPE)
endfunction()
