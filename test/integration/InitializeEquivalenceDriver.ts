// The default is the factory; the spec's definition is deserialising nothing over it.
import { Heartbeat } from "./uavcan/node/heartbeat_1_0";
import { Frame } from "./uavcan/metatransport/can/frame_0_2";
import { Real32 } from "./uavcan/primitive/array/real32_1_0";
import { SubjectIDList } from "./uavcan/node/port/subject_id_list_1_0";
import { NodeIDAllocationData } from "./uavcan/pnp/node_id_allocation_data_2_0";
import { Record as DiagnosticRecord } from "./uavcan/diagnostic/record_1_1";
import { SynchronizedTimestamp } from "./uavcan/time/synchronized_timestamp_1_0";

let failures = 0;
function check<T>(name: string, make: () => T, deser: (o: T, b: Uint8Array) => number, ser: (o: T, b: Uint8Array) => number): void {
  const a = make();
  const b = make();
  const db = deser(b, new Uint8Array(0));
  const wa = new Uint8Array(4096);
  const wb = new Uint8Array(4096);
  const na = ser(a, wa);
  const nb = ser(b, wb);
  const same = db >= 0 && na >= 0 && na === nb && wa.subarray(0, na).every((v, i) => v === wb[i]);
  console.log(`${name.padEnd(40)} ${same ? "same" : "DIFFER"} (${na} bytes)`);
  if (!same) { failures += 1; }
}

check("uavcan.node.Heartbeat", Heartbeat.create, Heartbeat.deserializeFrom, Heartbeat.serializeInto);
check("uavcan.metatransport.can.Frame", Frame.create, Frame.deserializeFrom, Frame.serializeInto);
check("uavcan.primitive.array.Real32", Real32.create, Real32.deserializeFrom, Real32.serializeInto);
check("uavcan.node.port.SubjectIDList", SubjectIDList.create, SubjectIDList.deserializeFrom, SubjectIDList.serializeInto);
check("uavcan.pnp.NodeIDAllocationData", NodeIDAllocationData.create, NodeIDAllocationData.deserializeFrom, NodeIDAllocationData.serializeInto);
check("uavcan.diagnostic.Record", DiagnosticRecord.create, DiagnosticRecord.deserializeFrom, DiagnosticRecord.serializeInto);
check("uavcan.time.SynchronizedTimestamp", SynchronizedTimestamp.create, SynchronizedTimestamp.deserializeFrom, SynchronizedTimestamp.serializeInto);
if (failures !== 0) {
  throw new Error(`${failures} default(s) differ from the specification's`);
}
