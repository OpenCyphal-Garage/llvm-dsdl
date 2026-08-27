cmake_minimum_required(VERSION 3.24)

foreach(var DSDLC UAVCAN_ROOT OUT_DIR TSC_EXECUTABLE NODE_EXECUTABLE)
  if(NOT DEFINED ${var} OR "${${var}}" STREQUAL "")
    message(FATAL_ERROR "Missing required variable: ${var}")
  endif()
endforeach()

if(NOT EXISTS "${DSDLC}")
  message(FATAL_ERROR "dsdlc executable not found: ${DSDLC}")
endif()

if(NOT EXISTS "${UAVCAN_ROOT}")
  message(FATAL_ERROR "uavcan root not found: ${UAVCAN_ROOT}")
endif()

if(NOT EXISTS "${TSC_EXECUTABLE}")
  message(FATAL_ERROR "tsc executable not found: ${TSC_EXECUTABLE}")
endif()

if(NOT EXISTS "${NODE_EXECUTABLE}")
  message(FATAL_ERROR "node executable not found: ${NODE_EXECUTABLE}")
endif()

file(REMOVE_RECURSE "${OUT_DIR}")
file(MAKE_DIRECTORY "${OUT_DIR}")

set(gen_dir "${OUT_DIR}/generated")
file(MAKE_DIRECTORY "${gen_dir}")

execute_process(
  COMMAND
    "${DSDLC}" --target-language ts --versioned-type-names
      "${UAVCAN_ROOT}"
      --outdir "${gen_dir}"
      --ts-module "uavcan_dsdl_generated_ts"
  RESULT_VARIABLE gen_result
  OUTPUT_VARIABLE gen_stdout
  ERROR_VARIABLE gen_stderr
)
if(NOT gen_result EQUAL 0)
  message(STATUS "dsdlc stdout:\n${gen_stdout}")
  message(STATUS "dsdlc stderr:\n${gen_stderr}")
  message(FATAL_ERROR "uavcan TypeScript generation failed")
endif()

file(READ "${gen_dir}/index.ts" ts_index)
foreach(required_alias
    "uavcan_node_port_subject_id_list_1_0"
    "uavcan_node_port_list_1_0"
    "uavcan_register_value_1_0"
    "uavcan_node_execute_command_1_3")
  if(NOT ts_index MATCHES "export \\* as ${required_alias} from ")
    message(FATAL_ERROR "Expected root TypeScript index.ts to export alias: ${required_alias}")
  endif()
endforeach()

