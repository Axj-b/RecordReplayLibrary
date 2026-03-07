# RecordReplayLibrary TODO

- [x] Fix merge behavior for `MergeDuplicateChannelNames=true` so duplicate-name channels are mapped and preserved.
- [x] Add compressed-channel rotation handling and propagate chunk flush failures.
- [x] Validate payload pointers (`length > 0` requires non-null data) for writes and annotations.
- [x] Prevent `ReaderSession::Seek` crashes when no segments are readable.
- [x] Propagate `Status` failures in `Splitter::Split` and `Splitter::Merge`.
- [x] Make reader message counters report total session counts, not only consumed counts.
- [x] Make recovery replacement logic work on Windows when replacing existing files.
- [x] Implement `SplitOptions::OneDirPerChannel` and support `{original_session_name}` in split patterns.
- [x] Add regression tests for the above fixes.
