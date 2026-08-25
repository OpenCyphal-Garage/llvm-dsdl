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
  "uint8 a\n"
  "uint3 b\n"
  "@sealed\n"
)
file(WRITE
  "${fixture_root}/demo/UnionComposite.1.0.dsdl"
  "@union\n"
  "demo.Inner.1.0 inner\n"
  "uint8 flag\n"
  "@sealed\n"
)

execute_process(
  COMMAND
    "${DSDLC}" --target-language ts --versioned-type-names
      "${fixture_root}"
      --outdir "${ts_out}"
      --ts-module "ts_runtime_union_composite_smoke"
  RESULT_VARIABLE ts_gen_result
  OUTPUT_VARIABLE ts_gen_stdout
  ERROR_VARIABLE ts_gen_stderr
)
if(NOT ts_gen_result EQUAL 0)
  message(STATUS "dsdlc ts stdout:\n${ts_gen_stdout}")
  message(STATUS "dsdlc ts stderr:\n${ts_gen_stderr}")
  message(FATAL_ERROR "TypeScript generation failed for union composite smoke")
endif()

set(ts_index "${ts_out}/index.ts")
if(NOT EXISTS "${ts_index}")
  message(FATAL_ERROR "generated TypeScript index missing: ${ts_index}")
endif()
file(READ "${ts_index}" ts_index_content)
string(REGEX MATCH "export \\* as ([A-Za-z0-9_]+) from \"\\./[^\"]*union_composite_1_0\";" ts_type_export "${ts_index_content}")
if("${CMAKE_MATCH_1}" STREQUAL "")
  message(FATAL_ERROR "failed to locate union_composite_1_0 export alias in ${ts_index}")
endif()
set(ts_type_module "${CMAKE_MATCH_1}")

file(WRITE
  "${ts_out}/runtime_union_composite_smoke.ts"
  "import { ${ts_type_module} } from \"./index\";\n"
  "\n"
  "const inObj: ${ts_type_module}.UnionComposite_1_0 = { _tag: 0, inner: { a: 18, b: 6 } };\n"
  "const outBytes = ${ts_type_module}.serializeUnionComposite_1_0(inObj);\n"
  "if (outBytes.length !== 3) {\n"
  "  throw new Error(\"unexpected serialized size \" + outBytes.length);\n"
  "}\n"
  "console.log(outBytes[0] + \" \" + outBytes[1] + \" \" + outBytes[2]);\n"
  "\n"
  "const decoded = ${ts_type_module}.deserializeUnionComposite_1_0(outBytes);\n"
  "if (decoded.value._tag !== 0 || !(\"inner\" in decoded.value)) {\n"
  "  throw new Error(\"unexpected decoded union variant\");\n"
  "}\n"
  "console.log(decoded.value._tag + \" \" + decoded.value.inner.a + \" \" + decoded.value.inner.b + \" \" + decoded.consumed);\n"
)

file(WRITE
  "${ts_out}/tsconfig-runtime-union-composite-smoke.json"
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
  COMMAND "${TSC_EXECUTABLE}" -p "${ts_out}/tsconfig-runtime-union-composite-smoke.json" --pretty false
  WORKING_DIRECTORY "${ts_out}"
  RESULT_VARIABLE tsc_result
  OUTPUT_VARIABLE tsc_stdout
  ERROR_VARIABLE tsc_stderr
)
if(NOT tsc_result EQUAL 0)
  message(STATUS "tsc stdout:\n${tsc_stdout}")
  message(STATUS "tsc stderr:\n${tsc_stderr}")
  message(FATAL_ERROR "failed to compile union composite TypeScript runtime smoke")
endif()

file(WRITE "${ts_out}/js/package.json" "{\n  \"type\": \"commonjs\"\n}\n")

execute_process(
  COMMAND "${NODE_EXECUTABLE}" "${ts_out}/js/runtime_union_composite_smoke.js"
  RESULT_VARIABLE node_result
  OUTPUT_VARIABLE node_stdout
  ERROR_VARIABLE node_stderr
)
if(NOT node_result EQUAL 0)
  message(STATUS "node stdout:\n${node_stdout}")
  message(STATUS "node stderr:\n${node_stderr}")
  message(FATAL_ERROR "TypeScript union composite runtime smoke execution failed")
endif()

string(STRIP "${node_stdout}" actual_output)
set(expected_output "0 18 6\n0 18 6 3")
if(NOT actual_output STREQUAL expected_output)
  file(WRITE "${OUT_DIR}/expected-output.txt" "${expected_output}\n")
  file(WRITE "${OUT_DIR}/actual-output.txt" "${actual_output}\n")
  message(FATAL_ERROR
    "unexpected union composite runtime smoke output. See ${OUT_DIR}/expected-output.txt and ${OUT_DIR}/actual-output.txt.")
endif()

message(STATUS "TypeScript union composite runtime smoke passed")