file(WRITE
  "${gen_dir}/runtime_execution_smoke.ts"
  "import { uavcan_node_port_subject_id_list_1_0 as subjectIdList, uavcan_node_port_list_1_0 as portList, uavcan_register_value_1_0 as registerValue, uavcan_node_execute_command_1_3 as executeCommand } from \"./index\";\n"
  "\n"
  "const subjectTotal: subjectIdList.SubjectIDList_1_0 = { _tag: 2, total: {} as any };\n"
  "const subjectBytes = subjectIdList.serializeSubjectIDList_1_0(subjectTotal);\n"
  "console.log(Array.from(subjectBytes).join(\" \"));\n"
  "const subjectDecoded = subjectIdList.deserializeSubjectIDList_1_0(subjectBytes);\n"
  "console.log(subjectDecoded.value._tag + \" \" + subjectDecoded.consumed);\n"
  "\n"
  "const valueEmpty: registerValue.Value_1_0 = { _tag: 0, empty: {} as any };\n"
  "const valueBytes = registerValue.serializeValue_1_0(valueEmpty);\n"
  "console.log(Array.from(valueBytes).join(\" \"));\n"
  "const valueDecoded = registerValue.deserializeValue_1_0(valueBytes);\n"
  "console.log(valueDecoded.value._tag + \" \" + valueDecoded.consumed);\n"
  "\n"
  "const valueNatural8: registerValue.Value_1_0 = { _tag: 11, natural8: { value: [1, 2, 255] } };\n"
  "const valueNatural8Bytes = registerValue.serializeValue_1_0(valueNatural8);\n"
  "if (valueNatural8Bytes.length !== 259) {\n"
  "  throw new Error(\"unexpected Value.natural8 serialized length \" + valueNatural8Bytes.length);\n"
  "}\n"
  "for (let i = 6; i < valueNatural8Bytes.length; ++i) {\n"
  "  if (valueNatural8Bytes[i] !== 0) {\n"
  "    throw new Error(\"unexpected non-zero trailing byte at index \" + i + \" for Value.natural8\");\n"
  "  }\n"
  "}\n"
  "console.log(valueNatural8Bytes.length + \" \" + valueNatural8Bytes[0] + \" \" + valueNatural8Bytes[1] + \" \" + valueNatural8Bytes[2] + \" \" + valueNatural8Bytes[3] + \" \" + valueNatural8Bytes[4] + \" \" + valueNatural8Bytes[5]);\n"
  "const valueNatural8Decoded = registerValue.deserializeValue_1_0(valueNatural8Bytes);\n"
  "if (valueNatural8Decoded.value._tag !== 11 || !(\"natural8\" in valueNatural8Decoded.value)) {\n"
  "  throw new Error(\"unexpected decoded Value.natural8 variant\");\n"
  "}\n"
  "console.log(valueNatural8Decoded.value._tag + \" \" + valueNatural8Decoded.value.natural8.value.join(\",\") + \" \" + valueNatural8Decoded.consumed);\n"
  "\n"
  "const executeCommandRequest: executeCommand.ExecuteCommand_1_3_Request = {\n"
  "  command: executeCommand.EXECUTE_COMMAND_1_3_REQUEST_COMMAND_IDENTIFY,\n"
  "  parameter: [65, 66, 67],\n"
  "};\n"
  "const executeCommandRequestBytes = executeCommand.serializeExecuteCommand_1_3_Request(executeCommandRequest);\n"
  "console.log(Array.from(executeCommandRequestBytes).join(\" \"));\n"
  "const executeCommandRequestDecoded = executeCommand.deserializeExecuteCommand_1_3_Request(executeCommandRequestBytes);\n"
  "console.log(executeCommandRequestDecoded.value.command + \" \" + executeCommandRequestDecoded.value.parameter.join(\",\") + \" \" + executeCommandRequestDecoded.consumed);\n"
  "\n"
  "const executeCommandResponse: executeCommand.ExecuteCommand_1_3_Response = {\n"
  "  status: executeCommand.EXECUTE_COMMAND_1_3_RESPONSE_STATUS_BAD_PARAMETER,\n"
  "  output: [9, 8, 7, 6],\n"
  "};\n"
  "const executeCommandResponseBytes = executeCommand.serializeExecuteCommand_1_3_Response(executeCommandResponse);\n"
  "console.log(Array.from(executeCommandResponseBytes).join(\" \"));\n"
  "const executeCommandResponseDecoded = executeCommand.deserializeExecuteCommand_1_3_Response(executeCommandResponseBytes);\n"
  "console.log(executeCommandResponseDecoded.value.status + \" \" + executeCommandResponseDecoded.value.output.join(\",\") + \" \" + executeCommandResponseDecoded.consumed);\n"
  "\n"
  "const zeroMask = new Array<boolean>(512).fill(false);\n"
  "const listValue: portList.List_1_0 = {\n"
  "  publishers: { _tag: 2, total: {} as any },\n"
  "  subscribers: { _tag: 2, total: {} as any },\n"
  "  clients: { mask: [...zeroMask] },\n"
  "  servers: { mask: [...zeroMask] },\n"
  "};\n"
  "const listBytes = portList.serializeList_1_0(listValue);\n"
  "if (listBytes.length !== 146) {\n"
  "  throw new Error(\"unexpected List serialized length \" + listBytes.length);\n"
  "}\n"
  "for (let i = 14; i < 78; ++i) {\n"
  "  if (listBytes[i] !== 0) {\n"
  "    throw new Error(\"unexpected non-zero clients mask payload byte at index \" + i);\n"
  "  }\n"
  "}\n"
  "for (let i = 82; i < 146; ++i) {\n"
  "  if (listBytes[i] !== 0) {\n"
  "    throw new Error(\"unexpected non-zero servers mask payload byte at index \" + i);\n"
  "  }\n"
  "}\n"
  "console.log(listBytes.length + \" \" + listBytes[0] + \" \" + listBytes[1] + \" \" + listBytes[2] + \" \" + listBytes[3] + \" \" + listBytes[4] + \" \" + listBytes[5] + \" \" + listBytes[6] + \" \" + listBytes[7] + \" \" + listBytes[8] + \" \" + listBytes[9] + \" \" + listBytes[10] + \" \" + listBytes[11] + \" \" + listBytes[12] + \" \" + listBytes[13] + \" \" + listBytes[78] + \" \" + listBytes[79] + \" \" + listBytes[80] + \" \" + listBytes[81]);\n"
  "const listDecoded = portList.deserializeList_1_0(listBytes);\n"
  "if (listDecoded.value.publishers._tag !== 2 || listDecoded.value.subscribers._tag !== 2) {\n"
  "  throw new Error(\"unexpected decoded List union tags\");\n"
  "}\n"
  "if (listDecoded.value.clients.mask.length !== 512 || listDecoded.value.servers.mask.length !== 512) {\n"
  "  throw new Error(\"unexpected decoded List service mask lengths\");\n"
  "}\n"
  "if (listDecoded.value.clients.mask.some(Boolean) || listDecoded.value.servers.mask.some(Boolean)) {\n"
  "  throw new Error(\"unexpected decoded List service mask bit\");\n"
  "}\n"
  "console.log(listDecoded.value.publishers._tag + \" \" + listDecoded.value.subscribers._tag + \" \" + listDecoded.value.clients.mask.length + \" \" + listDecoded.value.servers.mask.length + \" \" + listDecoded.consumed);\n"
  "\n"
  "const invalidListBytes = new Uint8Array(listBytes);\n"
  "invalidListBytes[0] = 255;\n"
  "invalidListBytes[1] = 0;\n"
  "invalidListBytes[2] = 0;\n"
  "invalidListBytes[3] = 0;\n"
  "let invalidListRejected = false;\n"
  "try {\n"
  "  portList.deserializeList_1_0(invalidListBytes);\n"
  "} catch (_err) {\n"
  "  invalidListRejected = true;\n"
  "}\n"
  "if (!invalidListRejected) {\n"
  "  throw new Error(\"invalid List delimiter header unexpectedly accepted\");\n"
  "}\n"
  "console.log(\"list_invalid_rejected\");\n"
)

