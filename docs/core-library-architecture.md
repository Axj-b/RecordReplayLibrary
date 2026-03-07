# Core Library Architecture

## Public API Surface

The umbrella header is `include/recplay/recplay.hpp`.

Main API types:

- `RecorderSession`: write-side session lifecycle and channel writes
- `ReaderSession`: read-side open/seek/iterate/query APIs
- `Splitter`: split/merge/validate/recover utilities
- `SessionManifest`, `ChannelDef`, `ChannelConfig`, `SessionConfig`
- `Status`, `RecordOp`, codec/layer enums

## Writer Side (`RecorderSession`)

### Main Responsibilities

- open session storage in directory mode or single-file mode
- define channels and emit `CHANNEL_DEF`
- write message records (`DATA`) or buffered chunk records (`CHUNK`)
- write index/footer/manifest on close
- perform rotation by segment size/duration

### Internal State Model

`src/writer.cpp` uses `detail::WriterImpl`, which owns:

- session config and manifest snapshot
- current mapped output file (`MappedFile`)
- segment index builder (`IndexBuilder`)
- per-channel accumulators (`ChunkAccumulator`)
- per-channel first/last buffered timestamps
- message counters and segment timing metadata

### Write Path

Uncompressed channel:

1. validate channel and payload pointer
2. estimate tail overhead and rotate if needed
3. write `RecordEnvelope + payload`
4. update seek index and per-segment timing window

Compressed channel:

1. append message into `ChunkAccumulator`
2. on flush callback:
   - estimate required bytes
   - request rotation (`Status::ErrorFull`) when needed
   - write `CHUNK` envelope + `ChunkHeader` + compressed payload
   - update segment timing window using buffered first/last timestamps
3. retry flush after rotation when requested

### Error Propagation Model

- `WriteRecord` and `WriteChunkRecord` validate null payload pointers when lengths are non-zero
- chunk flush callback now returns `Status` (instead of fire-and-forget)
- `Flush`, `Annotate`, and close paths propagate flush failures

## Reader Side (`ReaderSession`)

### Main Responsibilities

- open single `.rec` or directory-based session
- load channel definitions and segment indexes
- support seek and sequential iteration
- expose annotations and message counts

### Open Modes

- Single-file mode:
  - detect magic directly from provided path
  - scan embedded records to reconstruct manifest fields/channels
- Directory mode:
  - parse `session.manifest`
  - map each listed segment
  - scan for index and annotations
  - fail open if no readable segments remain

### Count Semantics

- total message counts are computed at open-time scan
- counts are no longer "messages consumed so far"
- `TotalMessageCount()` and `MessageCount(ch)` are stable over iteration

## Splitter/Merger (`Splitter`)

### Split

- open source via `ReaderSession`
- open one destination `RecorderSession` per selected channel
- stream messages and route by source channel id
- optionally replicate annotations to each output
- close outputs and summarize bytes/messages

### Merge

- open all source sessions
- build merged channel catalog
- maintain source-channel to merged-channel mapping
- interleave head messages by smallest timestamp
- write into destination recorder

Duplicate channel names:

- if `MergeDuplicateChannelNames == false`, duplicates fail fast
- if enabled, duplicate-name channels map into the same destination channel id

### Validation and Recovery

- `Validate`: checks header/footer magic, CRCs, record count consistency
- `Recover`: rescans records, rebuilds index/footer, rewrites manifest
- replacement uses overwrite copy semantics for Windows compatibility

## Internal Detail Modules (`src/detail`)

- `file_map`: memory-mapped read/write abstraction with growth and truncation
- `chunk_accumulator`: per-channel aggregation + compression + callback flush
- `index_builder`: write-time seek point collection and read-time seek search
- `manifest_io`: lightweight manifest serialization/parsing
- `crc32`: integrity checksum implementation
- `platform`: endian helpers, path separator, portable UTC conversion
