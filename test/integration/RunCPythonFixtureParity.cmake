cmake_minimum_required(VERSION 3.24)

foreach(var DSDLC OUT_DIR SOURCE_ROOT C_COMPILER PYTHON_EXECUTABLE FAMILY)
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
if(NOT EXISTS "${PYTHON_EXECUTABLE}")
  message(FATAL_ERROR "python executable not found: ${PYTHON_EXECUTABLE}")
endif()

set(dsdlc_extra_args "")
if(DEFINED DSDLC_EXTRA_ARGS AND NOT "${DSDLC_EXTRA_ARGS}" STREQUAL "")
  separate_arguments(dsdlc_extra_args NATIVE_COMMAND "${DSDLC_EXTRA_ARGS}")
endif()

set(py_runtime_specialization_arg "")
if(DEFINED PY_RUNTIME_SPECIALIZATION AND NOT "${PY_RUNTIME_SPECIALIZATION}" STREQUAL "")
  if(NOT "${PY_RUNTIME_SPECIALIZATION}" STREQUAL "portable" AND
     NOT "${PY_RUNTIME_SPECIALIZATION}" STREQUAL "fast")
    message(FATAL_ERROR "Invalid PY_RUNTIME_SPECIALIZATION value: ${PY_RUNTIME_SPECIALIZATION}")
  endif()
  set(py_runtime_specialization_arg --py-runtime-specialization "${PY_RUNTIME_SPECIALIZATION}")
endif()

file(REMOVE_RECURSE "${OUT_DIR}")
file(MAKE_DIRECTORY "${OUT_DIR}")

set(fixture_root "${OUT_DIR}/demo")
set(c_out "${OUT_DIR}/c")
set(py_out "${OUT_DIR}/py")
set(work_dir "${OUT_DIR}/work")
file(MAKE_DIRECTORY "${fixture_root}")
file(MAKE_DIRECTORY "${c_out}")
file(MAKE_DIRECTORY "${py_out}")
file(MAKE_DIRECTORY "${work_dir}")

set(c_sources "")
set(c_harness_content "")
set(py_harness_template "")
set(family_message "")

if(FAMILY STREQUAL "runtime")
  file(WRITE
    "${fixture_root}/Widget.1.0.dsdl"
    "uint8 foo\n"
    "uint16 bar\n"
    "@sealed\n"
  )

  set(c_sources
    "demo/Widget_1_0.c"
  )
  set(c_harness_content
    [=[
#include <stdint.h>
#include <stdio.h>
#include "demo/Widget_1_0.h"

int main(void) {
  demo__Widget in_obj;
  in_obj.foo = 0x12U;
  in_obj.bar = 0x3456U;

  uint8_t out_bytes[3] = {0};
  size_t out_size = sizeof(out_bytes);
  if ((demo__Widget__serialize_(&in_obj, out_bytes, &out_size) != 0) || (out_size != 3U)) {
    return 2;
  }
  printf("%u %u %u\n", (unsigned) out_bytes[0], (unsigned) out_bytes[1], (unsigned) out_bytes[2]);

  demo__Widget out_obj = {0};
  size_t consumed = out_size;
  if ((demo__Widget__deserialize_(&out_obj, out_bytes, &consumed) != 0) || (consumed != 3U)) {
    return 3;
  }
  printf("%u %u %u\n", (unsigned) out_obj.foo, (unsigned) out_obj.bar, (unsigned) consumed);
  return 0;
}
]=]
  )
  set(py_harness_template
    [=[
import importlib

pkg = "@PY_PACKAGE@"
Widget_1_0 = importlib.import_module(f"{pkg}.demo.widget_1_0").Widget_1_0

in_obj = Widget_1_0(foo=0x12, bar=0x3456)
out_bytes = in_obj.serialize()
assert len(out_bytes) == 3
print(f"{out_bytes[0]} {out_bytes[1]} {out_bytes[2]}")

out_obj = Widget_1_0.deserialize(out_bytes)
print(f"{out_obj.foo} {out_obj.bar} {len(out_bytes)}")
]=]
  )
  set(family_message "runtime")
