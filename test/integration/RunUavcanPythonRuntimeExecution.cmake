cmake_minimum_required(VERSION 3.24)

foreach(var DSDLC UAVCAN_ROOT OUT_DIR PYTHON_EXECUTABLE)
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
if(NOT EXISTS "${PYTHON_EXECUTABLE}")
  message(FATAL_ERROR "python executable not found: ${PYTHON_EXECUTABLE}")
endif()

if(NOT DEFINED PY_PACKAGE OR "${PY_PACKAGE}" STREQUAL "")
  set(PY_PACKAGE "uavcan_dsdl_generated_py")
endif()
if(NOT DEFINED PY_RUNTIME_SPECIALIZATION OR "${PY_RUNTIME_SPECIALIZATION}" STREQUAL "")
  set(PY_RUNTIME_SPECIALIZATION "portable")
endif()
if(NOT "${PY_RUNTIME_SPECIALIZATION}" STREQUAL "portable" AND
   NOT "${PY_RUNTIME_SPECIALIZATION}" STREQUAL "fast")
  message(FATAL_ERROR "Invalid PY_RUNTIME_SPECIALIZATION value: ${PY_RUNTIME_SPECIALIZATION}")
endif()

set(dsdlc_args
  --target-language python
  # The harness below is written against versioned type names.
  --versioned-type-names
  "${UAVCAN_ROOT}"
  --outdir "${OUT_DIR}"
  --py-package "${PY_PACKAGE}"
  --py-runtime-specialization "${PY_RUNTIME_SPECIALIZATION}"
)
if(DEFINED DSDLC_EXTRA_ARGS AND NOT "${DSDLC_EXTRA_ARGS}" STREQUAL "")
  separate_arguments(extra_args NATIVE_COMMAND "${DSDLC_EXTRA_ARGS}")
  list(INSERT dsdlc_args 2 ${extra_args})
endif()

file(REMOVE_RECURSE "${OUT_DIR}")
file(MAKE_DIRECTORY "${OUT_DIR}")

execute_process(
  COMMAND "${DSDLC}" ${dsdlc_args}
  RESULT_VARIABLE gen_result
  OUTPUT_VARIABLE gen_stdout
  ERROR_VARIABLE gen_stderr
)
if(NOT gen_result EQUAL 0)
  message(STATUS "dsdlc stdout:\n${gen_stdout}")
  message(STATUS "dsdlc stderr:\n${gen_stderr}")
  message(FATAL_ERROR "uavcan Python runtime execution generation failed")
endif()

