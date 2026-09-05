//===----------------------------------------------------------------------===//
///
/// @file
/// Go Cyphal node demo exposing heartbeat and register services.
///
/// This node uses llvm-dsdl-generated Go types for serialisation and a small
/// cgo shim around libudpard + POSIX UDP for transport.
///
//===----------------------------------------------------------------------===//

package main

/*
#cgo CFLAGS: -std=c11
#include "transport_shim.h"
#include <stdlib.h>
*/
import "C"

import (
	"errors"
	"flag"
	"fmt"
	"math"
	"os"
	"os/signal"
	"syscall"
	"unsafe"

	nodepkg "cyphal_yakut_demo_generated/uavcan/node"
	primpkg "cyphal_yakut_demo_generated/uavcan/primitive"
	arraypkg "cyphal_yakut_demo_generated/uavcan/primitive/array"
	regpkg "cyphal_yakut_demo_generated/uavcan/register"
	timepkg "cyphal_yakut_demo_generated/uavcan/time"
)

const (
	subjectHeartbeat      uint16 = 7509
	serviceRegisterAccess uint16 = 384
	serviceRegisterList   uint16 = 385
	txQueueCapacity       uint32 = 128
	rxDatagramCapacity    uint64 = 2048
	txDeadlineUsec        uint64 = 1_000_000
	maxWaitUsec           uint64 = 50_000
)

const (
	valueTagEmpty     uint8 = 0
	valueTagString    uint8 = 1
	valueTagInteger64 uint8 = 4
	valueTagInteger32 uint8 = 5
	valueTagInteger16 uint8 = 6
	valueTagInteger8  uint8 = 7
	valueTagNatural64 uint8 = 8
	valueTagNatural32 uint8 = 9
	valueTagNatural16 uint8 = 10
	valueTagNatural8  uint8 = 11
)

type options struct {
	name            string
	nodeID          uint16
	ifaceAddress    string
	heartbeatRateHz uint32
}

type parseResult int

const (
	parseSuccess parseResult = iota
	parseHelp
	parseError
)

type registerKind int

const (
	registerNatural16 registerKind = iota
	registerNatural32
	registerString
)

type registerEntry struct {
	name       string
	kind       registerKind
	mutable    bool
	persistent bool
	natural16  uint16
	natural32  uint32
	stringVal  string
}

type nodeApp struct {
	opts options
	node *C.GoDemoNode

	heartbeatTransferID uint8
	startedAtUsec       uint64
	heartbeatPeriodUsec uint64
	nextHeartbeatAt     uint64
	heartbeatCounter    uint32

	registers []registerEntry
}

func printUsage(programName string) {
	fmt.Fprintf(os.Stderr,
		"Usage: %s [options]\n"+
			"  --name <label>              Node label for log output (default: native-go)\n"+
			"  --node-id <n>               Local node-ID [0, %d]\n"+
			"  --iface <ipv4>              Local iface IPv4 address (default: 127.0.0.1)\n"+
			"  --heartbeat-rate-hz <n>     Heartbeat publication rate in Hz (default: 1)\n"+
			"  --help                      Show this help\n",
		programName,
		int(C.go_demo_node_id_max()))
}

func parseOptions(argv []string) (options, parseResult) {
	opts := options{
		name:            "native-go",
		nodeID:          0,
		ifaceAddress:    "127.0.0.1",
		heartbeatRateHz: 1,
	}

	fs := flag.NewFlagSet(argv[0], flag.ContinueOnError)
	fs.SetOutput(os.Stderr)

	showHelp := fs.Bool("help", false, "show help")
	name := fs.String("name", opts.name, "node label")
	nodeID := fs.Int("node-id", -1, "node ID")
	iface := fs.String("iface", opts.ifaceAddress, "local iface IPv4")
	rate := fs.Uint("heartbeat-rate-hz", uint(opts.heartbeatRateHz), "heartbeat publication rate")

	if err := fs.Parse(argv[1:]); err != nil {
		return opts, parseError
	}
	if *showHelp {
		printUsage(argv[0])
		return opts, parseHelp
	}

	if *nodeID < 0 || *nodeID > int(C.go_demo_node_id_max()) {
		fmt.Fprintf(os.Stderr, "Invalid --node-id: %d\n", *nodeID)
		return opts, parseError
	}
	if *rate == 0 || *rate > 1000 {
		fmt.Fprintf(os.Stderr, "Invalid --heartbeat-rate-hz: %d\n", *rate)
		return opts, parseError
	}

	opts.name = *name
	opts.nodeID = uint16(*nodeID)
	opts.ifaceAddress = *iface
	opts.heartbeatRateHz = uint32(*rate)
	return opts, parseSuccess
}

