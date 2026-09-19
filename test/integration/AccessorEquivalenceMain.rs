// An accessor against the body it stands in for: a getter answers what deserialise puts in the
// field, on a full buffer and on a short one, and a setter writes what deserialise reads back.
// Values are compared as bits, so a NaN meets itself; an integer round trip is also exact.
use uavcan_dsdl_generated::uavcan::file::error_1_0::uavcan_file_Error;
use uavcan_dsdl_generated::uavcan::node::version_1_0::uavcan_node_Version;
use uavcan_dsdl_generated::uavcan::primitive::scalar::integer16_1_0::uavcan_primitive_scalar_Integer16;
use uavcan_dsdl_generated::uavcan::primitive::scalar::natural64_1_0::uavcan_primitive_scalar_Natural64;
use uavcan_dsdl_generated::uavcan::primitive::scalar::real16_1_0::uavcan_primitive_scalar_Real16;
use uavcan_dsdl_generated::uavcan::primitive::scalar::real64_1_0::uavcan_primitive_scalar_Real64;
use uavcan_dsdl_generated::uavcan::si::unit::angle::quaternion_1_0::uavcan_si_unit_angle_Quaternion;
use uavcan_dsdl_generated::uavcan::si::unit::temperature::scalar_1_0::uavcan_si_unit_temperature_Scalar;

static mut RNG: u32 = 0x9E37_79B9;

fn fill(buffer: &mut [u8]) {
    for b in buffer.iter_mut() {
        // Single-threaded, and the state is read and written in one place.
        let mut s = unsafe { RNG };
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        unsafe { RNG = s };
        *b = s as u8;
    }
}

trait Bits {
    fn bits(&self) -> u64;
}
impl Bits for u8 {
    fn bits(&self) -> u64 {
        *self as u64
    }
}
impl Bits for u16 {
    fn bits(&self) -> u64 {
        *self as u64
    }
}
impl Bits for i16 {
    fn bits(&self) -> u64 {
        *self as u16 as u64
    }
}
impl Bits for u64 {
    fn bits(&self) -> u64 {
        *self
    }
}
impl Bits for f32 {
    fn bits(&self) -> u64 {
        self.to_bits() as u64
    }
}
impl Bits for f64 {
    fn bits(&self) -> u64 {
        self.to_bits()
    }
}

macro_rules! check_field {
    ($t:ty, $field:ident, $get:expr, $set:expr, $size:expr, $exact:expr) => {{
        let mut wire = vec![0u8; $size];
        fill(&mut wire);
        let mut ok = true;
        let (obj, _) = <$t>::from_bytes(&wire).unwrap();
        ok &= $get(&wire).bits() == obj.$field.bits();
        let half = $size / 2;
        let (short, _) = <$t>::from_bytes(&wire[..half]).unwrap();
        ok &= $get(&wire[..half]).bits() == short.$field.bits();
        let mut out = vec![0u8; $size];
        let v = $get(&wire);
        ok &= $set(&mut out, v).is_ok();
        let (back, _) = <$t>::from_bytes(&out).unwrap();
        ok &= $get(&out).bits() == back.$field.bits();
        if $exact {
            ok &= back.$field.bits() == v.bits();
        }
        ok &= $set(&mut out[..0], v).is_err();
        ok
    }};
}

// An element of a fixed array, through the index the accessor takes; one past the capacity reads
// as zero and cannot be set.
macro_rules! check_element {
    ($t:ty, $field:ident, $get:expr, $set:expr, $size:expr, $capacity:expr) => {{
        let mut ok = true;
        for i in 0..$capacity {
            let mut wire = vec![0u8; $size];
            fill(&mut wire);
            let (obj, _) = <$t>::from_bytes(&wire).unwrap();
            ok &= $get(&wire, i).bits() == obj.$field[i].bits();
            ok &= $get(&wire, $capacity).bits() == 0;
            let mut out = vec![0u8; $size];
            let v = $get(&wire, i);
            ok &= $set(&mut out, i, v).is_ok();
            let (back, _) = <$t>::from_bytes(&out).unwrap();
            ok &= $get(&out, i).bits() == back.$field[i].bits();
            ok &= $set(&mut out, $capacity, v).is_err();
        }
        ok
    }};
}

fn report(name: &str, same: bool) -> bool {
    println!("{:<40} {}", name, if same { "same" } else { "DIFFER" });
    same
}

fn main() {
    let mut ok = true;
    ok &= report(
        "uavcan.node.Version",
        check_field!(uavcan_node_Version, major, uavcan_node_Version::get_major, uavcan_node_Version::set_major, 2, true)
            && check_field!(uavcan_node_Version, minor, uavcan_node_Version::get_minor, uavcan_node_Version::set_minor, 2, true),
    );
    ok &= report(
        "uavcan.primitive.scalar.Integer16",
        check_field!(uavcan_primitive_scalar_Integer16, value, uavcan_primitive_scalar_Integer16::get_value, uavcan_primitive_scalar_Integer16::set_value, 2, true),
    );
    ok &= report(
        "uavcan.primitive.scalar.Natural64",
        check_field!(uavcan_primitive_scalar_Natural64, value, uavcan_primitive_scalar_Natural64::get_value, uavcan_primitive_scalar_Natural64::set_value, 8, true),
    );
    ok &= report(
        "uavcan.primitive.scalar.Real16",
        check_field!(uavcan_primitive_scalar_Real16, value, uavcan_primitive_scalar_Real16::get_value, uavcan_primitive_scalar_Real16::set_value, 2, false),
    );
    ok &= report(
        "uavcan.primitive.scalar.Real64",
        check_field!(uavcan_primitive_scalar_Real64, value, uavcan_primitive_scalar_Real64::get_value, uavcan_primitive_scalar_Real64::set_value, 8, false),
    );
    ok &= report(
        "uavcan.si.unit.temperature.Scalar",
        check_field!(uavcan_si_unit_temperature_Scalar, kelvin, uavcan_si_unit_temperature_Scalar::get_kelvin, uavcan_si_unit_temperature_Scalar::set_kelvin, 4, false),
    );
    ok &= report(
        "uavcan.file.Error",
        check_field!(uavcan_file_Error, value, uavcan_file_Error::get_value, uavcan_file_Error::set_value, 2, true),
    );
    ok &= report(
        "uavcan.si.unit.angle.Quaternion",
        check_element!(uavcan_si_unit_angle_Quaternion, wxyz, uavcan_si_unit_angle_Quaternion::get_wxyz, uavcan_si_unit_angle_Quaternion::set_wxyz, 16, 4),
    );
    std::process::exit(if ok { 0 } else { 1 });
}
