// An accessor against the body it stands in for: a getter answers what deserialise puts in the
// field, on a full buffer and on a short one, and a setter writes what deserialise reads back.
// Values are compared with Object.is, so a NaN meets itself; an integer round trip is also exact.
import { makeVersion, deserializeVersionFrom, getVersionMajor, setVersionMajor, getVersionMinor, setVersionMinor } from "./uavcan/node/version_1_0";
import { makeInteger16, deserializeInteger16From, getInteger16Value, setInteger16Value } from "./uavcan/primitive/scalar/integer16_1_0";
import { makeNatural64, deserializeNatural64From, getNatural64Value, setNatural64Value } from "./uavcan/primitive/scalar/natural64_1_0";
import { makeReal16, deserializeReal16From, getReal16Value, setReal16Value } from "./uavcan/primitive/scalar/real16_1_0";
import { makeReal64, deserializeReal64From, getReal64Value, setReal64Value } from "./uavcan/primitive/scalar/real64_1_0";
import { makeScalar, deserializeScalarFrom, getScalarKelvin, setScalarKelvin } from "./uavcan/si/unit/temperature/scalar_1_0";
import { makeError, deserializeErrorFrom, getErrorValue, setErrorValue } from "./uavcan/file/error_1_0";

let rng = 0x9e3779b9;
function fill(buffer: Uint8Array): void {
  for (let i = 0; i < buffer.length; i++) {
    rng ^= rng << 13;
    rng ^= rng >>> 17;
    rng ^= rng << 5;
    rng >>>= 0;
    buffer[i] = rng & 0xff;
  }
}

function checkField<T, V>(
  size: number,
  make: () => T,
  deser: (o: T, b: Uint8Array) => number,
  get: (b: Uint8Array) => V,
  set: (b: Uint8Array, v: V) => number,
  field: (o: T) => V,
  exact: boolean,
): boolean {
  const wire = new Uint8Array(size);
  fill(wire);
  let ok = true;
  const obj = make();
  ok = ok && deser(obj, wire) >= 0 && Object.is(get(wire), field(obj));
  const short = make();
  ok = ok && deser(short, wire.subarray(0, size / 2)) >= 0 && Object.is(get(wire.subarray(0, size / 2)), field(short));
  const out = new Uint8Array(size);
  const v = get(wire);
  ok = ok && set(out, v) === 0;
  const back = make();
  ok = ok && deser(back, out) >= 0 && Object.is(get(out), field(back));
  ok = ok && (!exact || Object.is(v, field(back)));
  ok = ok && set(out.subarray(0, 0), v) !== 0;
  return ok;
}

let failures = 0;
function report(name: string, same: boolean): void {
  console.log(`${name.padEnd(40)} ${same ? "same" : "DIFFER"}`);
  if (!same) { failures += 1; }
}

report("uavcan.node.Version",
  checkField(2, makeVersion, deserializeVersionFrom, getVersionMajor, setVersionMajor, (o) => o.major, true) &&
  checkField(2, makeVersion, deserializeVersionFrom, getVersionMinor, setVersionMinor, (o) => o.minor, true));
report("uavcan.primitive.scalar.Integer16",
  checkField(2, makeInteger16, deserializeInteger16From, getInteger16Value, setInteger16Value, (o) => o.value, true));
report("uavcan.primitive.scalar.Natural64",
  checkField(8, makeNatural64, deserializeNatural64From, getNatural64Value, setNatural64Value, (o) => o.value, true));
report("uavcan.primitive.scalar.Real16",
  checkField(2, makeReal16, deserializeReal16From, getReal16Value, setReal16Value, (o) => o.value, false));
report("uavcan.primitive.scalar.Real64",
  checkField(8, makeReal64, deserializeReal64From, getReal64Value, setReal64Value, (o) => o.value, false));
report("uavcan.si.unit.temperature.Scalar",
  checkField(4, makeScalar, deserializeScalarFrom, getScalarKelvin, setScalarKelvin, (o) => o.kelvin, false));
report("uavcan.file.Error",
  checkField(2, makeError, deserializeErrorFrom, getErrorValue, setErrorValue, (o) => o.value, true));
if (failures !== 0) { throw new Error(`${failures} type(s) differ`); }