elseif(FAMILY STREQUAL "service")
  file(WRITE
    "${fixture_root}/Inner.1.0.dsdl"
    "uint8 x\n"
    "@sealed\n"
  )
  file(WRITE
    "${fixture_root}/DemoService.1.0.dsdl"
    "uint8 req_id\n"
    "uint16[<=2] req_vals\n"
    "@sealed\n"
    "---\n"
    "uint8 status\n"
    "demo.Inner.1.0 result\n"
    "@sealed\n"
  )

  set(c_sources
    "demo/Inner_1_0.c"
    "demo/DemoService_1_0.c"
  )
  set(c_harness_content
    [=[
#include <stdint.h>
#include <stdio.h>
#include "demo/Inner_1_0.h"
#include "demo/DemoService_1_0.h"

static void print_bytes(const char* label, const uint8_t* bytes, size_t size) {
  printf("%s %u", label, (unsigned) size);
  for (size_t i = 0; i < size; ++i) {
    printf(" %u", (unsigned) bytes[i]);
  }
  printf("\n");
}

int main(void) {
  demo__DemoService__Request req_obj = {0};
  req_obj.req_id = 7U;
  req_obj.req_vals.count = 2U;
  req_obj.req_vals.elements[0] = 0x1234U;
  req_obj.req_vals.elements[1] = 0xABCDU;

  uint8_t req_bytes[16] = {0};
  size_t req_size = sizeof(req_bytes);
  if (demo__DemoService__Request__serialize_(&req_obj, req_bytes, &req_size) != 0) {
    return 2;
  }
  print_bytes("req_bytes", req_bytes, req_size);

  demo__DemoService__Request req_out = {0};
  size_t req_consumed = req_size;
  if ((demo__DemoService__Request__deserialize_(&req_out, req_bytes, &req_consumed) != 0) ||
      (req_out.req_vals.count != 2U)) {
    return 3;
  }
  printf("req_decoded %u %u %u %u %u\n",
         (unsigned) req_out.req_id,
         (unsigned) req_out.req_vals.count,
         (unsigned) req_out.req_vals.elements[0],
         (unsigned) req_out.req_vals.elements[1],
         (unsigned) req_consumed);

  demo__DemoService__Response resp_obj = {0};
  resp_obj.status = 99U;
  resp_obj.result.x = 42U;
  uint8_t resp_bytes[16] = {0};
  size_t resp_size = sizeof(resp_bytes);
  if (demo__DemoService__Response__serialize_(&resp_obj, resp_bytes, &resp_size) != 0) {
    return 4;
  }
  print_bytes("resp_bytes", resp_bytes, resp_size);

  demo__DemoService__Response resp_out = {0};
  size_t resp_consumed = resp_size;
  if (demo__DemoService__Response__deserialize_(&resp_out, resp_bytes, &resp_consumed) != 0) {
    return 5;
  }
  printf("resp_decoded %u %u %u\n",
         (unsigned) resp_out.status,
         (unsigned) resp_out.result.x,
         (unsigned) resp_consumed);
  return 0;
}
]=]
  )
  set(py_harness_template
    [=[
import importlib

pkg = "@PY_PACKAGE@"
svc_mod = importlib.import_module(f"{pkg}.demo.demo_service_1_0")
Inner_1_0 = importlib.import_module(f"{pkg}.demo.inner_1_0").Inner_1_0
Req = svc_mod.DemoService_1_0_Request
Resp = svc_mod.DemoService_1_0_Response

req_obj = Req(req_id=7, req_vals=[0x1234, 0xABCD])
req_bytes = req_obj.serialize()
print("req_bytes " + str(len(req_bytes)) + "".join(f" {v}" for v in req_bytes))
req_out = Req.deserialize(req_bytes)
print(f"req_decoded {req_out.req_id} {len(req_out.req_vals)} {req_out.req_vals[0]} {req_out.req_vals[1]} {len(req_bytes)}")

resp_obj = Resp(status=99, result=Inner_1_0(x=42))
resp_bytes = resp_obj.serialize()
print("resp_bytes " + str(len(resp_bytes)) + "".join(f" {v}" for v in resp_bytes))
resp_out = Resp.deserialize(resp_bytes)
print(f"resp_decoded {resp_out.status} {resp_out.result.x} {len(resp_bytes)}")
]=]
  )
  set(family_message "service")
elseif(FAMILY STREQUAL "variable_array")
  file(WRITE
    "${fixture_root}/VarArray.1.0.dsdl"
    "uint3[<=5] values\n"
    "uint5 tail\n"
    "@sealed\n"
  )

  set(c_sources
    "demo/VarArray_1_0.c"
  )
  set(c_harness_content
    [=[
#include <stdint.h>
#include <stdio.h>
#include "demo/VarArray_1_0.h"

int main(void) {
  demo__VarArray in_obj = {0};
  in_obj.values.count = 3U;
  in_obj.values.elements[0] = 1U;
  in_obj.values.elements[1] = 6U;
  in_obj.values.elements[2] = 8U;
  in_obj.tail = 31U;

  uint8_t out_bytes[4] = {0};
  size_t out_size = sizeof(out_bytes);
  if ((demo__VarArray__serialize_(&in_obj, out_bytes, &out_size) != 0) || (out_size != 3U)) {
    return 2;
  }
  printf("%u %u %u\n", (unsigned) out_bytes[0], (unsigned) out_bytes[1], (unsigned) out_bytes[2]);

  demo__VarArray out_obj = {0};
  size_t consumed = out_size;
  if ((demo__VarArray__deserialize_(&out_obj, out_bytes, &consumed) != 0) ||
      (consumed != 3U) || (out_obj.values.count != 3U)) {
    return 3;
  }
  printf("%u %u %u %u %u %u\n",
         (unsigned) out_obj.values.count,
         (unsigned) out_obj.values.elements[0],
         (unsigned) out_obj.values.elements[1],
         (unsigned) out_obj.values.elements[2],
         (unsigned) out_obj.tail,
         (unsigned) consumed);
  return 0;
}
]=]
  )
  set(py_harness_template
    [=[
import importlib

pkg = "@PY_PACKAGE@"
VarArray_1_0 = importlib.import_module(f"{pkg}.demo.var_array_1_0").VarArray_1_0

in_obj = VarArray_1_0(values=[1, 6, 8], tail=31)
out_bytes = in_obj.serialize()
assert len(out_bytes) == 3
print(f"{out_bytes[0]} {out_bytes[1]} {out_bytes[2]}")

out_obj = VarArray_1_0.deserialize(out_bytes)
print(f"{len(out_obj.values)} {out_obj.values[0]} {out_obj.values[1]} {out_obj.values[2]} {out_obj.tail} {len(out_bytes)}")
]=]
  )
  set(family_message "variable-array")