func makeNatural16Value(value uint16) regpkg.Value_1_0 {
	out := regpkg.Value_1_0{Tag: valueTagNatural16}
	out.Natural16 = arraypkg.Natural16_1_0{Value: []uint16{value}}
	return out
}

func makeNatural32Value(value uint32) regpkg.Value_1_0 {
	out := regpkg.Value_1_0{Tag: valueTagNatural32}
	out.Natural32 = arraypkg.Natural32_1_0{Value: []uint32{value}}
	return out
}

func makeStringValue(value string) regpkg.Value_1_0 {
	out := regpkg.Value_1_0{Tag: valueTagString}
	out.String = primpkg.String_1_0{Value: []uint8(value)}
	return out
}

func makeEmptyValue() regpkg.Value_1_0 {
	return regpkg.Value_1_0{Tag: valueTagEmpty}
}

func exportRegisterValue(entry registerEntry) regpkg.Value_1_0 {
	switch entry.kind {
	case registerNatural16:
		return makeNatural16Value(entry.natural16)
	case registerNatural32:
		return makeNatural32Value(entry.natural32)
	case registerString:
		return makeStringValue(entry.stringVal)
	default:
		return makeEmptyValue()
	}
}

func extractSingleUnsigned(value regpkg.Value_1_0) (uint64, bool) {
	switch value.Tag {
	case valueTagNatural8:
		if len(value.Natural8.Value) > 0 {
			return uint64(value.Natural8.Value[0]), true
		}
	case valueTagNatural16:
		if len(value.Natural16.Value) > 0 {
			return uint64(value.Natural16.Value[0]), true
		}
	case valueTagNatural32:
		if len(value.Natural32.Value) > 0 {
			return uint64(value.Natural32.Value[0]), true
		}
	case valueTagNatural64:
		if len(value.Natural64.Value) > 0 {
			return uint64(value.Natural64.Value[0]), true
		}
	case valueTagInteger8:
		if len(value.Integer8.Value) > 0 && value.Integer8.Value[0] >= 0 {
			return uint64(value.Integer8.Value[0]), true
		}
	case valueTagInteger16:
		if len(value.Integer16.Value) > 0 && value.Integer16.Value[0] >= 0 {
			return uint64(value.Integer16.Value[0]), true
		}
	case valueTagInteger32:
		if len(value.Integer32.Value) > 0 && value.Integer32.Value[0] >= 0 {
			return uint64(value.Integer32.Value[0]), true
		}
	case valueTagInteger64:
		if len(value.Integer64.Value) > 0 && value.Integer64.Value[0] >= 0 {
			return uint64(value.Integer64.Value[0]), true
		}
	}
	return 0, false
}

func applyRegisterWrite(entry *registerEntry, value regpkg.Value_1_0) bool {
	if value.Tag == valueTagEmpty {
		return false
	}

	switch entry.kind {
	case registerNatural16:
		parsed, ok := extractSingleUnsigned(value)
		if !ok {
			return false
		}
		entry.natural16 = uint16(minU64(parsed, math.MaxUint16))
		return true
	case registerNatural32:
		parsed, ok := extractSingleUnsigned(value)
		if !ok {
			return false
		}
		entry.natural32 = uint32(minU64(parsed, math.MaxUint32))
		return true
	case registerString:
		if value.Tag != valueTagString {
			return false
		}
		entry.stringVal = string(value.String.Value)
		return true
	default:
		return false
	}
}

func minU64(lhs, rhs uint64) uint64 {
	if lhs < rhs {
		return lhs
	}
	return rhs
}

func findRegister(registers []registerEntry, name string) *registerEntry {
	for idx := range registers {
		if registers[idx].name == name {
			return &registers[idx]
		}
	}
	return nil
}

