# Data Format Architecture

## Binary File Contract

The canonical binary contract is defined in `include/recplay/format.hpp`.

Format properties:

- little-endian encoding for all multi-byte numeric fields
- packed structs for fixed-layout records
- variable payload sections for record-specific bodies

## `.rec` Segment Layout

```text
[FileHeader: 64]
[RecordEnvelope + Payload] repeated N times
[FileFooter: 32]
```

## Fixed Structures

- `FileHeader` (64 bytes)
  - magic
  - version
  - session id
  - segment index
  - created timestamp
  - flags
- `RecordEnvelope` (20 bytes)
  - op
  - channel id
  - flags
  - payload length
  - timestamp
  - crc32
- `ChunkHeader` (9 bytes)
  - codec
  - uncompressed length
  - packed record count
- `IndexEntry` (18 bytes)
  - channel
  - timestamp
  - file offset
- `FileFooter` (32 bytes)
  - magic
  - index offset
  - record count
  - footer crc

## Record Operations

- `SessionStart`
- `ChannelDef`
- `Data`
- `Chunk`
- `Index`
- `Annotation`
- `SessionEnd`

## Variable Payload Contracts

### `CHANNEL_DEF`

```text
[channel_id:uint16]
[layer:uint8]
[codec:uint8]
[chunk_bytes:uint32]
[flush_ms:uint32]
[name_len:uint16][name]
[schema_len:uint16][schema]
[metadata_len:uint32][metadata]
```

### `ANNOTATION`

```text
[label_len:uint16][label]
[metadata_len:uint32][metadata]
```

### `CHUNK`

Payload starts with `ChunkHeader`, followed by compressed bytes.

The uncompressed chunk body is:

```text
repeated record_count times:
  [timestamp_ns:uint64]
  [payload_len:uint32]
  [payload_bytes]
```

## Session Storage Modes

Directory mode:

- output is `session_dir/segment_*.rec`
- sidecar `session.manifest` tracks channels and segments

Single-file mode:

- output is one `<name>.rec`
- no sidecar manifest required
- reader reconstructs manifest-like metadata from embedded records

## Integrity Model

- per-record CRC (optional; controlled by session config)
- footer CRC (always written in current implementation)
- validation cross-checks record count and structure bounds

## Indexing Model

- writer emits `INDEX` record near segment close
- payload is `[IndexHeader][IndexEntry * N]`
- reader loads per-segment seek index and performs channel-aware offset lookup
