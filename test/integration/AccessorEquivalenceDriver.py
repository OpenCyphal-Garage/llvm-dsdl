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
sys.exit(0 if ok else 1)
