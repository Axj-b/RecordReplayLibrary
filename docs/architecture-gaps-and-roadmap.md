# Architecture Gaps and Roadmap

This document captures important architecture decisions that are often missed in v1 systems and proposes concrete defaults for `RecordReplayLibrary`.

## Implementation Status (Current)

- [x] Reader rejects unsupported major format versions.
- [x] Public thread-safety contract notes added to `RecorderSession`, `ReaderSession`, and `Splitter`.
- [x] Deterministic merge ordering for equal timestamps is regression-tested.
- [x] Manifest writes now use temp-file replace with rename-first/copy-overwrite fallback.
- [ ] Compatibility golden-file matrix across historical versions.
- [ ] Crash-injection durability tests and stronger fsync mode.
- [ ] Fuzzing targets and parser hard limits.
- [ ] Structured diagnostics callback and benchmark harness.

## 1. Compatibility Policy

Current gap:

- format is versioned, but compatibility guarantees are not fully specified

Default decisions:

- major version change means potential breaking format changes
- minor version change must remain backward readable
- patch version must remain wire-compatible
- unknown record opcodes must be safely skippable where possible

Roadmap:

- define a "format compatibility contract" section in public docs
- add compatibility tests with golden `.rec` fixtures from prior versions
- add explicit reader behavior for unknown future fields/ops

## 2. Durability and Crash Semantics

Current gap:

- close/recover behavior exists, but crash guarantees are not explicitly documented

Default decisions:

- "clean close" is the only state that guarantees a complete footer/index
- interrupted sessions are recoverable best-effort, not lossless-guaranteed
- manifest updates should use write-temp + atomic replace pattern

Roadmap:

- document exact guarantees for `Open`, `Write`, `Flush`, `Close`, `Recover`
- add optional durability mode with stronger fsync/flush behavior
- add crash-injection tests to verify recovery boundaries

## 3. Threading Model

Current gap:

- session APIs are used as single-writer/single-reader objects, but contract is implicit

Default decisions:

- `RecorderSession` and `ReaderSession` are not thread-safe for concurrent calls
- multi-source ingestion must serialize into one recorder call path externally

Roadmap:

- document thread-safety guarantees and non-guarantees in headers
- add an optional `RecorderSessionMt` design proposal for internal queueing
- provide example ingestion adapter for multi-producer pipelines

## 4. Time Semantics

Current gap:

- timestamps are user-provided, but monotonicity/order and clock-source semantics are not formalized

Default decisions:

- timestamps are accepted as-is and can be non-monotonic across channels
- per-channel write order is preserved
- merge/split ordering ties on equal timestamps use stable source iteration order

Roadmap:

- define supported clock-source guidance (UTC wall, monotonic, hardware)
- document deterministic ordering for equal timestamp messages
- add test cases for non-monotonic and equal-timestamp scenarios

## 5. Security Hardening for Untrusted Files

Current gap:

- parser paths are defensive in places but not governed by a formal hardening policy

Default decisions:

- all length fields must be bounds-checked before memory access
- malformed records must fail safely without undefined behavior
- no dynamic allocation based on unbounded attacker-controlled sizes

Roadmap:

- add parser fuzzing targets for reader/validate/recover paths
- enforce maximum payload/index limits in config and reader
- add a security checklist to PR workflow for format/parsing changes

## 6. Observability and Operations

Current gap:

- status codes and counters exist, but no unified telemetry schema

Default decisions:

- expose counters for write drops, corruption count, per-channel message totals
- classify failures by operation phase (open/write/flush/rotate/close)

Roadmap:

- add structured diagnostics callback interface (optional)
- define metric names and units in docs
- emit reasoned rotate events (size, duration, manual, flush-pressure)

## 7. Performance Envelope

Current gap:

- no formal SLO/benchmark targets are documented

Default decisions:

- benchmark dimensions: sustained write throughput, read throughput, seek latency, memory peak
- test with single large channel and high-channel fanout profiles

Roadmap:

- add reproducible benchmark harness and baseline hardware profile
- track results per release
- set acceptance thresholds for regressions in CI

## 8. Data Governance and Compliance

Current gap:

- retention/privacy/encryption story is outside core architecture today

Default decisions:

- core library remains payload-agnostic and unencrypted by default
- governance controls are layered externally or via optional extension points

Roadmap:

- define extension hooks for encryption-at-rest and record signing
- define redaction/export tooling requirements
- document operational retention patterns (segment pruning, archival tiers)

## Suggested Milestones

Milestone A (short-term):

- compatibility contract + thread-safety docs
- deterministic ordering docs/tests
- crash semantics docs

Milestone B (mid-term):

- fuzzing and parser limit enforcement
- manifest atomic-write hardening
- structured diagnostics callback

Milestone C (long-term):

- durability mode options
- benchmark gating in CI
- optional governance extensions (sign/encrypt/redact)

## Acceptance Criteria for "Architecture Complete v1"

- compatibility policy is published and test-backed
- crash and durability semantics are explicitly documented
- thread-safety contract is explicit in API docs
- untrusted-file safety has fuzz coverage and bounded allocations
- core metrics and failure taxonomy are exposed/documented