elseif(FAMILY STREQUAL "fixed_array")
  file(WRITE
    "${fixture_root}/Inner.1.0.dsdl"
    "uint8 x\n"
    "@sealed\n"
  )
  file(WRITE
    "${fixture_root}/OuterArray.1.0.dsdl"
    "demo.Inner.1.0[2] items\n"
    "uint8 tail\n"
    "@sealed\n"
  )

  set(c_sources
    "demo/Inner_1_0.c"
    "demo/OuterArray_1_0.c"
  )
  set(c_harness_content
    [=[
#include <stdint.h>
#include <stdio.h>
#include "demo/Inner_1_0.h"
#include "demo/OuterArray_1_0.h"

int main(void) {
  demo__OuterArray in_obj = {0};
  in_obj.items[0].x = 17U;
  in_obj.items[1].x = 34U;
  in_obj.tail = 42U;

  uint8_t out_bytes[4] = {0};
  size_t out_size = sizeof(out_bytes);
  if ((demo__OuterArray__serialize_(&in_obj, out_bytes, &out_size) != 0) || (out_size != 3U)) {
    return 2;
  }
  printf("%u %u %u\n", (unsigned) out_bytes[0], (unsigned) out_bytes[1], (unsigned) out_bytes[2]);

  demo__OuterArray out_obj = {0};
  size_t consumed = out_size;
  if ((demo__OuterArray__deserialize_(&out_obj, out_bytes, &consumed) != 0) || (consumed != 3U)) {
    return 3;
  }
  printf("%u %u %u %u\n",
         (unsigned) out_obj.items[0].x,
         (unsigned) out_obj.items[1].x,
         (unsigned) out_obj.tail,
         (unsigned) consumed);
  return 0;
}
]=]
  )
  set(py_harness_template
    [=[
import importlib

pkg = "@PY_PACKAGE@"
Inner_1_0 = importlib.import_module(f"{pkg}.demo.inner_1_0").Inner_1_0
OuterArray_1_0 = importlib.import_module(f"{pkg}.demo.outer_array_1_0").OuterArray_1_0

in_obj = OuterArray_1_0(items=[Inner_1_0(x=17), Inner_1_0(x=34)], tail=42)
out_bytes = in_obj.serialize()
assert len(out_bytes) == 3
print(f"{out_bytes[0]} {out_bytes[1]} {out_bytes[2]}")

out_obj = OuterArray_1_0.deserialize(out_bytes)
print(f"{out_obj.items[0].x} {out_obj.items[1].x} {out_obj.tail} {len(out_bytes)}")
]=]
  )
  set(family_message "fixed-array")
elseif(FAMILY STREQUAL "bigint")
  file(WRITE
    "${fixture_root}/BigIntType.1.0.dsdl"
    "uint64 wide_u\n"
    "int64 wide_s\n"
    "@sealed\n"
  )

  set(c_sources
    "demo/BigIntType_1_0.c"
  )
  set(c_harness_content
    [=[
#include <stdint.h>
#include <stdio.h>
#include "demo/BigIntType_1_0.h"

int main(void) {
  demo__BigIntType in_obj = {0};
  in_obj.wide_u = 1152921504606846977ULL;
  in_obj.wide_s = -1152921504606846976LL;

  uint8_t out_bytes[16] = {0};
  size_t out_size = sizeof(out_bytes);
  if ((demo__BigIntType__serialize_(&in_obj, out_bytes, &out_size) != 0) || (out_size != 16U)) {
    return 2;
  }
  for (size_t i = 0; i < out_size; ++i) {
    printf("%u%s", (unsigned) out_bytes[i], (i + 1U == out_size) ? "" : " ");
  }
  printf("\n");

  demo__BigIntType out_obj = {0};
  size_t consumed = out_size;
  if ((demo__BigIntType__deserialize_(&out_obj, out_bytes, &consumed) != 0) || (consumed != 16U)) {
    return 3;
  }
  printf("%llu %lld %u\n",
         (unsigned long long) out_obj.wide_u,
         (long long) out_obj.wide_s,
         (unsigned) consumed);
  return 0;
}
]=]
  )
  set(py_harness_template
    [=[
import importlib

pkg = "@PY_PACKAGE@"
BigIntType_1_0 = importlib.import_module(f"{pkg}.demo.big_int_type_1_0").BigIntType_1_0

in_obj = BigIntType_1_0(wide_u=1152921504606846977, wide_s=-1152921504606846976)
out_bytes = in_obj.serialize()
assert len(out_bytes) == 16
print(" ".join(str(v) for v in out_bytes))

out_obj = BigIntType_1_0.deserialize(out_bytes)
print(f"{out_obj.wide_u} {out_obj.wide_s} {len(out_bytes)}")
]=]
  )
  set(family_message "bigint")
