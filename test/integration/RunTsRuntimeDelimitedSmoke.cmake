cmake_minimum_required(VERSION 3.24)

foreach(var DSDLC OUT_DIR SOURCE_ROOT TSC_EXECUTABLE NODE_EXECUTABLE)
  if(NOT DEFINED ${var} OR "${${var}}" STREQUAL "")
    message(FATAL_ERROR "Missing required variable: ${var}")
  endif()
endforeach()

if(NOT EXISTS "${DSDLC}")
  message(FATAL_ERROR "dsdlc executable not found: ${DSDLC}")
endif()
if(NOT EXISTS "${SOURCE_ROOT}")
  message(FATAL_ERROR "source root not found: ${SOURCE_ROOT}")
endif()
if(NOT EXISTS "${TSC_EXECUTABLE}")
  message(FATAL_ERROR "tsc executable not found: ${TSC_EXECUTABLE}")
endif()
if(NOT EXISTS "${NODE_EXECUTABLE}")
  message(FATAL_ERROR "node executable not found: ${NODE_EXECUTABLE}")
endif()

file(REMOVE_RECURSE "${OUT_DIR}")
file(MAKE_DIRECTORY "${OUT_DIR}")

set(fixture_root "${OUT_DIR}/fixture_root")
set(ts_out "${OUT_DIR}/ts")
file(MAKE_DIRECTORY "${fixture_root}/demo")
file(MAKE_DIRECTORY "${ts_out}")

file(WRITE
  "${fixture_root}/demo/Delimited.1.0.dsdl"
  "uint8 value\n"
  "@extent 64\n"
)
file(WRITE
  "${fixture_root}/demo/UsesDelimited.1.0.dsdl"
  "demo.Delimited.1.0 nested\n"
  "@sealed\n"
)

file(WRITE
  "${fixture_root}/demo/EmptyDelimited.1.0.dsdl"
  "@extent 8\n"
)
file(WRITE
  "${fixture_root}/demo/UsesEmptyDelimited.1.0.dsdl"
  "demo.EmptyDelimited.1.0 nested\n"
  "@sealed\n"
)

file(WRITE
  "${fixture_root}/demo/MaxDelimited.1.0.dsdl"
  "uint8[64] bytes\n"
  "@extent 512\n"
)
file(WRITE
  "${fixture_root}/demo/UsesMaxDelimited.1.0.dsdl"
  "demo.MaxDelimited.1.0 nested\n"
  "@sealed\n"
)

file(WRITE
  "${fixture_root}/demo/DelimitedInner.1.0.dsdl"
  "uint8 value\n"
  "@extent 64\n"
)
file(WRITE
  "${fixture_root}/demo/DelimitedOuter.1.0.dsdl"
  "demo.DelimitedInner.1.0 inner\n"
  "@extent 128\n"
)
file(WRITE
  "${fixture_root}/demo/UsesNestedDelimited.1.0.dsdl"
  "demo.DelimitedOuter.1.0 nested\n"
  "@sealed\n"
)

execute_process(
  COMMAND
    "${DSDLC}" --target-language ts --versioned-type-names
      "${fixture_root}"
      --outdir "${ts_out}"
      --ts-module "ts_runtime_delimited_smoke"
  RESULT_VARIABLE ts_gen_result
  OUTPUT_VARIABLE ts_gen_stdout
  ERROR_VARIABLE ts_gen_stderr
)
if(NOT ts_gen_result EQUAL 0)
  message(STATUS "dsdlc ts stdout:\n${ts_gen_stdout}")
  message(STATUS "dsdlc ts stderr:\n${ts_gen_stderr}")
  message(FATAL_ERROR "TypeScript generation failed for delimited composite smoke")
endif()

set(ts_index "${ts_out}/index.ts")
if(NOT EXISTS "${ts_index}")
  message(FATAL_ERROR "generated TypeScript index missing: ${ts_index}")
endif()
file(READ "${ts_index}" ts_index_content)

string(REGEX MATCH "export \\* as ([A-Za-z0-9_]+) from \"\\./[^\"]*uses_delimited_1_0\";" ts_type_export "${ts_index_content}")
if("${CMAKE_MATCH_1}" STREQUAL "")
  message(FATAL_ERROR "failed to locate uses_delimited_1_0 export alias in ${ts_index}")
