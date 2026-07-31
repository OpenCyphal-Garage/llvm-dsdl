# LSP Index Schema

## Purpose

`dsdld` persists per-file index shards for warm-start workspace indexing and
`workspace/symbol` queries.

Schema version: `1` (`LspIndexSchemaVersion`).

## File Naming

Each source file maps to one shard file in the configured index cache directory.
The source path is encoded into a filesystem-safe key and written as:

- `<encoded-source-path>.index.json`

## JSON Structure

Each shard is a JSON object:

```json
{
  "schema_version": 1,
  "metadata": {
    "file_path": "/abs/path/TypeA.1.0.dsdl",
    "source_uri": "file:///abs/path/TypeA.1.0.dsdl",
    "text_hash": 123456789,
    "snapshot_version": 42
  },
  "symbols": [
    {
      "usr": "type:demo.TypeA.1.0",
      "name": "TypeA",
      "qualified_name": "demo.TypeA.1.0",
      "container_name": "demo",
      "detail": "message",
      "kind": 23,
      "file_path": "/abs/path/TypeA.1.0.dsdl",
      "location": {
        "uri": "file:///abs/path/TypeA.1.0.dsdl",
        "line": 0,
        "character": 0,
        "length": 5
      }
    }
  ],
  "references": [
    {
      "target_usr": "type:demo.TypeA.1.0",
      "file_path": "/abs/path/TypeA.1.0.dsdl",
      "is_declaration": true,
      "location": {
        "uri": "file:///abs/path/TypeA.1.0.dsdl",
        "line": 0,
        "character": 0,
        "length": 5
      }
    }
  ]
}
```

## Schema Guarantees

1. `schema_version` is required and must match the runtime schema constant.
2. Serialization is deterministic for identical shard content.
3. Symbol/reference rows are sorted before persistence.
4. Duplicate logical shards (same `metadata.file_path`) are considered invalid.
5. Invalid shards are reported by verification and can be removed by repair.

## Compatibility Strategy

1. Any schema change increments `schema_version`.
2. Older schema versions are rejected by the current runtime.
3. Unsupported shard files are treated as invalid cache entries.
4. Verification/repair can remove unsupported shards for clean warm-starts.
