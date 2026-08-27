cmake_minimum_required(VERSION 3.24)

foreach(var DSDLC OUT_DIR SOURCE_ROOT C_COMPILER TSC_EXECUTABLE NODE_EXECUTABLE)
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
if(NOT EXISTS "${C_COMPILER}")
  message(FATAL_ERROR "C compiler not found: ${C_COMPILER}")
endif()
if(NOT EXISTS "${TSC_EXECUTABLE}")
  message(FATAL_ERROR "tsc executable not found: ${TSC_EXECUTABLE}")
endif()
if(NOT EXISTS "${NODE_EXECUTABLE}")
  message(FATAL_ERROR "node executable not found: ${NODE_EXECUTABLE}")
endif()

file(REMOVE_RECURSE "${OUT_DIR}")
file(MAKE_DIRECTORY "${OUT_DIR}")

set(fixture_root "${OUT_DIR}/demo")
set(c_out "${OUT_DIR}/c")
set(ts_out "${OUT_DIR}/ts")
set(work_dir "${OUT_DIR}/work")
file(MAKE_DIRECTORY "${fixture_root}")
file(MAKE_DIRECTORY "${c_out}")
file(MAKE_DIRECTORY "${ts_out}")
file(MAKE_DIRECTORY "${work_dir}")

file(WRITE
  "${fixture_root}/Inner.1.0.dsdl"
  "uint8 x\n"
  "@sealed\n"
)
file(WRITE
  "${fixture_root}/UnionArray.1.0.dsdl"
  "@union\n"
  "uint8[2] fixed\n"
  "demo.Inner.1.0[<=2] var_composite\n"
  "@sealed\n"
)

execute_process(
  COMMAND
    "${DSDLC}" --target-language c
      "${fixture_root}"
      --outdir "${c_out}"
  RESULT_VARIABLE c_gen_result
  OUTPUT_VARIABLE c_gen_stdout
  ERROR_VARIABLE c_gen_stderr
)
if(NOT c_gen_result EQUAL 0)
  message(STATUS "dsdlc c stdout:\n${c_gen_stdout}")
  message(STATUS "dsdlc c stderr:\n${c_gen_stderr}")
  message(FATAL_ERROR "fixture C generation failed")
endif()

execute_process(
  COMMAND
    "${DSDLC}" --target-language ts --versioned-type-names
      "${fixture_root}"
      --outdir "${ts_out}"
      --ts-module "fixture_ts_union_array_parity"
  RESULT_VARIABLE ts_gen_result
  OUTPUT_VARIABLE ts_gen_stdout
  ERROR_VARIABLE ts_gen_stderr
)
if(NOT ts_gen_result EQUAL 0)
  message(STATUS "dsdlc ts stdout:\n${ts_gen_stdout}")
  message(STATUS "dsdlc ts stderr:\n${ts_gen_stderr}")
  message(FATAL_ERROR "fixture TypeScript generation failed")
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

set(c_harness_src "${work_dir}/c_union_array_parity_harness.c")
file(WRITE
  "${c_harness_src}"
  "#include <stdint.h>\n"
  "#include <stdio.h>\n"
  "#include \"demo/Inner_1_0.h\"\n"
  "#include \"demo/UnionArray_1_0.h\"\n"
  "\n"
  "int main(void) {\n"
  "  demo__UnionArray fixed_obj;\n"
  "  fixed_obj._tag_ = 0U;\n"
  "  fixed_obj.fixed[0] = 9U;\n"
  "  fixed_obj.fixed[1] = 10U;\n"
  "  uint8_t fixed_bytes[8] = {0};\n"
  "  size_t fixed_size = sizeof(fixed_bytes);\n"
  "  const int8_t fixed_ser_rc = demo__UnionArray__serialize_(&fixed_obj, fixed_bytes, &fixed_size);\n"
  "  if ((fixed_ser_rc != 0) || (fixed_size != 3U)) {\n"
  "    return 2;\n"
  "  }\n"
  "  printf(\"%u %u %u\\n\", (unsigned) fixed_bytes[0], (unsigned) fixed_bytes[1], (unsigned) fixed_bytes[2]);\n"
  "  demo__UnionArray fixed_out;\n"
  "  fixed_out._tag_ = 1U;\n"
  "  fixed_out.var_composite.count = 0U;\n"
  "  size_t fixed_consumed = fixed_size;\n"
  "  const int8_t fixed_de_rc = demo__UnionArray__deserialize_(&fixed_out, fixed_bytes, &fixed_consumed);\n"
  "  if ((fixed_de_rc != 0) || (fixed_consumed != 3U) || (fixed_out._tag_ != 0U)) {\n"
  "    return 3;\n"
  "  }\n"
  "  printf(\"%u %u %u %u\\n\", (unsigned) fixed_out._tag_, (unsigned) fixed_out.fixed[0], (unsigned) fixed_out.fixed[1], (unsigned) fixed_consumed);\n"
  "\n"
  "  demo__UnionArray var_obj;\n"
  "  var_obj._tag_ = 1U;\n"
  "  var_obj.var_composite.count = 2U;\n"
  "  var_obj.var_composite.elements[0].x = 17U;\n"
  "  var_obj.var_composite.elements[1].x = 34U;\n"
  "  uint8_t var_bytes[8] = {0};\n"
  "  size_t var_size = sizeof(var_bytes);\n"
  "  const int8_t var_ser_rc = demo__UnionArray__serialize_(&var_obj, var_bytes, &var_size);\n"
  "  if ((var_ser_rc != 0) || (var_size != 4U)) {\n"
  "    return 4;\n"
  "  }\n"
  "  printf(\"%u %u %u %u\\n\", (unsigned) var_bytes[0], (unsigned) var_bytes[1], (unsigned) var_bytes[2], (unsigned) var_bytes[3]);\n"
  "  demo__UnionArray var_out;\n"
  "  var_out._tag_ = 0U;\n"
  "  var_out.fixed[0] = 0U;\n"
  "  var_out.fixed[1] = 0U;\n"
  "  size_t var_consumed = var_size;\n"
  "  const int8_t var_de_rc = demo__UnionArray__deserialize_(&var_out, var_bytes, &var_consumed);\n"
  "  if ((var_de_rc != 0) || (var_consumed != 4U) || (var_out._tag_ != 1U) || (var_out.var_composite.count != 2U)) {\n"
  "    return 5;\n"
  "  }\n"
  "  printf(\"%u %u %u %u %u\\n\", (unsigned) var_out._tag_, (unsigned) var_out.var_composite.count, (unsigned) var_out.var_composite.elements[0].x, (unsigned) var_out.var_composite.elements[1].x, (unsigned) var_consumed);\n"
  "  return 0;\n"
  "}\n"
)