endif()
set(ts_type_module "${CMAKE_MATCH_1}")

string(REGEX MATCH "export \\* as ([A-Za-z0-9_]+) from \"\\./[^\"]*uses_empty_delimited_1_0\";" ts_empty_export "${ts_index_content}")
if("${CMAKE_MATCH_1}" STREQUAL "")
  message(FATAL_ERROR "failed to locate uses_empty_delimited_1_0 export alias in ${ts_index}")
endif()
set(ts_empty_module "${CMAKE_MATCH_1}")

string(REGEX MATCH "export \\* as ([A-Za-z0-9_]+) from \"\\./[^\"]*uses_max_delimited_1_0\";" ts_max_export "${ts_index_content}")
if("${CMAKE_MATCH_1}" STREQUAL "")
  message(FATAL_ERROR "failed to locate uses_max_delimited_1_0 export alias in ${ts_index}")
endif()
set(ts_max_module "${CMAKE_MATCH_1}")

string(REGEX MATCH "export \\* as ([A-Za-z0-9_]+) from \"\\./[^\"]*uses_nested_delimited_1_0\";" ts_nested_export "${ts_index_content}")
if("${CMAKE_MATCH_1}" STREQUAL "")
  message(FATAL_ERROR "failed to locate uses_nested_delimited_1_0 export alias in ${ts_index}")
endif()
set(ts_nested_module "${CMAKE_MATCH_1}")

file(WRITE
  "${ts_out}/runtime_delimited_smoke.ts"
  "import { ${ts_type_module}, ${ts_empty_module}, ${ts_max_module}, ${ts_nested_module} } from \"./index\";\n"
  "\n"
  "const basicIn: ${ts_type_module}.UsesDelimited_1_0 = { nested: { value: 171 } };\n"
  "const basicBytes = ${ts_type_module}.serializeUsesDelimited_1_0(basicIn);\n"
  "if (basicBytes.length !== 5 || basicBytes[0] !== 1 || basicBytes[1] !== 0 || basicBytes[2] !== 0 || basicBytes[3] !== 0 || basicBytes[4] !== 171) {\n"
  "  throw new Error(\"unexpected basic delimited serialization\");\n"
  "}\n"
  "const basicDecoded = ${ts_type_module}.deserializeUsesDelimited_1_0(basicBytes);\n"
  "if (basicDecoded.value.nested.value !== 171 || basicDecoded.consumed !== 5) {\n"
  "  throw new Error(\"unexpected basic delimited deserialize\");\n"
  "}\n"
  "console.log(\"delimited_basic_ok\");\n"
  "\n"
  "const zeroIn: ${ts_empty_module}.UsesEmptyDelimited_1_0 = { nested: {} };\n"
  "const zeroBytes = ${ts_empty_module}.serializeUsesEmptyDelimited_1_0(zeroIn);\n"
  "if (zeroBytes.length !== 4 || zeroBytes[0] !== 0 || zeroBytes[1] !== 0 || zeroBytes[2] !== 0 || zeroBytes[3] !== 0) {\n"
  "  throw new Error(\"unexpected zero-payload delimiter serialization\");\n"
  "}\n"
  "const zeroDecoded = ${ts_empty_module}.deserializeUsesEmptyDelimited_1_0(zeroBytes);\n"
  "if (zeroDecoded.consumed !== 4) {\n"
  "  throw new Error(\"unexpected zero-payload delimiter deserialize\");\n"
  "}\n"
  "console.log(\"delimited_zero_payload_ok\");\n"
  "\n"
  "const maxPayload = Array.from({ length: 64 }, (_v, i) => i);\n"
  "const maxIn: ${ts_max_module}.UsesMaxDelimited_1_0 = { nested: { bytes: maxPayload } };\n"
  "const maxBytes = ${ts_max_module}.serializeUsesMaxDelimited_1_0(maxIn);\n"
  "if (maxBytes.length !== 68 || maxBytes[0] !== 64 || maxBytes[1] !== 0 || maxBytes[2] !== 0 || maxBytes[3] !== 0 || maxBytes[4] !== 0 || maxBytes[67] !== 63) {\n"
  "  throw new Error(\"unexpected max-payload delimiter serialization\");\n"
  "}\n"
  "const maxDecoded = ${ts_max_module}.deserializeUsesMaxDelimited_1_0(maxBytes);\n"
  "if (maxDecoded.consumed !== 68 || maxDecoded.value.nested.bytes.length !== 64 || maxDecoded.value.nested.bytes[0] !== 0 || maxDecoded.value.nested.bytes[63] !== 63) {\n"
  "  throw new Error(\"unexpected max-payload delimiter deserialize\");\n"
  "}\n"
  "console.log(\"delimited_max_payload_ok\");\n"
  "\n"
  "const nestedIn: ${ts_nested_module}.UsesNestedDelimited_1_0 = { nested: { inner: { value: 171 } } };\n"
  "const nestedBytes = ${ts_nested_module}.serializeUsesNestedDelimited_1_0(nestedIn);\n"
  "if (nestedBytes.length !== 9 || nestedBytes[0] !== 5 || nestedBytes[1] !== 0 || nestedBytes[2] !== 0 || nestedBytes[3] !== 0 || nestedBytes[4] !== 1 || nestedBytes[8] !== 171) {\n"
  "  throw new Error(\"unexpected nested delimiter-chain serialization\");\n"
  "}\n"
  "const nestedDecoded = ${ts_nested_module}.deserializeUsesNestedDelimited_1_0(nestedBytes);\n"
  "if (nestedDecoded.consumed !== 9 || nestedDecoded.value.nested.inner.value !== 171) {\n"
  "  throw new Error(\"unexpected nested delimiter-chain deserialize\");\n"
  "}\n"
  "console.log(\"delimited_nested_chain_ok\");\n"
)

