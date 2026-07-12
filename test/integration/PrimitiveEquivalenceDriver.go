//===----------------------------------------------------------------------===//
///
/// @file
/// Isolated cross-language runtime-primitive equivalence driver (Go).
///
/// Reads the shared vector file (os.Args[1]) and exercises each vector directly
/// against the Go runtime primitives, checking every primitive direction on its
/// own so paired bugs cannot mask each other. Prints "PROCESSED <n>" on success
/// and exits non-zero on the first mismatch. The C and Rust drivers consume the
/// exact same file.
///
//===----------------------------------------------------------------------===//

package main

import (
	"bufio"
	"bytes"
	"fmt"
	"math"
	"os"
	"strconv"
	"strings"

	rt "github.com/thirtytwobits/llvm-dsdl/runtime/go"
)

func parseHexBytes(s string) ([]byte, error) {
	if len(s)%2 != 0 {
		return nil, fmt.Errorf("odd-length hex: %q", s)
	}
	out := make([]byte, len(s)/2)
	for i := 0; i < len(s); i += 2 {
		v, err := strconv.ParseUint(s[i:i+2], 16, 8)
		if err != nil {
			return nil, err
		}
		out[i/2] = byte(v)
	}
	return out, nil
}

func hexU64(s string) uint64 {
	v, _ := strconv.ParseUint(s, 16, 64)
	return v
}

func main() {
	if len(os.Args) < 2 {
		fmt.Fprintln(os.Stderr, "usage: driver <vectors-file>")
		os.Exit(2)
	}
	f, err := os.Open(os.Args[1])
	if err != nil {
		fmt.Fprintf(os.Stderr, "cannot open vectors file: %s\n", os.Args[1])
		os.Exit(2)
	}
	defer f.Close()

	processed := 0
	sc := bufio.NewScanner(f)
	for sc.Scan() {
		line := sc.Text()
		if i := strings.IndexByte(line, '#'); i >= 0 {
			line = line[:i]
		}
		fields := strings.Fields(line)
		if len(fields) == 0 {
			continue
		}
		op := fields[0]
		fail := func(detail string) {
			fmt.Fprintf(os.Stderr, "MISMATCH %s: %s\n", op, detail)
			os.Exit(1)
		}

		switch op {
		case "f16pack":
			if len(fields) != 3 {
				fail("arity")
			}
			got := rt.Float16Pack(math.Float32frombits(uint32(hexU64(fields[1]))))
			want := uint16(hexU64(fields[2]))
			if got != want {
				fail(fmt.Sprintf("in=%s want=%04x got=%04x", fields[1], want, got))
			}
		case "f16unpack":
			if len(fields) != 3 {
				fail("arity")
			}
			got := math.Float32bits(rt.Float16Unpack(uint16(hexU64(fields[1]))))
			want := uint32(hexU64(fields[2]))
			if got != want {
				fail(fmt.Sprintf("in=%s want=%08x got=%08x", fields[1], want, got))
			}
		case "geti", "getu":
			if len(fields) != 6 {
				fail("arity")
			}
			typeBits, _ := strconv.Atoi(fields[1])
			lenBits, _ := strconv.Atoi(fields[2])
			offBits, _ := strconv.Atoi(fields[3])
			buf, e := parseHexBytes(fields[4])
			if e != nil {
				fail("bad buffer hex")
			}
			want := hexU64(fields[5])
			var got uint64
			if op == "geti" {
				switch typeBits {
				case 8:
					got = uint64(int64(rt.GetI8(buf, offBits, uint8(lenBits))))
				case 16:
					got = uint64(int64(rt.GetI16(buf, offBits, uint8(lenBits))))
				case 32:
					got = uint64(int64(rt.GetI32(buf, offBits, uint8(lenBits))))
				case 64:
					got = uint64(rt.GetI64(buf, offBits, uint8(lenBits)))
				default:
					fail("bad type_bits")
				}
			} else {
				switch typeBits {
				case 8:
					got = uint64(rt.GetU8(buf, offBits, uint8(lenBits)))
				case 16:
					got = uint64(rt.GetU16(buf, offBits, uint8(lenBits)))
				case 32:
					got = uint64(rt.GetU32(buf, offBits, uint8(lenBits)))
				case 64:
					got = rt.GetU64(buf, offBits, uint8(lenBits))
				default:
					fail("bad type_bits")
				}
			}
			if got != want {
				fail(fmt.Sprintf("type=%d len=%d off=%d buf=%s want=%016x got=%016x",
					typeBits, lenBits, offBits, fields[4], want, got))
			}
		case "copybits":
			if len(fields) != 7 {
				fail("arity")
			}
			dstOff, _ := strconv.Atoi(fields[1])
			lenBits, _ := strconv.Atoi(fields[2])
			srcOff, _ := strconv.Atoi(fields[3])
			src, e1 := parseHexBytes(fields[4])
			dst, e2 := parseHexBytes(fields[5])
			want, e3 := parseHexBytes(fields[6])
			if e1 != nil || e2 != nil || e3 != nil || len(dst) != len(want) {
				fail("bad buffer hex / length")
			}
			rt.CopyBits(dst, dstOff, lenBits, src, srcOff)
			if !bytes.Equal(dst, want) {
				fail(fmt.Sprintf("dstoff=%d len=%d srcoff=%d got=%x", dstOff, lenBits, srcOff, dst))
			}
		default:
			fmt.Fprintf(os.Stderr, "UNRECOGNIZED vector: %s\n", line)
			os.Exit(1)
		}
		processed++
	}
	if err := sc.Err(); err != nil {
		fmt.Fprintf(os.Stderr, "read error: %v\n", err)
		os.Exit(2)
	}

	fmt.Printf("PROCESSED %d\n", processed)
	fmt.Println("Go primitive equivalence PASS")
}