func initializeRegisters(app *nodeApp) {
	app.registers = []registerEntry{
		{
			name:       "uavcan.node.id",
			kind:       registerNatural16,
			mutable:    true,
			persistent: true,
			natural16:  app.opts.nodeID,
		},
		{
			name:       "uavcan.node.description",
			kind:       registerString,
			mutable:    true,
			persistent: true,
			stringVal:  "llvm-dsdl native register demo node",
		},
		{
			name:       "uavcan.udp.iface",
			kind:       registerString,
			mutable:    true,
			persistent: true,
			stringVal:  app.opts.ifaceAddress,
		},
		{
			name:       "demo.rate_hz",
			kind:       registerNatural32,
			mutable:    true,
			persistent: true,
			natural32:  app.opts.heartbeatRateHz,
		},
		{
			name:       "demo.counter",
			kind:       registerNatural32,
			mutable:    true,
			persistent: false,
			natural32:  0,
		},
		{
			name:       "sys.version",
			kind:       registerString,
			mutable:    false,
			persistent: true,
			stringVal:  "0.1.0-demo",
		},
	}
}

func updateHeartbeatPeriodFromRegisters(app *nodeApp) {
	rate := findRegister(app.registers, "demo.rate_hz")
	if rate == nil || rate.kind != registerNatural32 {
		app.heartbeatPeriodUsec = 1_000_000
		return
	}
	hz := rate.natural32
	if hz == 0 {
		hz = 1
	}
	period := uint64(1_000_000 / hz)
	if period == 0 {
		period = 1
	}
	app.heartbeatPeriodUsec = period
}

func serializeMessage(serializer interface{ Serialize([]byte) (int8, int) }, capacity int) ([]byte, error) {
	buffer := make([]byte, capacity)
	rc, size := serializer.Serialize(buffer)
	if rc < 0 {
		return nil, fmt.Errorf("serialize failed: rc=%d", int(rc))
	}
	if size < 0 || size > len(buffer) {
		return nil, fmt.Errorf("serialize produced invalid size: %d", size)
	}
	return buffer[:size], nil
}

func deserializeMessage(deserializer interface{ Deserialize([]byte) (int8, int) }, payload []byte) error {
	rc, _ := deserializer.Deserialize(payload)
	if rc < 0 {
		return fmt.Errorf("deserialize failed: rc=%d", int(rc))
	}
	return nil
}

func (app *nodeApp) sendRPCResponse(serviceID uint16, destinationNodeID uint16, transferID uint8, payload []byte) error {
	deadline := uint64(C.go_demo_now_usec()) + txDeadlineUsec
	priority := uint8(C.go_demo_priority_nominal())

	var ptr *C.uint8_t
	if len(payload) > 0 {
		ptr = (*C.uint8_t)(unsafe.Pointer(&payload[0]))
	}

	rc := int(C.go_demo_node_respond(
		app.node,
		C.uint16_t(serviceID),
		C.uint16_t(destinationNodeID),
		C.uint8_t(transferID),
		ptr,
		C.size_t(len(payload)),
		C.uint64_t(deadline),
		C.uint8_t(priority),
	))
	if rc < 0 {
		return fmt.Errorf("go_demo_node_respond failed: %d", rc)
	}
	return nil
}

