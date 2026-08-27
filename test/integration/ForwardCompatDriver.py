#===----------------------------------------------------------------------===#
#
# Part of the OpenCyphal project, under the MIT licence
# SPDX-License-Identifier: MIT
#
#===----------------------------------------------------------------------===#

"""Runtime forward/backward compatibility test for the generated Python serializers.

See ForwardCompatDriver.c for the rationale: round-trip tests only decode a buffer with the same type
version that wrote it, so the delimited version-skew skip is never exercised. This decodes across two
versions of a delimited composite (wire.nar.Inner has one field; wire.wid.Inner appends a second).
"""

from dsdl_gen.wire.nar.holder_1_0 import Holder@V1_0@ as NarHolder
from dsdl_gen.wire.nar.inner_1_0 import Inner@V1_0@ as NarInner
from dsdl_gen.wire.wid.holder_1_0 import Holder@V1_0@ as WidHolder
from dsdl_gen.wire.wid.inner_1_0 import Inner@V1_0@ as WidInner


def main() -> None:
    # Forward compatibility: wide writer -> narrow reader skips the appended y.
    wide_in = WidHolder(inner=WidInner(x=1, y=2), tail=99)
    narrow_out = NarHolder.deserialize(wide_in.serialize())
    assert narrow_out.inner.x == 1, narrow_out.inner.x
    assert narrow_out.tail == 99, narrow_out.tail
    print("fwdcompat_ok")

    # Zero extension: narrow writer -> wide reader zero-fills the absent y.
    narrow_in = NarHolder(inner=NarInner(x=5), tail=77)
    wide_out = WidHolder.deserialize(narrow_in.serialize())
    assert wide_out.inner.x == 5, wide_out.inner.x
    assert wide_out.inner.y == 0, wide_out.inner.y
    assert wide_out.tail == 77, wide_out.tail
    print("zeroext_ok")


if __name__ == "__main__":
    main()
