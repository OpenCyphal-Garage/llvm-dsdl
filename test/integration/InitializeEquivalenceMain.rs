// The default is `Default`; the spec's definition is deserialising nothing over it. A default
// that survives that -- deserialising nothing writes every member -- is the spec's.
use uavcan_dsdl_generated::uavcan::node::heartbeat_1_0::Heartbeat;
use uavcan_dsdl_generated::uavcan::metatransport::can::frame_0_2::Frame;
use uavcan_dsdl_generated::uavcan::primitive::array::real32_1_0::Real32;
use uavcan_dsdl_generated::uavcan::node::port::subject_id_list_1_0::SubjectIDList;
use uavcan_dsdl_generated::uavcan::pnp::node_id_allocation_data_2_0::NodeIDAllocationData;
use uavcan_dsdl_generated::uavcan::diagnostic::record_1_1::Record;
use uavcan_dsdl_generated::uavcan::time::synchronized_timestamp_1_0::SynchronizedTimestamp;

macro_rules! check {
    ($name:expr, $t:ty) => {{
        let a: $t = <$t>::default();
        let mut b: $t = <$t>::default();
        let db = b.deserialize(&[]);
        let mut wa = vec![0u8; 4096];
        let mut wb = vec![0u8; 4096];
        let sa = a.serialize(&mut wa);
        let sb = b.serialize(&mut wb);
        let same = match (db, sa, sb) {
            (Ok(_), Ok(na), Ok(nb)) => na == nb && wa[..na] == wb[..nb],
            _ => false,
        };
        println!("{:<40} {}", $name, if same { "same" } else { "DIFFER" });
        same
    }};
}

fn main() {
    let mut ok = true;
    ok &= check!("uavcan.node.Heartbeat", Heartbeat);
    ok &= check!("uavcan.metatransport.can.Frame", Frame);
    ok &= check!("uavcan.primitive.array.Real32", Real32);
    ok &= check!("uavcan.node.port.SubjectIDList", SubjectIDList);
    ok &= check!("uavcan.pnp.NodeIDAllocationData", NodeIDAllocationData);
    ok &= check!("uavcan.diagnostic.Record", Record);
    ok &= check!("uavcan.time.SynchronizedTimestamp", SynchronizedTimestamp);
    if !ok { std::process::exit(1); }
}
