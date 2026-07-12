//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//
//
// Isolated cross-language runtime-primitive equivalence driver (Rust).
//
// Reads the shared vector file (argv[1]) and exercises each vector directly
// against the Rust runtime primitives, checking every primitive direction on
// its own so paired bugs cannot mask each other. Prints "PROCESSED <n>" on
// success and exits non-zero on the first mismatch. The C and Go drivers consume
// the exact same file.
//
// The build supplies a sibling `dsdl_runtime` module that points at the real
// `runtime/rust/dsdl_runtime.rs`.
//
//===----------------------------------------------------------------------===//

#![allow(clippy::unusual_byte_groupings)]

mod dsdl_runtime;

use std::io::BufRead;
use std::process::exit;

fn parse_hex_bytes(s: &str) -> Result<Vec<u8>, String> {
    if s.len() % 2 != 0 {
        return Err(format!("odd-length hex: {s}"));
    }
    let mut out = Vec::with_capacity(s.len() / 2);
    let bytes = s.as_bytes();
    let mut i = 0;
    while i < bytes.len() {
        let hi = &s[i..i + 2];
        out.push(u8::from_str_radix(hi, 16).map_err(|e| e.to_string())?);
        i += 2;
    }
    Ok(out)
}

fn hex_u64(s: &str) -> u64 {
    u64::from_str_radix(s, 16).unwrap_or(0)
}

fn fail(op: &str, detail: &str) -> ! {
    eprintln!("MISMATCH {op}: {detail}");
    exit(1);
}

fn main() {
    let path = match std::env::args().nth(1) {
        Some(p) => p,
        None => {
            eprintln!("usage: driver <vectors-file>");
            exit(2);
        }
    };
    let file = match std::fs::File::open(&path) {
        Ok(f) => f,
        Err(_) => {
            eprintln!("cannot open vectors file: {path}");
            exit(2);
        }
    };

    let mut processed: usize = 0;
    for line in std::io::BufReader::new(file).lines() {
        let line = line.unwrap_or_default();
        let line = match line.find('#') {
            Some(idx) => &line[..idx],
            None => &line[..],
        };
        let f: Vec<&str> = line.split_whitespace().collect();
        if f.is_empty() {
            continue;
        }
        let op = f[0];

        match op {
            "f16pack" => {
                if f.len() != 3 {
                    fail(op, "arity");
                }
                let got = dsdl_runtime::float16_pack(f32::from_bits(hex_u64(f[1]) as u32));
                let want = hex_u64(f[2]) as u16;
                if got != want {
                    fail(op, &format!("in={} want={want:04x} got={got:04x}", f[1]));
                }
            }
            "f16unpack" => {
                if f.len() != 3 {
                    fail(op, "arity");
                }
                let got = dsdl_runtime::float16_unpack(hex_u64(f[1]) as u16).to_bits();
                let want = hex_u64(f[2]) as u32;
                if got != want {
                    fail(op, &format!("in={} want={want:08x} got={got:08x}", f[1]));
                }
            }
            "geti" | "getu" => {
                if f.len() != 6 {
                    fail(op, "arity");
                }
                let type_bits: u32 = f[1].parse().unwrap_or(0);
                let len_bits: u8 = f[2].parse().unwrap_or(0);
                let off_bits: usize = f[3].parse().unwrap_or(0);
                let buf = parse_hex_bytes(f[4]).unwrap_or_else(|_| fail(op, "bad buffer hex"));
                let want = hex_u64(f[5]);
                let got: u64 = if op == "geti" {
                    match type_bits {
                        8 => dsdl_runtime::get_i8(&buf, off_bits, len_bits) as i64 as u64,
                        16 => dsdl_runtime::get_i16(&buf, off_bits, len_bits) as i64 as u64,
                        32 => dsdl_runtime::get_i32(&buf, off_bits, len_bits) as i64 as u64,
                        64 => dsdl_runtime::get_i64(&buf, off_bits, len_bits) as u64,
                        _ => fail(op, "bad type_bits"),
                    }
                } else {
                    match type_bits {
                        8 => dsdl_runtime::get_u8(&buf, off_bits, len_bits) as u64,
                        16 => dsdl_runtime::get_u16(&buf, off_bits, len_bits) as u64,
                        32 => dsdl_runtime::get_u32(&buf, off_bits, len_bits) as u64,
                        64 => dsdl_runtime::get_u64(&buf, off_bits, len_bits),
                        _ => fail(op, "bad type_bits"),
                    }
                };
                if got != want {
                    fail(
                        op,
                        &format!(
                            "type={type_bits} len={len_bits} off={off_bits} buf={} want={want:016x} got={got:016x}",
                            f[4]
                        ),
                    );
                }
            }
            "copybits" => {
                if f.len() != 7 {
                    fail(op, "arity");
                }
                let dst_off: usize = f[1].parse().unwrap_or(0);
                let len_bits: usize = f[2].parse().unwrap_or(0);
                let src_off: usize = f[3].parse().unwrap_or(0);
                let src = parse_hex_bytes(f[4]).unwrap_or_else(|_| fail(op, "bad buffer hex"));
                let mut dst = parse_hex_bytes(f[5]).unwrap_or_else(|_| fail(op, "bad buffer hex"));
                let want = parse_hex_bytes(f[6]).unwrap_or_else(|_| fail(op, "bad buffer hex"));
                if dst.len() != want.len() {
                    fail(op, "dst/want length mismatch");
                }
                dsdl_runtime::copy_bits(&mut dst, dst_off, len_bits, &src, src_off);
                if dst != want {
                    fail(
                        op,
                        &format!("dstoff={dst_off} len={len_bits} srcoff={src_off} got={dst:02x?}"),
                    );
                }
            }
            _ => {
                eprintln!("UNRECOGNIZED vector: {line}");
                exit(1);
            }
        }
        processed += 1;
    }

    println!("PROCESSED {processed}");
    println!("Rust primitive equivalence PASS");
}
