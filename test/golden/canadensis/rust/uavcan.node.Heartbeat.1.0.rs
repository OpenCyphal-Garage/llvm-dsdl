#[cfg(not(target_endian = "little"))]
compile_error!("Zero-copy serialization requires a little-endian target");
#[allow(unused_variables, unused_braces, unused_parens)]
#[allow(clippy::identity_op)]
pub mod uavcan {
    pub mod node {
        pub mod health_1_0 {
            /// `uavcan.node.Health.1.0`
            ///
            /// Fixed size 1 bytes
            ///
            #[cfg_attr(
                not(doctest),
                doc = " Abstract component health information. If the node performs multiple activities (provides multiple network services),\n its health status should reflect the status of the worst-performing activity (network service).\n Follows:\n   https://www.law.cornell.edu/cfr/text/14/23.1322\n   https://www.faa.gov/documentLibrary/media/Advisory_Circular/AC_25.1322-1.pdf section 6"
            )]
            pub struct Health {
                ///
                /// `saturated uint2`
                ///
                /// Always aligned,
                /// size 2 bits
                pub value: u8,
            }
            impl ::canadensis_encoding::DataType for Health {
                /// This type is sealed.
                const EXTENT_BYTES: Option<u32> = None;
            }
            impl ::canadensis_encoding::Message for Health {}
            impl Health {
                #[cfg_attr(
                    not(doctest),
                    doc = " The component is functioning properly (nominal)."
                )]
                pub const NOMINAL: u8 = 0;
                #[cfg_attr(
                    not(doctest),
                    doc = " A critical parameter went out of range or the component encountered a minor failure that does not prevent\n the subsystem from performing any of its real-time functions."
                )]
                pub const ADVISORY: u8 = 1;
                #[cfg_attr(
                    not(doctest),
                    doc = " The component encountered a major failure and is performing in a degraded mode or outside of its designed limitations."
                )]
                pub const CAUTION: u8 = 2;
                #[cfg_attr(
                    not(doctest),
                    doc = " The component suffered a fatal malfunction and is unable to perform its intended function."
                )]
                pub const WARNING: u8 = 3;
            }
            impl ::canadensis_encoding::Serialize for Health {
                fn size_bits(&self) -> usize {
                    8
                }
                fn serialize(&self, cursor: &mut ::canadensis_encoding::WriteCursor<'_>) {
                    cursor.write_u2(self.value);
                }
            }
            impl ::canadensis_encoding::Deserialize for Health {
                fn deserialize(
                    cursor: &mut ::canadensis_encoding::ReadCursor<'_>,
                ) -> ::core::result::Result<Self, ::canadensis_encoding::DeserializeError>
                where
                    Self: Sized,
                {
                    Ok(Health {
                        value: { cursor.read_u2() as _ },
                    })
                }
            }
        }
        pub mod heartbeat_1_0 {
            #[cfg_attr(not(doctest), doc = "The fixed subject ID for this message type")]
            pub const SUBJECT: ::canadensis_core::SubjectId =
                ::canadensis_core::SubjectId::from_truncating(7509);

            /// `uavcan.node.Heartbeat.1.0`
            ///
            /// Fixed size 7 bytes
            ///
            #[cfg_attr(
                not(doctest),
                doc = " Abstract node status information.\n This is the only high-level function that shall be implemented by all nodes.\n\n All Cyphal nodes that have a node-ID are required to publish this message to its fixed subject periodically.\n Nodes that do not have a node-ID (also known as \"anonymous nodes\") shall not publish to this subject.\n\n The default subject-ID 7509 is 1110101010101 in binary. The alternating bit pattern at the end helps transceiver\n synchronization (e.g., on CAN-based networks) and on some transports permits automatic bit rate detection.\n\n Network-wide health monitoring can be implemented by subscribing to the fixed subject."
            )]
            pub struct Heartbeat {
                #[cfg_attr(
                    not(doctest),
                    doc = " [second]\n The uptime seconds counter should never overflow. The counter will reach the upper limit in ~136 years,\n upon which time it should stay at 0xFFFFFFFF until the node is restarted.\n Other nodes may detect that a remote node has restarted when this value leaps backwards."
                )]
                ///
                /// `saturated uint32`
                ///
                /// Always aligned,
                /// size 32 bits
                pub uptime: u32,
                #[cfg_attr(not(doctest), doc = " The abstract health status of this node.")]
                ///
                /// `uavcan.node.Health.1.0`
                ///
                /// Always aligned,
                /// size 8 bits
                pub health: crate::uavcan::node::health_1_0::Health,
                #[cfg_attr(
                    not(doctest),
                    doc = " The abstract operating mode of the publishing node.\n This field indicates the general level of readiness that can be further elaborated on a per-activity basis\n using various specialized interfaces."
                )]
                ///
                /// `uavcan.node.Mode.1.0`
                ///
                /// Always aligned,
                /// size 8 bits
                pub mode: crate::uavcan::node::mode_1_0::Mode,
                #[cfg_attr(
                    not(doctest),
                    doc = " Optional, vendor-specific node status code, e.g. a fault code or a status bitmask.\n Fits into a single-frame Classic CAN transfer (least capable transport, smallest MTU)."
                )]
                ///
                /// `saturated uint8`
                ///
                /// Always aligned,
                /// size 8 bits
                pub vendor_specific_status_code: u8,
            }
            impl ::canadensis_encoding::DataType for Heartbeat {
                /// This type is delimited with an extent of 12 bytes.
                const EXTENT_BYTES: Option<u32> = Some(12);
            }
            impl ::canadensis_encoding::Message for Heartbeat {}
            impl Heartbeat {
                #[cfg_attr(
                    not(doctest),
                    doc = " [second]\n The publication period shall not exceed this limit.\n The period should not change while the node is running."
                )]
                pub const MAX_PUBLICATION_PERIOD: u16 = 1;
                #[cfg_attr(
                    not(doctest),
                    doc = " [second]\n If the last message from the node was received more than this amount of time ago, it should be considered offline."
                )]
                pub const OFFLINE_TIMEOUT: u16 = 3;
            }
            impl ::canadensis_encoding::Serialize for Heartbeat {
                fn size_bits(&self) -> usize {
                    56
                }
                fn serialize(&self, cursor: &mut ::canadensis_encoding::WriteCursor<'_>) {
                    cursor.write_aligned_u32(self.uptime);
                    cursor.write_composite(&self.health);
                    cursor.write_composite(&self.mode);
                    cursor.write_aligned_u8(self.vendor_specific_status_code);
                }
            }
            impl ::canadensis_encoding::Deserialize for Heartbeat {
                fn deserialize(
                    cursor: &mut ::canadensis_encoding::ReadCursor<'_>,
                ) -> ::core::result::Result<Self, ::canadensis_encoding::DeserializeError>
                where
                    Self: Sized,
                {
                    Ok(Heartbeat {
                        uptime: { cursor.read_u32() as _ },
                        health: { cursor.read_composite()? },
                        mode: { cursor.read_composite()? },
                        vendor_specific_status_code: { cursor.read_u8() as _ },
                    })
                }
            }
        }
        pub mod mode_1_0 {
            /// `uavcan.node.Mode.1.0`
            ///
            /// Fixed size 1 bytes
            ///
            #[cfg_attr(
                not(doctest),
                doc = " The operating mode of a node.\n Reserved values can be used in future revisions of the specification."
            )]
            pub struct Mode {
                ///
                /// `saturated uint3`
                ///
                /// Always aligned,
                /// size 3 bits
                pub value: u8,
            }
            impl ::canadensis_encoding::DataType for Mode {
                /// This type is sealed.
                const EXTENT_BYTES: Option<u32> = None;
            }
            impl ::canadensis_encoding::Message for Mode {}
            impl Mode {
                #[cfg_attr(not(doctest), doc = " Normal operating mode.")]
                pub const OPERATIONAL: u8 = 0;
                #[cfg_attr(
                    not(doctest),
                    doc = " Initialization is in progress; this mode is entered immediately after startup."
                )]
                pub const INITIALIZATION: u8 = 1;
                #[cfg_attr(not(doctest), doc = " E.g., calibration, self-test, etc.")]
                pub const MAINTENANCE: u8 = 2;
                #[cfg_attr(
                    not(doctest),
                    doc = " New software/firmware is being loaded or the bootloader is running."
                )]
                pub const SOFTWARE_UPDATE: u8 = 3;
            }
            impl ::canadensis_encoding::Serialize for Mode {
                fn size_bits(&self) -> usize {
                    8
                }
                fn serialize(&self, cursor: &mut ::canadensis_encoding::WriteCursor<'_>) {
                    cursor.write_u3(self.value);
                }
            }
            impl ::canadensis_encoding::Deserialize for Mode {
                fn deserialize(
                    cursor: &mut ::canadensis_encoding::ReadCursor<'_>,
                ) -> ::core::result::Result<Self, ::canadensis_encoding::DeserializeError>
                where
                    Self: Sized,
                {
                    Ok(Mode {
                        value: { cursor.read_u3() as _ },
                    })
                }
            }
        }
    }
}