set(runtime_script "${OUT_DIR}/uavcan_python_runtime_execution.py")
file(WRITE
  "${runtime_script}"
  [=[
from __future__ import annotations

import importlib

PKG = "@PY_PACKAGE@"
RUNTIME_SPECIALIZATION = "@PY_RUNTIME_SPECIALIZATION@"

HeartbeatMod = importlib.import_module(f"{PKG}.uavcan.node.heartbeat_1_0")
HealthMod = importlib.import_module(f"{PKG}.uavcan.node.health_1_0")
ModeMod = importlib.import_module(f"{PKG}.uavcan.node.mode_1_0")
ExecuteCommandMod = importlib.import_module(f"{PKG}.uavcan.node.execute_command_1_3")
ValueMod = importlib.import_module(f"{PKG}.uavcan.register.value_1_0")
EmptyMod = importlib.import_module(f"{PKG}.uavcan.primitive.empty_1_0")
Natural32Mod = importlib.import_module(f"{PKG}.uavcan.primitive.array.natural32_1_0")
AccelVector3Mod = importlib.import_module(f"{PKG}.uavcan.si.unit.acceleration.vector3_1_0")


def roundtrip(obj: object, cls: type) -> object:
    payload = obj.serialize()
    decoded = cls.deserialize(payload)
    assert decoded.serialize() == payload
    return decoded


# Message roundtrip.
heartbeat = HeartbeatMod.Heartbeat_1_0(
    uptime=123456,
    health=HealthMod.Health_1_0(value=HealthMod.HEALTH_1_0_CAUTION),
    mode=ModeMod.Mode_1_0(value=ModeMod.MODE_1_0_INITIALIZATION),
    vendor_specific_status_code=77,
)
heartbeat_rt = roundtrip(heartbeat, HeartbeatMod.Heartbeat_1_0)
assert heartbeat_rt.uptime == heartbeat.uptime
assert heartbeat_rt.health.value == heartbeat.health.value
assert heartbeat_rt.mode.value == heartbeat.mode.value
assert heartbeat_rt.vendor_specific_status_code == heartbeat.vendor_specific_status_code
print("uavcan-python-runtime message-roundtrip-ok")

# Service request/response roundtrip.
execute_req = ExecuteCommandMod.ExecuteCommand_1_3_Request(
    command=ExecuteCommandMod.EXECUTE_COMMAND_1_3_REQUEST_COMMAND_IDENTIFY,
    parameter=[ord(c) for c in "blink"],
)
execute_req_rt = roundtrip(execute_req, ExecuteCommandMod.ExecuteCommand_1_3_Request)
assert execute_req_rt.command == execute_req.command
assert execute_req_rt.parameter == execute_req.parameter
print("uavcan-python-runtime service-request-roundtrip-ok")

execute_resp = ExecuteCommandMod.ExecuteCommand_1_3_Response(
    status=ExecuteCommandMod.EXECUTE_COMMAND_1_3_RESPONSE_STATUS_SUCCESS,
    output=[1, 2, 3, 4],
)
execute_resp_rt = roundtrip(execute_resp, ExecuteCommandMod.ExecuteCommand_1_3_Response)
assert execute_resp_rt.status == execute_resp.status
assert execute_resp_rt.output == execute_resp.output
print("uavcan-python-runtime service-response-roundtrip-ok")

# Union-heavy roundtrip via register.Value.
register_value = ValueMod.Value_1_0(_tag=0, empty=EmptyMod.Empty_1_0())
register_value_rt = roundtrip(register_value, ValueMod.Value_1_0)
assert register_value_rt._tag == 0
assert register_value_rt.empty is not None
print("uavcan-python-runtime union-roundtrip-ok")

# Composite roundtrip.
accel = AccelVector3Mod.Vector3_1_0(meter_per_second_per_second=[1.0, -2.5, 9.81])
accel_rt = roundtrip(accel, AccelVector3Mod.Vector3_1_0)
assert len(accel_rt.meter_per_second_per_second) == 3
assert abs(accel_rt.meter_per_second_per_second[0] - 1.0) < 1e-6
assert abs(accel_rt.meter_per_second_per_second[1] + 2.5) < 1e-6
assert abs(accel_rt.meter_per_second_per_second[2] - 9.81) < 1e-5
print("uavcan-python-runtime composite-roundtrip-ok")

# Truncated decode malformed-input contract:
# - portable pure runtime: zero-extends missing bits
# - fast pure runtime: rejects byte-aligned out-of-range extract with ValueError
if RUNTIME_SPECIALIZATION == "fast":
    truncated_failed = False
    try:
        HeartbeatMod.Heartbeat_1_0.deserialize(bytes([0x34, 0x12]))
    except ValueError:
        truncated_failed = True
    assert truncated_failed
    print("uavcan-python-runtime truncated-buffer-rejected-ok")
else:
    truncated = HeartbeatMod.Heartbeat_1_0.deserialize(bytes([0x34, 0x12]))
    assert truncated.uptime == 0x1234
    assert truncated.health.value == 0
    assert truncated.mode.value == 0
    assert truncated.vendor_specific_status_code == 0
    print("uavcan-python-runtime truncated-buffer-zero-extend-ok")

# Negative path: invalid union tags should fail.
invalid_union_failed = False
try:
    ValueMod.Value_1_0.deserialize(bytes([0xFF]))
except ValueError:
    invalid_union_failed = True
assert invalid_union_failed
print("uavcan-python-runtime invalid-union-tag-ok")

# Negative path: array length violations should fail clearly.
oversized_req_failed = False
try:
    ExecuteCommandMod.ExecuteCommand_1_3_Request(command=1, parameter=[0] * 256).serialize()
except ValueError:
    oversized_req_failed = True
assert oversized_req_failed

oversized_resp_failed = False
try:
    ExecuteCommandMod.ExecuteCommand_1_3_Response(status=0, output=list(range(47))).serialize()
except ValueError:
    oversized_resp_failed = True
assert oversized_resp_failed

decode_len_failed = False
try:
    Natural32Mod.Natural32_1_0.deserialize(bytes([65]))
except ValueError:
    decode_len_failed = True
assert decode_len_failed
print("uavcan-python-runtime array-length-violations-ok")

print("uavcan-python-runtime-execution-ok")
]=]
)

file(READ "${runtime_script}" runtime_script_content)
string(REPLACE "@PY_PACKAGE@" "${PY_PACKAGE}" runtime_script_content "${runtime_script_content}")
string(REPLACE "@PY_RUNTIME_SPECIALIZATION@" "${PY_RUNTIME_SPECIALIZATION}" runtime_script_content "${runtime_script_content}")
file(WRITE "${runtime_script}" "${runtime_script_content}")

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
    "PYTHONPATH=${OUT_DIR}"
    "LLVMDSDL_PY_RUNTIME_MODE=pure"
    "${PYTHON_EXECUTABLE}" "${runtime_script}"
  RESULT_VARIABLE runtime_result
  OUTPUT_VARIABLE runtime_stdout
  ERROR_VARIABLE runtime_stderr
)
if(NOT runtime_result EQUAL 0)
  message(STATUS "uavcan Python runtime execution stdout:\n${runtime_stdout}")
  message(STATUS "uavcan Python runtime execution stderr:\n${runtime_stderr}")
  message(FATAL_ERROR "uavcan Python runtime execution test failed")
endif()

message(STATUS "uavcan Python runtime execution passed")
