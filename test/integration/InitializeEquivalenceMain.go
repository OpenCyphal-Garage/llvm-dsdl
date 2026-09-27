package main

import (
	"bytes"
	"fmt"
	"os"
	node "uavcan_dsdl_generated/uavcan/node"
	can "uavcan_dsdl_generated/uavcan/metatransport/can"
	array "uavcan_dsdl_generated/uavcan/primitive/array"
	port "uavcan_dsdl_generated/uavcan/node/port"
	pnp "uavcan_dsdl_generated/uavcan/pnp"
	diagnostic "uavcan_dsdl_generated/uavcan/diagnostic"
	uavtime "uavcan_dsdl_generated/uavcan/time"
)

// The default is the zero value; the spec's definition is deserialising nothing over it.
type serdes interface {
	Serialize(buffer []byte) (int, error)
	Deserialize(buffer []byte) (int, error)
}

func check(name string, a serdes, b serdes) bool {
	_, derr := b.Deserialize([]byte{})
	wa := make([]byte, 4096)
	wb := make([]byte, 4096)
	na, aerr := a.Serialize(wa)
	nb, berr := b.Serialize(wb)
	same := derr == nil && aerr == nil && berr == nil && na == nb && bytes.Equal(wa[:na], wb[:nb])
	verdict := "same"
	if !same {
		verdict = "DIFFER"
	}
	fmt.Printf("%-40s %s (%d bytes)\n", name, verdict, na)
	return same
}

func main() {
	ok := true
	{
		var a, b node.Heartbeat
		ok = check("uavcan.node.Heartbeat", &a, &b) && ok
	}
	{
		var a, b can.Frame
		ok = check("uavcan.metatransport.can.Frame", &a, &b) && ok
	}
	{
		var a, b array.Real32
		ok = check("uavcan.primitive.array.Real32", &a, &b) && ok
	}
	{
		var a, b port.SubjectIDList
		ok = check("uavcan.node.port.SubjectIDList", &a, &b) && ok
	}
	{
		var a, b pnp.NodeIDAllocationData
		ok = check("uavcan.pnp.NodeIDAllocationData", &a, &b) && ok
	}
	{
		var a, b diagnostic.Record
		ok = check("uavcan.diagnostic.Record", &a, &b) && ok
	}
	{
		var a, b uavtime.SynchronizedTimestamp
		ok = check("uavcan.time.SynchronizedTimestamp", &a, &b) && ok
	}
	if !ok {
		os.Exit(1)
	}
}