elseif(FAMILY STREQUAL "float")
  file(WRITE
    "${fixture_root}/FloatType.1.0.dsdl"
    "float32 f32\n"
    "float64 f64\n"
    "@sealed\n"
  )

  set(c_sources
    "demo/FloatType_1_0.c"
  )
  set(c_harness_content
    [=[
#include <stdint.h>
#include <stdio.h>
#include "demo/FloatType_1_0.h"

static void print_bytes(const uint8_t* bytes, size_t size) {
  for (size_t i = 0; i < size; ++i) {
    printf("%u%s", (unsigned) bytes[i], (i + 1U == size) ? "" : " ");
  }
  printf("\n");
}

int main(void) {
  demo__FloatType in_obj = {0};
  in_obj.f32 = 1.5F;
  in_obj.f64 = -2.25;

  uint8_t out_bytes[12] = {0};
  size_t out_size = sizeof(out_bytes);
  if ((demo__FloatType__serialize_(&in_obj, out_bytes, &out_size) != 0) || (out_size != 12U)) {
    return 2;
  }
  print_bytes(out_bytes, out_size);

  demo__FloatType out_obj = {0};
  size_t consumed = out_size;
  if ((demo__FloatType__deserialize_(&out_obj, out_bytes, &consumed) != 0) || (consumed != 12U)) {
    return 3;
  }
  uint8_t roundtrip_bytes[12] = {0};
  size_t roundtrip_size = sizeof(roundtrip_bytes);
  if ((demo__FloatType__serialize_(&out_obj, roundtrip_bytes, &roundtrip_size) != 0) ||
      (roundtrip_size != out_size)) {
    return 4;
  }
  print_bytes(roundtrip_bytes, roundtrip_size);
  printf("%u\n", (unsigned) consumed);
  return 0;
}
]=]
  )
  set(py_harness_template
    [=[
import importlib

pkg = "@PY_PACKAGE@"
FloatType_1_0 = importlib.import_module(f"{pkg}.demo.float_type_1_0").FloatType_1_0

in_obj = FloatType_1_0(f32=1.5, f64=-2.25)
out_bytes = in_obj.serialize()
assert len(out_bytes) == 12
print(" ".join(str(v) for v in out_bytes))

out_obj = FloatType_1_0.deserialize(out_bytes)
roundtrip = out_obj.serialize()
print(" ".join(str(v) for v in roundtrip))
print(str(len(out_bytes)))
]=]
  )
  set(family_message "float")
elseif(FAMILY STREQUAL "utf8")
  file(WRITE
    "${fixture_root}/Utf8Type.1.0.dsdl"
    "utf8 ch\n"
    "utf8[2] text\n"
    "@sealed\n"
  )

  set(c_sources
    "demo/Utf8Type_1_0.c"
  )
  set(c_harness_content
    [=[
#include <stdint.h>
#include <stdio.h>
#include "demo/Utf8Type_1_0.h"

int main(void) {
  demo__Utf8Type in_obj = {0};
  in_obj.ch = 65U;
  in_obj.text[0] = 66U;
  in_obj.text[1] = 67U;

  uint8_t out_bytes[3] = {0};
  size_t out_size = sizeof(out_bytes);
  if ((demo__Utf8Type__serialize_(&in_obj, out_bytes, &out_size) != 0) || (out_size != 3U)) {
    return 2;
  }
  printf("%u %u %u\n", (unsigned) out_bytes[0], (unsigned) out_bytes[1], (unsigned) out_bytes[2]);

  demo__Utf8Type out_obj = {0};
  size_t consumed = out_size;
  if ((demo__Utf8Type__deserialize_(&out_obj, out_bytes, &consumed) != 0) || (consumed != 3U)) {
    return 3;
  }
  printf("%u %u %u %u\n",
         (unsigned) out_obj.ch,
         (unsigned) out_obj.text[0],
         (unsigned) out_obj.text[1],
         (unsigned) consumed);
  return 0;
}
]=]
  )
  set(py_harness_template
    [=[
import importlib

pkg = "@PY_PACKAGE@"
Utf8Type_1_0 = importlib.import_module(f"{pkg}.demo.utf8type_1_0").Utf8Type_1_0

in_obj = Utf8Type_1_0(ch=65, text=[66, 67])
out_bytes = in_obj.serialize()
assert len(out_bytes) == 3
print(f"{out_bytes[0]} {out_bytes[1]} {out_bytes[2]}")

out_obj = Utf8Type_1_0.deserialize(out_bytes)
print(f"{out_obj.ch} {out_obj.text[0]} {out_obj.text[1]} {len(out_bytes)}")
]=]
  )
  set(family_message "utf8")
