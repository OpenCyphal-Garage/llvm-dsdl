"""An accessor against the body it stands in for: a getter answers what deserialise puts in the
field, on a full buffer and on a short one, and a setter writes what deserialise reads back. Values
are compared as bits, so a NaN meets itself; an integer round trip is also exact.
Usage: driver.py <generated root> <package>
"""
import importlib
import struct
import sys

sys.path.insert(0, sys.argv[1])
PACKAGE = sys.argv[2]

_rng = 0x9E3779B9


def fill(size):
    global _rng
    out = bytearray(size)
    for i in range(size):
        _rng ^= (_rng << 13) & 0xFFFFFFFF
        _rng ^= _rng >> 17
        _rng ^= (_rng << 5) & 0xFFFFFFFF
        out[i] = _rng & 0xFF
    return out


def bits(value):
    return struct.pack("<d", value) if isinstance(value, float) else value


def load(module, name):
    return getattr(importlib.import_module(f"{PACKAGE}.{module}"), name)


def check_field(cls, size, field, exact):
    get = getattr(cls, f"get_{field}")
    set_ = getattr(cls, f"set_{field}")
    wire = fill(size)
    ok = bits(get(memoryview(wire))) == bits(getattr(cls.deserialize(bytes(wire)), field))
    half = memoryview(wire)[: size // 2]
    ok = ok and bits(get(half)) == bits(getattr(cls.deserialize(bytes(half)), field))
    out = bytearray(size)
    v = get(memoryview(wire))
    ok = ok and set_(memoryview(out), v) == 0
    back = cls.deserialize(bytes(out))
    ok = ok and bits(get(memoryview(out))) == bits(getattr(back, field))
    ok = ok and (not exact or bits(v) == bits(getattr(back, field)))
    ok = ok and set_(memoryview(out)[:0], v) != 0
    return ok


def check_element(cls, size, field, capacity):
    """An element of a fixed array, through the index the accessor takes; one past the capacity
    reads as zero and cannot be set."""
    get = getattr(cls, f"get_{field}")
    set_ = getattr(cls, f"set_{field}")
    ok = True
    for i in range(capacity):
        wire = fill(size)
        ok = ok and bits(get(memoryview(wire), i)) == bits(getattr(cls.deserialize(bytes(wire)), field)[i])
        ok = ok and get(memoryview(wire), capacity) == 0
        out = bytearray(size)
        v = get(memoryview(wire), i)
        ok = ok and set_(memoryview(out), i, v) == 0
        ok = ok and bits(get(memoryview(out), i)) == bits(getattr(cls.deserialize(bytes(out)), field)[i])
        ok = ok and set_(memoryview(out), capacity, v) != 0
    return ok


def report(name, same):
    print(f"{name:<40} {'same' if same else 'DIFFER'}")
    return same


ok = True
version = load("uavcan.node.version_1_0", "Version")
ok = report("uavcan.node.Version", check_field(version, 2, "major", True) and check_field(version, 2, "minor", True)) and ok
ok = report("uavcan.primitive.scalar.Integer16", check_field(load("uavcan.primitive.scalar.integer16_1_0", "Integer16"), 2, "value", True)) and ok
ok = report("uavcan.primitive.scalar.Natural64", check_field(load("uavcan.primitive.scalar.natural64_1_0", "Natural64"), 8, "value", True)) and ok
ok = report("uavcan.primitive.scalar.Real16", check_field(load("uavcan.primitive.scalar.real16_1_0", "Real16"), 2, "value", False)) and ok
ok = report("uavcan.primitive.scalar.Real64", check_field(load("uavcan.primitive.scalar.real64_1_0", "Real64"), 8, "value", False)) and ok
ok = report("uavcan.si.unit.temperature.Scalar", check_field(load("uavcan.si.unit.temperature.scalar_1_0", "Scalar"), 4, "kelvin", False)) and ok
ok = report("uavcan.file.Error", check_field(load("uavcan.file.error_1_0", "Error"), 2, "value", True)) and ok
ok = report("uavcan.si.unit.angle.Quaternion", check_element(load("uavcan.si.unit.angle.quaternion_1_0", "Quaternion"), 16, "wxyz", 4)) and ok
# A nested composite, through the buffer its getter answers: the nested type's own getter on it
# agrees with deserialise on the full buffer and on one cut inside the nested field.
sample = load("uavcan.si.sample.temperature.scalar_1_0", "Scalar")
stamp_type = load("uavcan.time.synchronized_timestamp_1_0", "SynchronizedTimestamp")
wire = fill(11)
same = stamp_type.get_microsecond(sample.get_timestamp(memoryview(wire))) == sample.deserialize(bytes(wire)).timestamp.microsecond
stamp = sample.get_timestamp(memoryview(wire)[:3])
same = same and len(stamp) == 3 and stamp_type.get_microsecond(stamp) == sample.deserialize(bytes(wire[:3])).timestamp.microsecond
same = same and check_field(sample, 11, "kelvin", False)
ok = report("uavcan.si.sample.temperature.Scalar", same) and ok
sys.exit(0 if ok else 1)
