#include <cstdint>
#include <cstdio>
#include <array>
#include <cstring>
#include "uavcan/node/Version_1_0.hpp"
#include "uavcan/primitive/scalar/Integer16_1_0.hpp"
#include "uavcan/primitive/scalar/Natural64_1_0.hpp"
#include "uavcan/primitive/scalar/Real16_1_0.hpp"
#include "uavcan/primitive/scalar/Real64_1_0.hpp"
#include "uavcan/si/unit/temperature/Scalar_1_0.hpp"
#include "uavcan/file/Error_1_0.hpp"
#include "uavcan/si/unit/angle/Quaternion_1_0.hpp"

// An accessor against the body it stands in for: a getter answers what deserialise puts in the
// field, on a full buffer and on a short one, and a setter writes what deserialise reads back.
// Values are compared as bits, so a NaN meets itself; an integer round trip is also exact.
static std::uint32_t rng = 0x9E3779B9u;

static void fill(std::uint8_t* const buffer, const std::size_t size)
{
    for (std::size_t i = 0; i < size; ++i)
    {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        buffer[i] = static_cast<std::uint8_t>(rng);
    }
}

template <typename V>
static bool sameBits(const V& a, const V& b)
{
    return std::memcmp(&a, &b, sizeof(V)) == 0;
}

template <typename T, typename V, std::size_t Size>
static bool checkField(V (*get)(const std::uint8_t*, std::size_t),
                       std::int8_t (*set)(std::uint8_t*, std::size_t, V),
                       V T::*     member,
                       const bool exact)
{
    std::uint8_t wire[Size];
    std::uint8_t out[Size] = {};
    fill(wire, Size);
    T           obj{};
    std::size_t size = Size;
    bool        ok   = obj.deserialize(wire, &size) == 0;
    ok               = ok && sameBits(get(wire, Size), obj.*member);
    size             = Size / 2;
    ok               = ok && obj.deserialize(wire, &size) == 0;
    ok               = ok && sameBits(get(wire, Size / 2), obj.*member);
    const V v        = get(wire, Size);
    ok               = ok && set(out, Size, v) == 0;
    size             = Size;
    ok               = ok && obj.deserialize(out, &size) == 0;
    ok               = ok && sameBits(get(out, Size), obj.*member);
    ok               = ok && (!exact || sameBits(v, obj.*member));
    ok               = ok && set(out, 0, v) != 0;
    return ok;
}

// An element of a fixed array, through the index the accessor takes; one past the capacity reads
// as zero and cannot be set.
template <typename T, typename V, std::size_t Size, std::size_t Capacity>
static bool checkElement(V (*get)(const std::uint8_t*, std::size_t, std::size_t),
                         std::int8_t (*set)(std::uint8_t*, std::size_t, std::size_t, V),
                         std::array<V, Capacity> T::* member)
{
    bool ok = true;
    for (std::size_t i = 0; i < Capacity; ++i)
    {
        std::uint8_t wire[Size];
        std::uint8_t out[Size] = {};
        fill(wire, Size);
        T           obj{};
        std::size_t size = Size;
        const V     zero{};
        ok        = ok && obj.deserialize(wire, &size) == 0;
        ok        = ok && sameBits(get(wire, Size, i), (obj.*member)[i]);
        ok        = ok && sameBits(get(wire, Size, Capacity), zero);
        const V v = get(wire, Size, i);
        ok        = ok && set(out, Size, i, v) == 0;
        size      = Size;
        ok        = ok && obj.deserialize(out, &size) == 0;
        ok        = ok && sameBits(get(out, Size, i), (obj.*member)[i]);
        ok        = ok && set(out, Size, Capacity, v) != 0;
    }
    return ok;
}

static int failures = 0;

static void report(const char* const name, const bool same)
{
    std::printf("%-40s %s\n", name, same ? "same" : "DIFFER");
    if (!same)
    {
        ++failures;
    }
}

int main()
{
    using uavcan::file::Error;
    using uavcan::node::Version;
    using uavcan::primitive::scalar::Integer16;
    using uavcan::primitive::scalar::Natural64;
    using uavcan::primitive::scalar::Real16;
    using uavcan::primitive::scalar::Real64;
    using uavcan::si::unit::temperature::Scalar;
    report("uavcan.node.Version",
           checkField<Version, std::uint8_t, 2>(&Version::get_major, &Version::set_major, &Version::major, true) &&
               checkField<Version, std::uint8_t, 2>(&Version::get_minor, &Version::set_minor, &Version::minor, true));
    report("uavcan.primitive.scalar.Integer16",
           checkField<Integer16, std::int16_t, 2>(&Integer16::get_value,
                                                  &Integer16::set_value,
                                                  &Integer16::value,
                                                  true));
    report("uavcan.primitive.scalar.Natural64",
           checkField<Natural64, std::uint64_t, 8>(&Natural64::get_value,
                                                   &Natural64::set_value,
                                                   &Natural64::value,
                                                   true));
    report("uavcan.primitive.scalar.Real16",
           checkField<Real16, float, 2>(&Real16::get_value, &Real16::set_value, &Real16::value, false));
    report("uavcan.primitive.scalar.Real64",
           checkField<Real64, double, 8>(&Real64::get_value, &Real64::set_value, &Real64::value, false));
    report("uavcan.si.unit.temperature.Scalar",
           checkField<Scalar, float, 4>(&Scalar::get_kelvin, &Scalar::set_kelvin, &Scalar::kelvin, false));
    report("uavcan.file.Error",
           checkField<Error, std::uint16_t, 2>(&Error::get_value, &Error::set_value, &Error::value, true));
    using uavcan::si::unit::angle::Quaternion;
    report("uavcan.si.unit.angle.Quaternion",
           checkElement<Quaternion, float, 16, 4>(&Quaternion::get_wxyz, &Quaternion::set_wxyz, &Quaternion::wxyz));
    return failures == 0 ? 0 : 1;
}