elseif(FAMILY STREQUAL "delimited")
  file(WRITE
    "${fixture_root}/Delimited.1.0.dsdl"
    "uint8 value\n"
    "@extent 64\n"
  )
  file(WRITE
    "${fixture_root}/UsesDelimited.1.0.dsdl"
    "demo.Delimited.1.0 nested\n"
    "@sealed\n"
  )

  set(c_sources
    "demo/Delimited_1_0.c"
    "demo/UsesDelimited_1_0.c"
  )
  set(c_harness_content
    [=[
#include <stdint.h>
#include <stdio.h>
#include "demo/Delimited_1_0.h"
#include "demo/UsesDelimited_1_0.h"

int main(void) {
  demo__UsesDelimited in_obj = {0};
  in_obj.nested.value = 171U;

  uint8_t out_bytes[16] = {0};
  size_t out_size = sizeof(out_bytes);
  if ((demo__UsesDelimited__serialize_(&in_obj, out_bytes, &out_size) != 0) || (out_size != 5U)) {
    return 2;
  }
  printf("%u %u %u %u %u\n",
         (unsigned) out_bytes[0],
         (unsigned) out_bytes[1],
         (unsigned) out_bytes[2],
         (unsigned) out_bytes[3],
         (unsigned) out_bytes[4]);

  demo__UsesDelimited out_obj = {0};
  size_t consumed = out_size;
  if ((demo__UsesDelimited__deserialize_(&out_obj, out_bytes, &consumed) != 0) ||
      (consumed != 5U)) {
    return 3;
  }
  printf("%u %u\n", (unsigned) out_obj.nested.value, (unsigned) consumed);
  return 0;
}
]=]
  )
  set(py_harness_template
    [=[
import importlib

pkg = "@PY_PACKAGE@"
Delimited_1_0 = importlib.import_module(f"{pkg}.demo.delimited_1_0").Delimited_1_0
UsesDelimited_1_0 = importlib.import_module(f"{pkg}.demo.uses_delimited_1_0").UsesDelimited_1_0

in_obj = UsesDelimited_1_0(nested=Delimited_1_0(value=171))
out_bytes = in_obj.serialize()
assert len(out_bytes) == 5
print(f"{out_bytes[0]} {out_bytes[1]} {out_bytes[2]} {out_bytes[3]} {out_bytes[4]}")

out_obj = UsesDelimited_1_0.deserialize(out_bytes)
print(f"{out_obj.nested.value} {len(out_bytes)}")
]=]
  )
  set(family_message "delimited")
elseif(FAMILY STREQUAL "union")
  file(WRITE
    "${fixture_root}/UnionTag.1.0.dsdl"
    "@union\n"
    "uint8 first\n"
    "uint16 second\n"
    "@sealed\n"
  )

  set(c_sources
    "demo/UnionTag_1_0.c"
  )
  set(c_harness_content
    [=[
#include <stdint.h>
#include <stdio.h>
#include "demo/UnionTag_1_0.h"

int main(void) {
  demo__UnionTag in_obj = {0};
  in_obj._tag_ = 1U;
  in_obj.second = 0x3456U;

  uint8_t out_bytes[4] = {0};
  size_t out_size = sizeof(out_bytes);
  if ((demo__UnionTag__serialize_(&in_obj, out_bytes, &out_size) != 0) || (out_size != 3U)) {
    return 2;
  }
  printf("%u %u %u\n", (unsigned) out_bytes[0], (unsigned) out_bytes[1], (unsigned) out_bytes[2]);

  demo__UnionTag out_obj = {0};
  size_t consumed = out_size;
  if ((demo__UnionTag__deserialize_(&out_obj, out_bytes, &consumed) != 0) ||
      (consumed != 3U) || (out_obj._tag_ != 1U)) {
    return 3;
  }
  printf("%u %u %u\n", (unsigned) out_obj._tag_, (unsigned) out_obj.second, (unsigned) consumed);
  return 0;
}
]=]
  )
  set(py_harness_template
    [=[
import importlib

pkg = "@PY_PACKAGE@"
UnionTag_1_0 = importlib.import_module(f"{pkg}.demo.union_tag_1_0").UnionTag_1_0

in_obj = UnionTag_1_0(_tag=1, second=0x3456)
out_bytes = in_obj.serialize()
assert len(out_bytes) == 3
print(f"{out_bytes[0]} {out_bytes[1]} {out_bytes[2]}")

out_obj = UnionTag_1_0.deserialize(out_bytes)
print(f"{out_obj._tag} {out_obj.second} {len(out_bytes)}")
]=]
  )
  set(family_message "union")
