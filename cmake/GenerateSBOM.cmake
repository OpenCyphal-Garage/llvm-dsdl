#===----------------------------------------------------------------------===#
##
## @file
## Emits a CycloneDX 1.5 SBOM describing the release artifacts and their provenance.
##
## Run in script mode (`cmake -P`) so the SBOM can be regenerated without reconfiguring. Required
## inputs: OUTPUT_FILE, TOOL_VERSION, LLVM_VERSION, SOURCE_ROOT, TOOLS (";"-separated). Optional:
## ZSTD_VERSION, SOURCE_DATE_EPOCH_VALUE.
##
## Determinism: this project pins byte-reproducible output (see the determinism lanes), so the document
## carries NO wall-clock timestamp unless SOURCE_DATE_EPOCH_VALUE is supplied, and no random serial
## number. Regenerating from the same commit yields a byte-identical SBOM.
##
#===----------------------------------------------------------------------===#
cmake_minimum_required(VERSION 3.24)

foreach(var OUTPUT_FILE TOOL_VERSION LLVM_VERSION SOURCE_ROOT TOOLS)
  if(NOT DEFINED ${var} OR "${${var}}" STREQUAL "")
    message(FATAL_ERROR "GenerateSBOM: missing required variable: ${var}")
  endif()
endforeach()

# --- Provenance: exact source revision and the pinned submodule commits ------------------------------
# Best-effort: a source tarball (no .git) still produces a valid SBOM, just without VCS pedigree.
set(source_commit "unknown")
set(submodule_components "")
find_program(SBOM_GIT_EXECUTABLE git QUIET)
if(SBOM_GIT_EXECUTABLE AND EXISTS "${SOURCE_ROOT}/.git")
  execute_process(
    COMMAND "${SBOM_GIT_EXECUTABLE}" -C "${SOURCE_ROOT}" rev-parse HEAD
    OUTPUT_VARIABLE source_commit
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
  if("${source_commit}" STREQUAL "")
    set(source_commit "unknown")
  endif()

  # Submodules are build/test inputs (the DSDL corpus and libudpard for the examples); they are not
  # linked into the shipped binaries, so they are recorded with scope "excluded" for provenance only.
  execute_process(
    COMMAND "${SBOM_GIT_EXECUTABLE}" -C "${SOURCE_ROOT}" submodule status
    OUTPUT_VARIABLE submodule_status
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
  string(REPLACE "\n" ";" submodule_lines "${submodule_status}")
  foreach(line IN LISTS submodule_lines)
    # Format: "<+|-| ><sha1> <path> (<describe>)"
    if(line MATCHES "^[ +-]?([0-9a-f]+) +([^ ]+)")
      set(sm_sha "${CMAKE_MATCH_1}")
      set(sm_path "${CMAKE_MATCH_2}")
      get_filename_component(sm_name "${sm_path}" NAME)
      string(APPEND submodule_components
"    {
      \"type\": \"library\",
      \"name\": \"${sm_name}\",
      \"version\": \"${sm_sha}\",
      \"scope\": \"excluded\",
      \"description\": \"Build/test input pinned at ${sm_sha} (${sm_path}); not linked into shipped artifacts.\"
    },
")
    endif()
  endforeach()
endif()

# --- Shipped tools ----------------------------------------------------------------------------------
set(tool_properties "")
foreach(tool IN LISTS TOOLS)
  string(APPEND tool_properties "        { \"name\": \"llvm-dsdl:tool\", \"value\": \"${tool}\" },\n")
endforeach()
string(REGEX REPLACE ",\n$" "\n" tool_properties "${tool_properties}")

# --- Optional linked third-party --------------------------------------------------------------------
set(zstd_component "")
if(DEFINED ZSTD_VERSION AND NOT "${ZSTD_VERSION}" STREQUAL "")
  set(zstd_component
"    {
      \"type\": \"library\",
      \"name\": \"zstd\",
      \"version\": \"${ZSTD_VERSION}\",
      \"scope\": \"required\",
      \"licenses\": [ { \"license\": { \"id\": \"BSD-3-Clause\" } } ],
      \"description\": \"Compression library pulled in transitively by LLVM.\"
    },
")
endif()

set(timestamp_field "")
if(DEFINED SOURCE_DATE_EPOCH_VALUE AND NOT "${SOURCE_DATE_EPOCH_VALUE}" STREQUAL "")
  string(TIMESTAMP sbom_timestamp "%Y-%m-%dT%H:%M:%SZ" UTC)
  set(timestamp_field "    \"timestamp\": \"${sbom_timestamp}\",\n")
endif()

file(WRITE "${OUTPUT_FILE}"
"{
  \"bomFormat\": \"CycloneDX\",
  \"specVersion\": \"1.5\",
  \"version\": 1,
  \"metadata\": {
${timestamp_field}    \"component\": {
      \"type\": \"application\",
      \"name\": \"llvm-dsdl\",
      \"version\": \"${TOOL_VERSION}\",
      \"description\": \"LLVM/MLIR-based DSDL (OpenCyphal) compiler: dsdlc, dsdl-opt, dsdld.\",
      \"licenses\": [ { \"license\": { \"id\": \"MIT\" } } ],
      \"properties\": [
${tool_properties}      ],
      \"pedigree\": {
        \"commits\": [ { \"uid\": \"${source_commit}\" } ]
      }
    }
  },
  \"components\": [
    {
      \"type\": \"library\",
      \"name\": \"llvm\",
      \"version\": \"${LLVM_VERSION}\",
      \"scope\": \"required\",
      \"licenses\": [ { \"license\": { \"id\": \"Apache-2.0 WITH LLVM-exception\" } } ],
      \"description\": \"LLVM support libraries linked into the shipped tools. The major version is locked; see docs/SUPPLY_CHAIN.md.\"
    },
    {
      \"type\": \"library\",
      \"name\": \"mlir\",
      \"version\": \"${LLVM_VERSION}\",
      \"scope\": \"required\",
      \"licenses\": [ { \"license\": { \"id\": \"Apache-2.0 WITH LLVM-exception\" } } ],
      \"description\": \"MLIR dialect/pass infrastructure, versioned and released with LLVM.\"
    },
${zstd_component}${submodule_components}    {
      \"type\": \"library\",
      \"name\": \"llvm-dsdl-runtime\",
      \"version\": \"${TOOL_VERSION}\",
      \"scope\": \"required\",
      \"licenses\": [ { \"license\": { \"id\": \"MIT\" } } ],
      \"description\": \"First-party serialization runtime shipped as headers/sources alongside generated code.\"
    }
  ]
}
")

message(STATUS "SBOM written: ${OUTPUT_FILE} (llvm-dsdl ${TOOL_VERSION}, LLVM ${LLVM_VERSION}, commit ${source_commit})")