file(WRITE
  "${gen_dir}/tsconfig-runtime-execution-smoke.json"
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
  COMMAND "${TSC_EXECUTABLE}" -p "${gen_dir}/tsconfig-runtime-execution-smoke.json" --pretty false
  WORKING_DIRECTORY "${gen_dir}"
  RESULT_VARIABLE tsc_result
  OUTPUT_VARIABLE tsc_stdout
  ERROR_VARIABLE tsc_stderr
)
if(NOT tsc_result EQUAL 0)
  message(STATUS "tsc stdout:\n${tsc_stdout}")
  message(STATUS "tsc stderr:\n${tsc_stderr}")
  message(FATAL_ERROR "generated uavcan TypeScript runtime execution smoke compile failed")
endif()

file(WRITE "${gen_dir}/js/package.json" "{\n  \"type\": \"commonjs\"\n}\n")

execute_process(
  COMMAND "${NODE_EXECUTABLE}" "${gen_dir}/js/runtime_execution_smoke.js"
  RESULT_VARIABLE node_result
  OUTPUT_VARIABLE node_stdout
  ERROR_VARIABLE node_stderr
)
if(NOT node_result EQUAL 0)
  message(STATUS "node stdout:\n${node_stdout}")
  message(STATUS "node stderr:\n${node_stderr}")
  message(FATAL_ERROR "generated uavcan TypeScript runtime execution smoke failed")
endif()

string(STRIP "${node_stdout}" actual_output)
set(expected_output "2\n2 1\n0\n0 1\n259 11 3 0 1 2 255\n11 1,2,255 259\n249 255 3 65 66 67\n65529 65,66,67 6\n4 4 9 8 7 6\n4 9,8,7,6 6\n146 1 0 0 0 2 1 0 0 0 2 64 0 0 0 64 0 0 0\n2 2 512 512 146\nlist_invalid_rejected")
if(NOT actual_output STREQUAL expected_output)
  file(WRITE "${OUT_DIR}/expected-output.txt" "${expected_output}\n")
  file(WRITE "${OUT_DIR}/actual-output.txt" "${actual_output}\n")
  message(FATAL_ERROR
    "unexpected uavcan TypeScript runtime execution smoke output. See ${OUT_DIR}/expected-output.txt and ${OUT_DIR}/actual-output.txt.")
endif()

message(STATUS "uavcan TypeScript runtime execution smoke passed")
