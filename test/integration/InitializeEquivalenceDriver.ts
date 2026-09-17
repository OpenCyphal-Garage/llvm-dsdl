// The default is the factory; the spec's definition is deserialising nothing over it.
import { makeHeartbeat, deserializeHeartbeatFrom, serializeHeartbeatInto } from "./uavcan/node/heartbeat_1_0";
import { makeFrame, deserializeFrameFrom, serializeFrameInto } from "./uavcan/metatransport/can/frame_0_2";
import { makeReal32, deserializeReal32From, serializeReal32Into } from "./uavcan/primitive/array/real32_1_0";
import { makeSubjectIDList, deserializeSubjectIDListFrom, serializeSubjectIDListInto } from "./uavcan/node/port/subject_id_list_1_0";
import { makeNodeIDAllocationData, deserializeNodeIDAllocationDataFrom, serializeNodeIDAllocationDataInto } from "./uavcan/pnp/node_id_allocation_data_2_0";
import { makeRecord, deserializeRecordFrom, serializeRecordInto } from "./uavcan/diagnostic/record_1_1";
import { makeSynchronizedTimestamp, deserializeSynchronizedTimestampFrom, serializeSynchronizedTimestampInto } from "./uavcan/time/synchronized_timestamp_1_0";

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

check("uavcan.node.Heartbeat", makeHeartbeat, deserializeHeartbeatFrom, serializeHeartbeatInto);
check("uavcan.metatransport.can.Frame", makeFrame, deserializeFrameFrom, serializeFrameInto);
check("uavcan.primitive.array.Real32", makeReal32, deserializeReal32From, serializeReal32Into);
check("uavcan.node.port.SubjectIDList", makeSubjectIDList, deserializeSubjectIDListFrom, serializeSubjectIDListInto);
check("uavcan.pnp.NodeIDAllocationData", makeNodeIDAllocationData, deserializeNodeIDAllocationDataFrom, serializeNodeIDAllocationDataInto);
check("uavcan.diagnostic.Record", makeRecord, deserializeRecordFrom, serializeRecordInto);
check("uavcan.time.SynchronizedTimestamp", makeSynchronizedTimestamp, deserializeSynchronizedTimestampFrom, serializeSynchronizedTimestampInto);
if (failures !== 0) {
  throw new Error(`${failures} default(s) differ from the specification's`);
}
