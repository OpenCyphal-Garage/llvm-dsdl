# dsdld structured logging

`dsdld` emits structured, level-gated records so an operator can reconstruct what the server was doing
after the fact — "which request was in flight when the editor stopped responding?"

## Destination

**stderr, one JSON object per line.** stdout carries the JSON-RPC frames and is never interleaved with
log text; the logger serialises writes, so lines stay intact even though records originate from both the
main thread and the request-scheduler worker.

Capture them the way you capture any stderr:

```console
$ dsdld 2> dsdld.log
```

Most editors surface a language server's stderr in an output/log panel.

## Verbosity

Controlled by the standard LSP trace value — there is no bespoke knob:

| Trace | Emits |
| --- | --- |
| `off` | nothing |
| `messages` (default) | `error` / `warn` / `info` — the request-level backbone |
| `verbose` | the above plus `debug` (configuration changes and other high-volume detail) |

Set it three ways, each taking effect immediately:

- `initialize` params: `{"trace": "verbose"}` — applied before the handshake is answered, so the
  handshake itself is logged at the requested level.
- The `$/setTrace` notification: `{"value": "off"}` — the protocol's own runtime control.
- The `trace` workspace setting via `workspace/didChangeConfiguration`.

## Record shape

Every record carries `event` and `level`; the rest is event-specific.

```json
{"event":"request","latency_us":196,"level":"info","method":"initialize","outcome":"ok"}
{"event":"request","latency_us":91240,"level":"info","method":"textDocument/completion","outcome":"cancelled"}
{"code":-32601,"event":"error_response","level":"warn","message":"method not found: nope/bogus"}
{"event":"set_trace","level":"info","value":"verbose"}
{"applied":true,"event":"configuration_changed","level":"debug"}
```

| Event | Level | Meaning |
| --- | --- | --- |
| `request` | info | One record per request — **synchronous and scheduler-completed alike**, because every request funnels through a single telemetry choke point. Carries `method`, `latency_us`, and `outcome` (`ok` or `cancelled`). |
| `error_response` | warn | A request was answered with a JSON-RPC error. Cancellations are routine flow control and stay at `info`. |
| `set_trace` | info | Verbosity changed at runtime. |
| `configuration_changed` | debug | `workspace/didChangeConfiguration` was applied. |

## Deliberate omissions

Records carry **no document text and no secrets** — method names, latencies, and error codes only. The
AI surface's separate audit log applies its own redaction (see `ai-operation.md`); this channel
avoids the problem by never carrying payloads in the first place.

Omitting the log sink entirely (the library default when embedding `lsp::Server`) makes the server
silent regardless of trace level, and the gate short-circuits before any JSON is built, so `off` costs
next to nothing.
