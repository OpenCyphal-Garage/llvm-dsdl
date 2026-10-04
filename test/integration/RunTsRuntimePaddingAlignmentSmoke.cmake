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

set(fixture_root "${OUT_DIR}/demo")
set(ts_out "${OUT_DIR}/ts")
file(MAKE_DIRECTORY "${fixture_root}")
file(MAKE_DIRECTORY "${ts_out}")

file(WRITE
  "${fixture_root}/Inner.1.0.dsdl"
  "uint8 value\n"
  "@sealed\n"
)
file(WRITE
  "${fixture_root}/Outer.1.0.dsdl"
  "uint1 head\n"
  "demo.Inner.1.0 inner\n"
  "void7\n"
  "uint1 tail\n"
  "@sealed\n"
)
file(WRITE
  "${fixture_root}/Choice.1.0.dsdl"
  "@union\n"
  "bool flag\n"
  "demo.Inner.1.0 inner\n"
  "@sealed\n"
)

execute_process(
  COMMAND
    "${DSDLC}" --target-language ts --experimental-languages --versioned-type-names
      "${fixture_root}"
      --outdir "${ts_out}"
      --ts-module "ts_runtime_padding_alignment_smoke"
  RESULT_VARIABLE ts_gen_result
  OUTPUT_VARIABLE ts_gen_stdout
  ERROR_VARIABLE ts_gen_stderr
)
if(NOT ts_gen_result EQUAL 0)
  message(STATUS "dsdlc ts stdout:\n${ts_gen_stdout}")
  message(STATUS "dsdlc ts stderr:\n${ts_gen_stderr}")
  message(FATAL_ERROR "TypeScript generation failed for padding/alignment smoke")
endif()

set(outer_file "${ts_out}/demo/outer_1_0.ts")
set(choice_file "${ts_out}/demo/choice_1_0.ts")
if(NOT EXISTS "${outer_file}")
  message(FATAL_ERROR "expected generated file missing: ${outer_file}")
endif()
if(NOT EXISTS "${choice_file}")
  message(FATAL_ERROR "expected generated file missing: ${choice_file}")
endif()

file(READ "${outer_file}" outer_text)
file(READ "${choice_file}" choice_text)
# Outer's layout is fixed, so the seven bits that align `inner` and the void field after it
# are each one write at an offset that is a number. Choice's arms end at different bits, so
# its final alignment is computed at run time.
if(NOT outer_text MATCHES "writeUnsigned\\(buffer, Number\\(1n\\), 7, 0n, false\\)")
  message(FATAL_ERROR "outer runtime is missing the 7-bit alignment write before the nested composite")
endif()
if(NOT outer_text MATCHES "writeUnsigned\\(buffer, Number\\(16n\\), 7, 0n, false\\)")
  message(FATAL_ERROR "outer runtime is missing the void7 write")
endif()
foreach(pattern "= [A-Za-z0-9_]+ \\+ 7n;" "= [A-Za-z0-9_]+ / 8n;" "= [A-Za-z0-9_]+ \\* 8n;")
  if(NOT choice_text MATCHES "${pattern}")
    message(FATAL_ERROR "choice runtime is missing expected 8-bit alignment progression")
  endif()
endforeach()

include("${CMAKE_CURRENT_LIST_DIR}/TsModulePath.cmake")
llvmdsdl_ts_module("${ts_out}" "outer_1_0" ts_outer_module)
llvmdsdl_ts_module("${ts_out}" "choice_1_0" ts_choice_module)