elseif(FAMILY STREQUAL "composite")
  file(WRITE
    "${fixture_root}/Inner.1.0.dsdl"
    "uint8 a\n"
    "uint3 b\n"
    "@sealed\n"
  )
  file(WRITE
    "${fixture_root}/Outer.1.0.dsdl"
    "demo.Inner.1.0 inner\n"
    "uint5 tail\n"
    "@sealed\n"
  )

  set(c_sources
    "demo/Inner_1_0.c"
    "demo/Outer_1_0.c"
  )
  set(c_harness_content
    [=[
#include <stdint.h>
#include <stdio.h>
#include "demo/Inner_1_0.h"
#include "demo/Outer_1_0.h"

int main(void) {
  demo__Outer in_obj = {0};
  in_obj.inner.a = 18U;
  in_obj.inner.b = 6U;
  in_obj.tail = 31U;

  uint8_t out_bytes[4] = {0};
  size_t out_size = sizeof(out_bytes);
  if ((demo__Outer__serialize_(&in_obj, out_bytes, &out_size) != 0) || (out_size != 3U)) {
    return 2;
  }
  printf("%u %u %u\n", (unsigned) out_bytes[0], (unsigned) out_bytes[1], (unsigned) out_bytes[2]);

  demo__Outer out_obj = {0};
  size_t consumed = out_size;
  if ((demo__Outer__deserialize_(&out_obj, out_bytes, &consumed) != 0) || (consumed != 3U)) {
    return 3;
  }
  printf("%u %u %u %u\n",
         (unsigned) out_obj.inner.a,
         (unsigned) out_obj.inner.b,
         (unsigned) out_obj.tail,
         (unsigned) consumed);
  return 0;
}
]=]
  )
  set(py_harness_template
    [=[
import importlib

pkg = "@PY_PACKAGE@"
Inner_1_0 = importlib.import_module(f"{pkg}.demo.inner_1_0").Inner_1_0
Outer_1_0 = importlib.import_module(f"{pkg}.demo.outer_1_0").Outer_1_0

in_obj = Outer_1_0(inner=Inner_1_0(a=18, b=6), tail=31)
out_bytes = in_obj.serialize()
assert len(out_bytes) == 3
print(f"{out_bytes[0]} {out_bytes[1]} {out_bytes[2]}")

out_obj = Outer_1_0.deserialize(out_bytes)
print(f"{out_obj.inner.a} {out_obj.inner.b} {out_obj.tail} {len(out_bytes)}")
]=]
  )
  set(family_message "composite")
elseif(FAMILY STREQUAL "truncated_decode")
  file(WRITE
    "${fixture_root}/Scalar.1.0.dsdl"
    "uint16 value\n"
    "@sealed\n"
  )
  file(WRITE
    "${fixture_root}/Vector.1.0.dsdl"
    "uint8[<=5] values\n"
    "@sealed\n"
  )

  set(c_sources
    "demo/Scalar_1_0.c"
    "demo/Vector_1_0.c"
  )
  set(c_harness_content
    [=[
#include <stdint.h>
#include <stdio.h>
#include "demo/Scalar_1_0.h"
#include "demo/Vector_1_0.h"

int main(void) {
  demo__Scalar scalar_in = {0};
  scalar_in.value = 0x3456U;
  uint8_t scalar_full[4] = {0};
  size_t scalar_full_size = sizeof(scalar_full);
  if ((demo__Scalar__serialize_(&scalar_in, scalar_full, &scalar_full_size) != 0) ||
      (scalar_full_size != 2U)) {
    return 2;
  }
  demo__Scalar scalar_out = {0};
  size_t scalar_short_size = scalar_full_size - 1U;
  if (demo__Scalar__deserialize_(&scalar_out, scalar_full, &scalar_short_size) != 0) {
    return 3;
  }
  if ((scalar_out.value != 0x56U) || (scalar_short_size != 1U)) {
    return 4;
  }
  printf("scalar_truncated_zero_extended\n");

  demo__Vector vector_in = {0};
  vector_in.values.count = 5U;
  vector_in.values.elements[0] = 1U;
  vector_in.values.elements[1] = 2U;
  vector_in.values.elements[2] = 3U;
  vector_in.values.elements[3] = 4U;
  vector_in.values.elements[4] = 229U;
  uint8_t vector_full[16] = {0};
  size_t vector_full_size = sizeof(vector_full);
  if (demo__Vector__serialize_(&vector_in, vector_full, &vector_full_size) != 0) {
    return 5;
  }
  demo__Vector vector_out = {0};
  size_t vector_short_size = vector_full_size - 1U;
  if (demo__Vector__deserialize_(&vector_out, vector_full, &vector_short_size) != 0) {
    return 6;
  }
  if ((vector_out.values.count != 5U) || (vector_out.values.elements[4] != 0U)) {
    return 7;
  }
  printf("vector_truncated_zero_extended\n");

  demo__Vector vector_invalid = {0};
  uint8_t vector_invalid_bytes[1] = {0x07U};
  size_t vector_invalid_size = 1U;
  if (demo__Vector__deserialize_(&vector_invalid, vector_invalid_bytes, &vector_invalid_size) == 0) {
    return 8;
  }
  printf("vector_invalid_length_rejected\n");
  return 0;
}
]=]
  )
  set(py_harness_template
    [=[
import importlib

pkg = "@PY_PACKAGE@"
Scalar_1_0 = importlib.import_module(f"{pkg}.demo.scalar_1_0").Scalar_1_0
Vector_1_0 = importlib.import_module(f"{pkg}.demo.vector_1_0").Vector_1_0

scalar_in = Scalar_1_0(value=0x3456)
scalar_full = scalar_in.serialize()
scalar_short = scalar_full[:1]
scalar_out = Scalar_1_0.deserialize(scalar_short)
assert scalar_out.value == 0x56
print("scalar_truncated_zero_extended")

vector_in = Vector_1_0(values=[1, 2, 3, 4, 229])
vector_full = vector_in.serialize()
vector_short = vector_full[:-1]
vector_out = Vector_1_0.deserialize(vector_short)
assert len(vector_out.values) == 5
assert vector_out.values[-1] == 0
print("vector_truncated_zero_extended")

failed = False
try:
  Vector_1_0.deserialize(bytes([0x07]))
except Exception:
  failed = True
assert failed
print("vector_invalid_length_rejected")
]=]
  )
  set(family_message "truncated-decode")