set(c_harness_bin "${work_dir}/c_union_array_parity_harness")
execute_process(
  COMMAND
    "${C_COMPILER}"
      -std=c11
      -Wall
      -Wextra
      -Werror
      -I "${c_out}"
      "${c_harness_src}"
      "${c_out}/demo/Inner_1_0.c"
      "${c_out}/demo/UnionArray_1_0.c"
      -o "${c_harness_bin}"
  RESULT_VARIABLE c_cc_result
  OUTPUT_VARIABLE c_cc_stdout
  ERROR_VARIABLE c_cc_stderr
)
if(NOT c_cc_result EQUAL 0)
  message(STATUS "C compile stdout:\n${c_cc_stdout}")
  message(STATUS "C compile stderr:\n${c_cc_stderr}")
  message(FATAL_ERROR "failed to compile fixture C union-array parity harness")
endif()

execute_process(
  COMMAND "${c_harness_bin}"
  RESULT_VARIABLE c_run_result
  OUTPUT_VARIABLE c_run_stdout
  ERROR_VARIABLE c_run_stderr
)
if(NOT c_run_result EQUAL 0)
  message(STATUS "C harness stdout:\n${c_run_stdout}")
  message(STATUS "C harness stderr:\n${c_run_stderr}")
  message(FATAL_ERROR "fixture C union-array parity harness failed")
endif()

file(WRITE
  "${ts_out}/runtime_union_array_parity_smoke.ts"
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
  "${ts_out}/tsconfig-runtime-union-array-parity.json"
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
  COMMAND "${TSC_EXECUTABLE}" -p "${ts_out}/tsconfig-runtime-union-array-parity.json" --pretty false
  WORKING_DIRECTORY "${ts_out}"
  RESULT_VARIABLE tsc_result
  OUTPUT_VARIABLE tsc_stdout
  ERROR_VARIABLE tsc_stderr
)
if(NOT tsc_result EQUAL 0)
  message(STATUS "tsc stdout:\n${tsc_stdout}")
  message(STATUS "tsc stderr:\n${tsc_stderr}")
  message(FATAL_ERROR "failed to compile fixture TypeScript union-array parity smoke")
endif()

file(WRITE "${ts_out}/js/package.json" "{\n  \"type\": \"commonjs\"\n}\n")

execute_process(
  COMMAND "${NODE_EXECUTABLE}" "${ts_out}/js/runtime_union_array_parity_smoke.js"
  RESULT_VARIABLE node_result
  OUTPUT_VARIABLE node_stdout
  ERROR_VARIABLE node_stderr
)
if(NOT node_result EQUAL 0)
  message(STATUS "node stdout:\n${node_stdout}")
  message(STATUS "node stderr:\n${node_stderr}")
  message(FATAL_ERROR "TypeScript union-array parity smoke execution failed")
endif()

string(STRIP "${c_run_stdout}" c_output)
string(STRIP "${node_stdout}" ts_output)
if(NOT c_output STREQUAL ts_output)
  file(WRITE "${OUT_DIR}/c-output.txt" "${c_output}\n")
  file(WRITE "${OUT_DIR}/ts-output.txt" "${ts_output}\n")
  message(FATAL_ERROR
    "C vs TypeScript union-array parity mismatch. See ${OUT_DIR}/c-output.txt and ${OUT_DIR}/ts-output.txt.")
endif()

message(STATUS "fixture C<->TypeScript union-array parity smoke passed")
