#===----------------------------------------------------------------------===#
#
# Part of the OpenCyphal project, under the MIT licence
# SPDX-License-Identifier: MIT
#
#===----------------------------------------------------------------------===#
#
# The C probe's reading of an equal-length union, in Python.
#
#===----------------------------------------------------------------------===#

import struct, sys
sys.path.insert(0, sys.argv[1])
from dsdl_gen.fixtures_union.vendor.choice_1_0 import Choice
wire = bytearray(b"\x01" + struct.pack("<f", 5.5))
mv = memoryview(wire)
ok = Choice.get_tag_(mv) == 1 and Choice.get_real(mv) == 5.5 and len(Choice.get_quad(mv)) == 4 and Choice.set_tag_(mv, 0) == 0
print(f"union-accessors Python: {'ok' if ok else 'FAILED'}")
sys.exit(0 if ok else 1)