func (app *nodeApp) publishHeartbeat(nowUsec uint64) error {
	counter := findRegister(app.registers, "demo.counter")
	if counter != nil {
		counter.natural32 = app.heartbeatCounter
	}
	app.heartbeatCounter++

	uptimeSeconds := uint64(0)
	if nowUsec > app.startedAtUsec {
		uptimeSeconds = (nowUsec - app.startedAtUsec) / 1_000_000
	}

	heartbeat := nodepkg.Heartbeat_1_0{
		Uptime: uint32(minU64(uptimeSeconds, math.MaxUint32)),
		Health: nodepkg.Health_1_0{
			Value: nodepkg.HEALTH_1_0_NOMINAL,
		},
		Mode: nodepkg.Mode_1_0{
			Value: nodepkg.MODE_1_0_OPERATIONAL,
		},
		VendorSpecificStatusCode: uint8(app.heartbeatCounter & 0xFF),
	}

	encoded, err := serializeMessage(&heartbeat, nodepkg.HEARTBEAT_1_0_SERIALIZATION_BUFFER_SIZE_BYTES)
	if err != nil {
		return err
	}

	deadline := nowUsec + txDeadlineUsec
	priority := uint8(C.go_demo_priority_nominal())
	rc := int(C.go_demo_node_publish(
		app.node,
		C.uint16_t(subjectHeartbeat),
		C.uint8_t(app.heartbeatTransferID),
		(*C.uint8_t)(unsafe.Pointer(&encoded[0])),
		C.size_t(len(encoded)),
		C.uint64_t(deadline),
		C.uint8_t(priority),
	))
	if rc < 0 {
		return fmt.Errorf("go_demo_node_publish failed: %d", rc)
	}

	app.heartbeatTransferID++
	return nil
}

func (app *nodeApp) handleRegisterListRequest(sourceNodeID uint16, transferID uint8, payload []byte) error {
	request := regpkg.List_1_0_Request{}
	if err := deserializeMessage(&request, payload); err != nil {
		return err
	}

	response := regpkg.List_1_0_Response{}
	if int(request.Index) < len(app.registers) {
		response.Name = regpkg.Name_1_0{Name: []uint8(app.registers[int(request.Index)].name)}
	} else {
		response.Name = regpkg.Name_1_0{Name: []uint8{}}
	}

	encoded, err := serializeMessage(&response, regpkg.LIST_1_0_RESPONSE_SERIALIZATION_BUFFER_SIZE_BYTES)
	if err != nil {
		return err
	}
	return app.sendRPCResponse(serviceRegisterList, sourceNodeID, transferID, encoded)
}

func (app *nodeApp) handleRegisterAccessRequest(sourceNodeID uint16, transferID uint8, payload []byte) error {
	request := regpkg.Access_1_0_Request{}
	if err := deserializeMessage(&request, payload); err != nil {
		return err
	}

	requestedName := string(request.Name.Name)
	entry := findRegister(app.registers, requestedName)

	response := regpkg.Access_1_0_Response{}
	response.Timestamp = timepkg.SynchronizedTimestamp_1_0{Microsecond: timepkg.SYNCHRONIZED_TIMESTAMP_1_0_UNKNOWN}

	if entry == nil {
		response.Mutable = false
		response.Persistent = false
		response.Value = makeEmptyValue()
	} else {
		if request.Value.Tag != valueTagEmpty && entry.mutable {
			_ = applyRegisterWrite(entry, request.Value)
			if entry.name == "demo.rate_hz" {
				updateHeartbeatPeriodFromRegisters(app)
			}
		}
		response.Mutable = entry.mutable
		response.Persistent = entry.persistent
		response.Value = exportRegisterValue(*entry)
	}

	encoded, err := serializeMessage(&response, regpkg.ACCESS_1_0_RESPONSE_SERIALIZATION_BUFFER_SIZE_BYTES)
	if err != nil {
		return err
	}
	return app.sendRPCResponse(serviceRegisterAccess, sourceNodeID, transferID, encoded)
}

func (app *nodeApp) initialize() error {
	cIface := C.CString(app.opts.ifaceAddress)
	defer C.free(unsafe.Pointer(cIface))

	app.node = C.go_demo_node_create()
	if app.node == nil {
		return errors.New("go_demo_node_create failed")
	}

	rc := int(C.go_demo_node_init(
		app.node,
		C.uint16_t(app.opts.nodeID),
		cIface,
		C.uint16_t(serviceRegisterAccess),
		C.size_t(regpkg.ACCESS_1_0_REQUEST_EXTENT_BYTES),
		C.uint16_t(serviceRegisterList),
		C.size_t(regpkg.LIST_1_0_REQUEST_EXTENT_BYTES),
		C.uint32_t(txQueueCapacity),
		C.size_t(rxDatagramCapacity),
	))
	if rc < 0 {
		return fmt.Errorf("go_demo_node_init failed: %d", rc)
	}

	initializeRegisters(app)
	updateHeartbeatPeriodFromRegisters(app)
	app.startedAtUsec = uint64(C.go_demo_now_usec())
	app.nextHeartbeatAt = app.startedAtUsec + app.heartbeatPeriodUsec

	var endpointIP C.uint32_t
	var endpointPort C.uint16_t
	if int(C.go_demo_node_get_rpc_endpoint(app.node, &endpointIP, &endpointPort)) < 0 {
		return errors.New("go_demo_node_get_rpc_endpoint failed")
	}

	fmt.Fprintf(os.Stderr,
		"[%s] started node_id=%d iface=%s heartbeat_hz=%d rpc_group=0x%08x:%d\n",
		app.opts.name,
		app.opts.nodeID,
		app.opts.ifaceAddress,
		app.opts.heartbeatRateHz,
		uint32(endpointIP),
		uint16(endpointPort))
	return nil
}