elseif(FAMILY STREQUAL "padding_alignment")
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

  set(c_sources
    "demo/Inner_1_0.c"
    "demo/Outer_1_0.c"
    "demo/Choice_1_0.c"
  )
  set(c_harness_content
    [=[
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "demo/Inner_1_0.h"
#include "demo/Outer_1_0.h"
#include "demo/Choice_1_0.h"

int main(void) {
  demo__Outer outer_in = {0};
  outer_in.head = 1U;
  outer_in.inner.value = 170U;
  outer_in.tail = 1U;

  uint8_t outer_bytes[8] = {0};
  size_t outer_size = sizeof(outer_bytes);
  if ((demo__Outer__serialize_(&outer_in, outer_bytes, &outer_size) != 0) || (outer_size != 3U)) {
    return 2;
  }
  printf("%u %u %u\n", (unsigned) outer_bytes[0], (unsigned) outer_bytes[1], (unsigned) outer_bytes[2]);

  demo__Outer outer_out = {0};
  size_t outer_consumed = outer_size;
  if ((demo__Outer__deserialize_(&outer_out, outer_bytes, &outer_consumed) != 0) || (outer_consumed != 3U)) {
    return 3;
  }
  printf("%u %u %u %u\n",
         (unsigned) outer_out.head,
         (unsigned) outer_out.inner.value,
         (unsigned) outer_out.tail,
         (unsigned) outer_consumed);

  demo__Choice choice_inner_in = {0};
  choice_inner_in._tag_ = 1U;
  choice_inner_in.inner.value = 170U;
  uint8_t choice_inner_bytes[8] = {0};
  size_t choice_inner_size = sizeof(choice_inner_bytes);
  if ((demo__Choice__serialize_(&choice_inner_in, choice_inner_bytes, &choice_inner_size) != 0) ||
      (choice_inner_size != 2U)) {
    return 4;
  }
  printf("%u %u\n", (unsigned) choice_inner_bytes[0], (unsigned) choice_inner_bytes[1]);

  demo__Choice choice_inner_out = {0};
  size_t choice_inner_consumed = choice_inner_size;
  if ((demo__Choice__deserialize_(&choice_inner_out, choice_inner_bytes, &choice_inner_consumed) != 0) ||
      (choice_inner_consumed != 2U) || (choice_inner_out._tag_ != 1U)) {
    return 5;
  }
  printf("%u %u %u\n",
         (unsigned) choice_inner_out._tag_,
         (unsigned) choice_inner_out.inner.value,
         (unsigned) choice_inner_consumed);

  demo__Choice choice_flag_in = {0};
  choice_flag_in._tag_ = 0U;
  choice_flag_in.flag = true;
  uint8_t choice_flag_bytes[8] = {0};
  size_t choice_flag_size = sizeof(choice_flag_bytes);
  if ((demo__Choice__serialize_(&choice_flag_in, choice_flag_bytes, &choice_flag_size) != 0) ||
      (choice_flag_size != 2U)) {
    return 6;
  }
  printf("%u %u\n", (unsigned) choice_flag_bytes[0], (unsigned) choice_flag_bytes[1]);

  demo__Choice choice_flag_out = {0};
  size_t choice_flag_consumed = choice_flag_size;
  if ((demo__Choice__deserialize_(&choice_flag_out, choice_flag_bytes, &choice_flag_consumed) != 0) ||
      (choice_flag_consumed != 2U) || (choice_flag_out._tag_ != 0U)) {
    return 7;
  }
  printf("%u %u %u\n",
         (unsigned) choice_flag_out._tag_,
         (unsigned) choice_flag_out.flag,
         (unsigned) choice_flag_consumed);
  return 0;
}
]=]
  )
  set(py_harness_template
    [=[
import importlib

pkg = "@PY_PACKAGE@"
Inner_1_0 = importlib.import_module(f"{pkg}.demo.inner_1_0").Inner_1_0
Outer_1_0 = importlib.import_module(f"{pkg}.demo.outer_1_0").Outer_1_0
Choice_1_0 = importlib.import_module(f"{pkg}.demo.choice_1_0").Choice_1_0

outer_in = Outer_1_0(head=1, inner=Inner_1_0(value=170), tail=1)
outer_bytes = outer_in.serialize()
assert len(outer_bytes) == 3
print(f"{outer_bytes[0]} {outer_bytes[1]} {outer_bytes[2]}")
outer_out = Outer_1_0.deserialize(outer_bytes)
print(f"{outer_out.head} {outer_out.inner.value} {outer_out.tail} {len(outer_bytes)}")

choice_inner_in = Choice_1_0(_tag=1, inner=Inner_1_0(value=170))
choice_inner_bytes = choice_inner_in.serialize()
assert len(choice_inner_bytes) == 2
print(f"{choice_inner_bytes[0]} {choice_inner_bytes[1]}")
choice_inner_out = Choice_1_0.deserialize(choice_inner_bytes)
print(f"{choice_inner_out._tag} {choice_inner_out.inner.value} {len(choice_inner_bytes)}")

choice_flag_in = Choice_1_0(_tag=0, flag=True)
choice_flag_bytes = choice_flag_in.serialize()
assert len(choice_flag_bytes) == 2
print(f"{choice_flag_bytes[0]} {choice_flag_bytes[1]}")
choice_flag_out = Choice_1_0.deserialize(choice_flag_bytes)
print(f"{choice_flag_out._tag} {1 if choice_flag_out.flag else 0} {len(choice_flag_bytes)}")
]=]
  )
  set(family_message "padding-alignment")
