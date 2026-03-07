# Runtime Flow Architecture

## Recording Flow

```mermaid
sequenceDiagram
    participant Client
    participant Recorder as RecorderSession
    participant Acc as ChunkAccumulator
    participant File as MappedFile
    participant Index as IndexBuilder

    Client->>Recorder: Open(SessionConfig)
    Recorder->>File: OpenWrite(...)
    Recorder->>File: Write FileHeader + SessionStart

    Client->>Recorder: DefineChannel(ChannelConfig)
    Recorder->>File: Write CHANNEL_DEF

    Client->>Recorder: Write(channel, ts, payload)
    alt uncompressed
        Recorder->>Recorder: rotation check
        Recorder->>File: Write DATA record
        Recorder->>Index: MaybeAdd(channel, ts, offset)
    else compressed
        Recorder->>Acc: Push(ts, payload)
        Acc-->>Recorder: OnFlush(payload, unc_bytes, rec_count)
        Recorder->>Recorder: rotation check / rotate if needed
        Recorder->>File: Write CHUNK record
        Recorder->>Index: MaybeAdd(channel, first_ts, offset)
    end

    Client->>Recorder: Close()
    Recorder->>Acc: FlushAll
    Recorder->>File: Write INDEX, SESSION_END, footer
    Recorder->>File: Close + truncate
    Recorder->>Recorder: Write manifest (directory mode)
```

## Read Flow

```mermaid
sequenceDiagram
    participant Client
    participant Reader as ReaderSession
    participant Seg as SegmentData
    participant Seek as SeekIndex

    Client->>Reader: Open(path)
    alt path is single .rec
        Reader->>Seg: map one file
        Reader->>Reader: scan records for channels, index, annotations
    else session directory
        Reader->>Reader: read session.manifest
        Reader->>Seg: map listed segment files
        Reader->>Reader: scan each readable segment
    end

    Client->>Reader: Seek(targetNs)
    Reader->>Seek: Find(channel, targetNs) per channel
    Reader->>Reader: pick earliest usable offset

    Client->>Reader: ReadNext(out)
    Reader->>Reader: parse envelope at cursor
    alt DATA
        Reader-->>Client: MessageView(payload ptr)
    else CHUNK
        Reader->>Reader: decompress and iterate inner records
        Reader-->>Client: MessageView(decoded ptr)
    end
```

## Split Flow

```mermaid
sequenceDiagram
    participant User
    participant Split as Splitter::Split
    participant Src as ReaderSession
    participant Dst as RecorderSession[*]

    User->>Split: Split(source, output, options)
    Split->>Src: Open(source)
    Split->>Dst: Open one recorder per selected channel
    Split->>Dst: Define corresponding destination channels

    loop source messages
        Split->>Src: ReadNext()
        Split->>Dst: Write routed message to matching recorder
    end

    opt include annotations
        Split->>Src: Annotations()
        Split->>Dst: Annotate on each destination recorder
    end

    Split->>Dst: Close all outputs
    Split-->>User: SplitResult
```

## Merge Flow

```mermaid
sequenceDiagram
    participant User
    participant Merge as Splitter::Merge
    participant Src as ReaderSession[*]
    participant Dst as RecorderSession

    User->>Merge: Merge(sources, output, options)
    Merge->>Src: Open all sources
    Merge->>Merge: Build merged channel list + source->dest map
    Merge->>Dst: Open destination + DefineChannel

    loop while any source has data
        Merge->>Src: ReadNext head per reader
        Merge->>Merge: pick lowest timestamp head
        Merge->>Dst: Write mapped message
    end

    opt include annotations
        Merge->>Src: collect annotations
        Merge->>Dst: write sorted annotations
    end

    Merge->>Dst: Close
    Merge-->>User: MergeResult
```

## Validation and Recovery Flow

Validation:

1. read manifest
2. for each segment:
   - check header/footer magic
   - verify footer CRC
   - walk records and verify envelope CRCs
   - compare counted records vs footer record count

Recovery:

1. read manifest, or discover `.rec` files when manifest is missing
2. scan each segment to find valid record boundary
3. rebuild index and footer in temporary file
4. replace original file with recovered output
5. rewrite manifest

## Concurrency Model

- `RecorderSession`, `ReaderSession`, and `Splitter` APIs are used in a single-threaded manner per instance in current implementation.
- Example programs may use worker threads, but synchronize access around shared recorder calls explicitly.