func (app *nodeApp) shutdown() {
	if app.node == nil {
		return
	}
	_ = C.go_demo_node_shutdown(app.node)
	C.go_demo_node_destroy(app.node)
	app.node = nil
	fmt.Fprintf(os.Stderr, "[%s] stopped\n", app.opts.name)
}

func main() {
	opts, parseRC := parseOptions(os.Args)
	switch parseRC {
	case parseHelp:
		os.Exit(0)
	case parseError:
		printUsage(os.Args[0])
		os.Exit(1)
	case parseSuccess:
	}

	app := &nodeApp{opts: opts}
	if err := app.initialize(); err != nil {
		fmt.Fprintf(os.Stderr, "[%s] %v\n", app.opts.name, err)
		app.shutdown()
		os.Exit(1)
	}
	defer app.shutdown()

	stopSignals := make(chan os.Signal, 2)
	signal.Notify(stopSignals, os.Interrupt, syscall.SIGTERM)
	defer signal.Stop(stopSignals)

	stopping := false
	for !stopping {
		select {
		case <-stopSignals:
			stopping = true
			continue
		default:
		}

		now := uint64(C.go_demo_now_usec())
		if now >= app.nextHeartbeatAt {
			if err := app.publishHeartbeat(now); err != nil {
				fmt.Fprintf(os.Stderr, "[%s] heartbeat publish failed: %v\n", app.opts.name, err)
				break
			}
			for app.nextHeartbeatAt <= now {
				app.nextHeartbeatAt += app.heartbeatPeriodUsec
			}
		}

		C.go_demo_node_pump_tx(app.node)

		nowAfterTx := uint64(C.go_demo_now_usec())
		untilNextHeartbeat := app.heartbeatPeriodUsec
		if app.nextHeartbeatAt > nowAfterTx {
			untilNextHeartbeat = app.nextHeartbeatAt - nowAfterTx
		}
		timeoutUsec := untilNextHeartbeat
		if timeoutUsec > maxWaitUsec {
			timeoutUsec = maxWaitUsec
		}
		if timeoutUsec == 0 {
			timeoutUsec = 1
		}

		var transfer C.GoDemoRpcTransfer
		pollRC := int(C.go_demo_node_poll_rpc(app.node, C.uint64_t(timeoutUsec), &transfer))
		if pollRC < 0 {
			if pollRC == -int(syscall.EINTR) {
				continue
			}
			fmt.Fprintf(os.Stderr, "[%s] go_demo_node_poll_rpc failed: %d\n", app.opts.name, pollRC)
			break
		}
		if pollRC == 0 {
			continue
		}

		payload := C.GoBytes(unsafe.Pointer(transfer.payload), C.int(transfer.payload_size))
		serviceID := uint16(transfer.service_id)
		sourceNodeID := uint16(transfer.source_node_id)
		transferID := uint8(transfer.transfer_id)
		C.go_demo_node_release_transfer(&transfer)

		var err error
		switch serviceID {
		case serviceRegisterList:
			err = app.handleRegisterListRequest(sourceNodeID, transferID, payload)
		case serviceRegisterAccess:
			err = app.handleRegisterAccessRequest(sourceNodeID, transferID, payload)
		default:
			err = nil
		}
		if err != nil {
			fmt.Fprintf(os.Stderr, "[%s] rpc handling failed: %v\n", app.opts.name, err)
		}
	}
}