else()
  message(FATAL_ERROR "Unsupported FAMILY for C/Python parity: ${FAMILY}")
endif()

set(py_package "llvmdsdl_py_${FAMILY}")
set(PY_PACKAGE "${py_package}")

execute_process(
  COMMAND
    "${DSDLC}" --target-language c
      "${fixture_root}"
      ${dsdlc_extra_args}
      --outdir "${c_out}"
  RESULT_VARIABLE c_gen_result
  OUTPUT_VARIABLE c_gen_stdout
  ERROR_VARIABLE c_gen_stderr
)
if(NOT c_gen_result EQUAL 0)
  message(STATUS "dsdlc c stdout:\n${c_gen_stdout}")
  message(STATUS "dsdlc c stderr:\n${c_gen_stderr}")
  message(FATAL_ERROR "fixture C generation failed for family=${FAMILY}")
endif()

execute_process(
  COMMAND
    "${DSDLC}" --target-language python
      "${fixture_root}"
      ${dsdlc_extra_args}
      --outdir "${py_out}"
      --py-package "${py_package}"
      ${py_runtime_specialization_arg}
  RESULT_VARIABLE py_gen_result
  OUTPUT_VARIABLE py_gen_stdout
  ERROR_VARIABLE py_gen_stderr
)
if(NOT py_gen_result EQUAL 0)
  message(STATUS "dsdlc python stdout:\n${py_gen_stdout}")
  message(STATUS "dsdlc python stderr:\n${py_gen_stderr}")
  message(FATAL_ERROR "fixture Python generation failed for family=${FAMILY}")
endif()

set(c_source_paths "")
foreach(src IN LISTS c_sources)
  list(APPEND c_source_paths "${c_out}/${src}")
endforeach()

set(c_harness_src "${work_dir}/c_python_parity_harness.c")
file(WRITE "${c_harness_src}" "${c_harness_content}")

set(c_harness_bin "${work_dir}/c_python_parity_harness")
execute_process(
  COMMAND
    "${C_COMPILER}"
      -std=c11
      -Wall
      -Wextra
      -Werror
      -I "${c_out}"
      "${c_harness_src}"
      ${c_source_paths}
      -o "${c_harness_bin}"
  RESULT_VARIABLE c_cc_result
  OUTPUT_VARIABLE c_cc_stdout
  ERROR_VARIABLE c_cc_stderr
)
if(NOT c_cc_result EQUAL 0)
  message(STATUS "C compile stdout:\n${c_cc_stdout}")
  message(STATUS "C compile stderr:\n${c_cc_stderr}")
  message(FATAL_ERROR "failed to compile fixture C/Python parity harness for family=${FAMILY}")
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
  message(FATAL_ERROR "fixture C harness failed for family=${FAMILY}")
endif()

string(CONFIGURE "${py_harness_template}" py_harness_content @ONLY)
set(py_harness_script "${work_dir}/python_parity_harness.py")
file(WRITE "${py_harness_script}" "${py_harness_content}")

execute_process(
  COMMAND
    "${CMAKE_COMMAND}" -E env
      "PYTHONPATH=${py_out}"
      "LLVMDSDL_PY_RUNTIME_MODE=pure"
      "${PYTHON_EXECUTABLE}" "${py_harness_script}"
  RESULT_VARIABLE py_run_result
  OUTPUT_VARIABLE py_run_stdout
  ERROR_VARIABLE py_run_stderr
)
if(NOT py_run_result EQUAL 0)
  message(STATUS "Python harness stdout:\n${py_run_stdout}")
  message(STATUS "Python harness stderr:\n${py_run_stderr}")
  message(FATAL_ERROR "fixture Python harness failed for family=${FAMILY}")
endif()

string(STRIP "${c_run_stdout}" c_output)
string(STRIP "${py_run_stdout}" py_output)
if(NOT c_output STREQUAL py_output)
  file(WRITE "${OUT_DIR}/c-output.txt" "${c_output}\n")
  file(WRITE "${OUT_DIR}/python-output.txt" "${py_output}\n")
  message(FATAL_ERROR
    "C vs Python ${family_message} parity mismatch. See ${OUT_DIR}/c-output.txt and ${OUT_DIR}/python-output.txt.")
endif()

message(STATUS "fixture C<->Python ${family_message} parity passed")
