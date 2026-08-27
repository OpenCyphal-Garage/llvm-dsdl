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
set(work_dir "${OUT_DIR}/work")
file(MAKE_DIRECTORY "${fixture_root}/demo")
file(MAKE_DIRECTORY "${ts_out}")
file(MAKE_DIRECTORY "${work_dir}")

file(WRITE
  "${fixture_root}/demo/Inner.1.0.dsdl"
  "uint8 x\n"
  "@sealed\n"
)
file(WRITE
  "${fixture_root}/demo/UnionArray.1.0.dsdl"
  "@union\n"
  "uint8[2] fixed\n"
  "demo.Inner.1.0[<=2] var_composite\n"
  "@sealed\n"
)

execute_process(
  COMMAND
    "${DSDLC}" --target-language ts --versioned-type-names
      "${fixture_root}"
      --outdir "${ts_out}"
      --ts-module "ts_runtime_union_array_smoke"
  RESULT_VARIABLE ts_gen_result
  OUTPUT_VARIABLE ts_gen_stdout
  ERROR_VARIABLE ts_gen_stderr
)
if(NOT ts_gen_result EQUAL 0)
  message(STATUS "dsdlc ts stdout:\n${ts_gen_stdout}")
  message(STATUS "dsdlc ts stderr:\n${ts_gen_stderr}")
  message(FATAL_ERROR "TypeScript generation failed for union-array smoke")
endif()

set(ts_index "${ts_out}/index.ts")
if(NOT EXISTS "${ts_index}")
  message(FATAL_ERROR "generated TypeScript index missing: ${ts_index}")
endif()
file(READ "${ts_index}" ts_index_content)
string(REGEX MATCH "export \\* as ([A-Za-z0-9_]+) from \"\\./[^\"]*union_array_1_0\";" ts_type_export "${ts_index_content}")
if("${CMAKE_MATCH_1}" STREQUAL "")
  message(FATAL_ERROR "failed to locate union_array_1_0 export alias in ${ts_index}")
endif()
set(ts_type_module "${CMAKE_MATCH_1}")

file(WRITE
  "${ts_out}/runtime_union_array_smoke.ts"
  "import { ${ts_type_module} } from \"./index\";\n"
  "\n"
  "const fixedObj: ${ts_type_module}.UnionArray_1_0 = { _tag: 0, fixed: [9, 10] };\n"
  "const fixedBytes = ${ts_type_module}.serializeUnionArray_1_0(fixedObj);\n"
  "console.log(fixedBytes[0] + \" \" + fixedBytes[1] + \" \" + fixedBytes[2]);\n"
  "const fixedDecoded = ${ts_type_module}.deserializeUnionArray_1_0(fixedBytes);\n"
  "if (fixedDecoded.value._tag !== 0 || !(\"fixed\" in fixedDecoded.value)) {\n"
  "  throw new Error(\"unexpected fixed decoded variant\");\n"
  "}\n"
  "console.log(fixedDecoded.value._tag + \" \" + fixedDecoded.value.fixed[0] + \" \" + fixedDecoded.value.fixed[1] + \" \" + fixedDecoded.consumed);\n"
  "\n"
  "const varObj: ${ts_type_module}.UnionArray_1_0 = { _tag: 1, var_composite: [{ x: 17 }, { x: 34 }] };\n"
  "const varBytes = ${ts_type_module}.serializeUnionArray_1_0(varObj);\n"
  "console.log(varBytes[0] + \" \" + varBytes[1] + \" \" + varBytes[2] + \" \" + varBytes[3]);\n"
  "const varDecoded = ${ts_type_module}.deserializeUnionArray_1_0(varBytes);\n"
  "if (varDecoded.value._tag !== 1 || !(\"var_composite\" in varDecoded.value)) {\n"
  "  throw new Error(\"unexpected variable decoded variant\");\n"
  "}\n"
  "console.log(varDecoded.value._tag + \" \" + varDecoded.value.var_composite.length + \" \" + varDecoded.value.var_composite[0].x + \" \" + varDecoded.value.var_composite[1].x + \" \" + varDecoded.consumed);\n"
)

file(WRITE
  "${ts_out}/tsconfig-runtime-union-array-smoke.json"
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
  COMMAND "${TSC_EXECUTABLE}" -p "${ts_out}/tsconfig-runtime-union-array-smoke.json" --pretty false
  WORKING_DIRECTORY "${ts_out}"
  RESULT_VARIABLE tsc_result
  OUTPUT_VARIABLE tsc_stdout
  ERROR_VARIABLE tsc_stderr
)
if(NOT tsc_result EQUAL 0)
  message(STATUS "tsc stdout:\n${tsc_stdout}")
  message(STATUS "tsc stderr:\n${tsc_stderr}")
  message(FATAL_ERROR "failed to compile union-array TypeScript runtime smoke")
endif()

file(WRITE "${ts_out}/js/package.json" "{\n  \"type\": \"commonjs\"\n}\n")

execute_process(
  COMMAND "${NODE_EXECUTABLE}" "${ts_out}/js/runtime_union_array_smoke.js"
  RESULT_VARIABLE node_result
  OUTPUT_VARIABLE node_stdout
  ERROR_VARIABLE node_stderr
)
if(NOT node_result EQUAL 0)
  message(STATUS "node stdout:\n${node_stdout}")
  message(STATUS "node stderr:\n${node_stderr}")
  message(FATAL_ERROR "TypeScript union-array runtime smoke execution failed")
endif()

string(STRIP "${node_stdout}" actual_output)
set(expected_output "0 9 10\n0 9 10 3\n1 2 17 34\n1 2 17 34 4")
if(NOT actual_output STREQUAL expected_output)
  file(WRITE "${OUT_DIR}/expected-output.txt" "${expected_output}\n")
  file(WRITE "${OUT_DIR}/actual-output.txt" "${actual_output}\n")
  message(FATAL_ERROR
    "unexpected union-array runtime smoke output. See ${OUT_DIR}/expected-output.txt and ${OUT_DIR}/actual-output.txt.")
endif()

message(STATUS "TypeScript union-array runtime smoke passed")