file(WRITE
  "${ts_out}/runtime_padding_alignment_smoke.ts"
  "import { ${TS_INDEX_IMPORTS} } from \"./index\";\n"
  "\n"
  "const outerIn: ${ts_outer_module}.Outer_1_0 = { head: 1, inner: { value: 170 }, tail: 1 };\n"
  "const outerBytes = ${ts_outer_module}.Outer_1_0.serialize(outerIn);\n"
  "if (outerBytes.length !== 3) {\n"
  "  throw new Error(\"unexpected outer serialized size \" + outerBytes.length);\n"
  "}\n"
  "console.log(outerBytes[0] + \" \" + outerBytes[1] + \" \" + outerBytes[2]);\n"
  "const outerDecoded = ${ts_outer_module}.Outer_1_0.deserialize(outerBytes);\n"
  "console.log(outerDecoded.value.head + \" \" + outerDecoded.value.inner.value + \" \" + outerDecoded.value.tail + \" \" + outerDecoded.consumed);\n"
  "\n"
  "const choiceInnerIn: ${ts_choice_module}.Choice_1_0 = { _tag: 1, inner: { value: 170 } };\n"
  "const choiceInnerBytes = ${ts_choice_module}.Choice_1_0.serialize(choiceInnerIn);\n"
  "if (choiceInnerBytes.length !== 2) {\n"
  "  throw new Error(\"unexpected choice-inner serialized size \" + choiceInnerBytes.length);\n"
  "}\n"
  "console.log(choiceInnerBytes[0] + \" \" + choiceInnerBytes[1]);\n"
  "const choiceInnerDecoded = ${ts_choice_module}.Choice_1_0.deserialize(choiceInnerBytes);\n"
  "if (choiceInnerDecoded.value._tag !== 1 || !(\"inner\" in choiceInnerDecoded.value)) {\n"
  "  throw new Error(\"unexpected decoded choice-inner variant\");\n"
  "}\n"
  "console.log(choiceInnerDecoded.value._tag + \" \" + choiceInnerDecoded.value.inner.value + \" \" + choiceInnerDecoded.consumed);\n"
  "\n"
  "const choiceFlagIn: ${ts_choice_module}.Choice_1_0 = { _tag: 0, flag: true };\n"
  "const choiceFlagBytes = ${ts_choice_module}.Choice_1_0.serialize(choiceFlagIn);\n"
  "if (choiceFlagBytes.length !== 2) {\n"
  "  throw new Error(\"unexpected choice-flag serialized size \" + choiceFlagBytes.length);\n"
  "}\n"
  "console.log(choiceFlagBytes[0] + \" \" + choiceFlagBytes[1]);\n"
  "const choiceFlagDecoded = ${ts_choice_module}.Choice_1_0.deserialize(choiceFlagBytes);\n"
  "if (choiceFlagDecoded.value._tag !== 0 || !(\"flag\" in choiceFlagDecoded.value)) {\n"
  "  throw new Error(\"unexpected decoded choice-flag variant\");\n"
  "}\n"
  "console.log(choiceFlagDecoded.value._tag + \" \" + (choiceFlagDecoded.value.flag ? 1 : 0) + \" \" + choiceFlagDecoded.consumed);\n"
)

file(WRITE
  "${ts_out}/tsconfig-runtime-padding-alignment-smoke.json"
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
  COMMAND "${TSC_EXECUTABLE}" -p "${ts_out}/tsconfig-runtime-padding-alignment-smoke.json" --pretty false
  WORKING_DIRECTORY "${ts_out}"
  RESULT_VARIABLE tsc_result
  OUTPUT_VARIABLE tsc_stdout
  ERROR_VARIABLE tsc_stderr
)
if(NOT tsc_result EQUAL 0)
  message(STATUS "tsc stdout:\n${tsc_stdout}")
  message(STATUS "tsc stderr:\n${tsc_stderr}")
  message(FATAL_ERROR "failed to compile TypeScript runtime padding/alignment smoke")
endif()

file(WRITE "${ts_out}/js/package.json" "{\n  \"type\": \"commonjs\"\n}\n")

execute_process(
  COMMAND "${NODE_EXECUTABLE}" "${ts_out}/js/runtime_padding_alignment_smoke.js"
  RESULT_VARIABLE node_result
  OUTPUT_VARIABLE node_stdout
  ERROR_VARIABLE node_stderr
)
if(NOT node_result EQUAL 0)
  message(STATUS "node stdout:\n${node_stdout}")
  message(STATUS "node stderr:\n${node_stderr}")
  message(FATAL_ERROR "TypeScript runtime padding/alignment smoke execution failed")
endif()

string(STRIP "${node_stdout}" actual_output)
set(expected_output [=[1 170 128
1 170 1 3
1 170
1 170 2
0 1
0 1 2]=])
if(NOT actual_output STREQUAL expected_output)
  file(WRITE "${OUT_DIR}/expected-output.txt" "${expected_output}\n")
  file(WRITE "${OUT_DIR}/actual-output.txt" "${actual_output}\n")
  message(FATAL_ERROR
    "unexpected runtime padding/alignment smoke output. See ${OUT_DIR}/expected-output.txt and ${OUT_DIR}/actual-output.txt.")
endif()

message(STATUS "TypeScript runtime padding/alignment smoke passed")