file(WRITE
  "${ts_out}/tsconfig-runtime-delimited-smoke.json"
  "{\n"
  "  \"compilerOptions\": {\n"
  "    \"target\": \"ES2022\",\n"
  "    \"module\": \"CommonJS\",\n"
  "    \"moduleResolution\": \"Node\",\n"
  "    \"strict\": true,\n"
  "    \"skipLibCheck\": true,\n"
  "    \"outDir\": \"./js\"\n"
  "  },\n"
  "  \"include\": [\"./**/*.ts\"]\n"
  "}\n"
)

execute_process(
  COMMAND "${TSC_EXECUTABLE}" -p "${ts_out}/tsconfig-runtime-delimited-smoke.json" --pretty false
  WORKING_DIRECTORY "${ts_out}"
  RESULT_VARIABLE tsc_result
  OUTPUT_VARIABLE tsc_stdout
  ERROR_VARIABLE tsc_stderr
)
if(NOT tsc_result EQUAL 0)
  message(STATUS "tsc stdout:\n${tsc_stdout}")
  message(STATUS "tsc stderr:\n${tsc_stderr}")
  message(FATAL_ERROR "failed to compile delimited TypeScript runtime smoke")
endif()

file(WRITE "${ts_out}/js/package.json" "{\n  \"type\": \"commonjs\"\n}\n")

execute_process(
  COMMAND "${NODE_EXECUTABLE}" "${ts_out}/js/runtime_delimited_smoke.js"
  RESULT_VARIABLE node_result
  OUTPUT_VARIABLE node_stdout
  ERROR_VARIABLE node_stderr
)
if(NOT node_result EQUAL 0)
  message(STATUS "node stdout:\n${node_stdout}")
  message(STATUS "node stderr:\n${node_stderr}")
  message(FATAL_ERROR "TypeScript delimited runtime smoke execution failed")
endif()

string(STRIP "${node_stdout}" actual_output)
set(expected_output [=[delimited_basic_ok
delimited_zero_payload_ok
delimited_max_payload_ok
delimited_nested_chain_ok]=])
if(NOT actual_output STREQUAL expected_output)
  file(WRITE "${OUT_DIR}/expected-output.txt" "${expected_output}\n")
  file(WRITE "${OUT_DIR}/actual-output.txt" "${actual_output}\n")
  message(FATAL_ERROR
    "unexpected delimited runtime smoke output. See ${OUT_DIR}/expected-output.txt and ${OUT_DIR}/actual-output.txt.")
endif()

message(STATUS "TypeScript delimited runtime smoke passed")
