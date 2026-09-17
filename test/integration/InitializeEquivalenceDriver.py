"""The default is the dataclass; the specification's definition is deserialising nothing over it.

Usage: driver.py <generated root> <package>
"""
import importlib
import sys

sys.path.insert(0, sys.argv[1])
PACKAGE = sys.argv[2]


def load(module, name):
    return getattr(importlib.import_module(f"{PACKAGE}.{module}"), name)


def check(name, cls):
    a = cls()
    b = cls()
    consumed = b._deserialize_from(memoryview(b""))
    wa = bytearray(4096)
    wb = bytearray(4096)
    na = a._serialize_into(memoryview(wa))
    nb = b._serialize_into(memoryview(wb))
    same = consumed >= 0 and na >= 0 and na == nb and wa[:na] == wb[:nb]
    print(f"{name:<40} {'same' if same else 'DIFFER'} ({na} bytes)")
    return same


ok = True
ok = check("uavcan.node.Heartbeat", load("uavcan.node.heartbeat_1_0", "Heartbeat")) and ok
ok = check("uavcan.metatransport.can.Frame", load("uavcan.metatransport.can.frame_0_2", "Frame")) and ok
ok = check("uavcan.primitive.array.Real32", load("uavcan.primitive.array.real32_1_0", "Real32")) and ok
ok = check("uavcan.node.port.SubjectIDList", load("uavcan.node.port.subject_id_list_1_0", "SubjectIDList")) and ok
ok = check("uavcan.pnp.NodeIDAllocationData", load("uavcan.pnp.node_id_allocation_data_2_0", "NodeIDAllocationData")) and ok
ok = check("uavcan.diagnostic.Record", load("uavcan.diagnostic.record_1_1", "Record")) and ok
ok = check("uavcan.time.SynchronizedTimestamp", load("uavcan.time.synchronized_timestamp_1_0", "SynchronizedTimestamp")) and ok
sys.exit(0 if ok else 1)
