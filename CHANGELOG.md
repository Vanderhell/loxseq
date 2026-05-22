# Changelog

## [Unreleased]

### Added
- Public API (include/loxseq/loxseq.h).
- Implementation (src/loxseq.c).
- CMake build with examples + tests.
- CI workflow (.github/workflows/ci.yml).
- Docs: resume model, storage contract, integration, limitations.
- Examples: examples/minimal.c, examples/batch_reactor.c.
- On-disk record format v1 with CRC-16/CCITT-FALSE.

### Not yet
- libFuzzer harness over storage read paths.
- Cortex-M0+ memory profile with 32-step sequence.
- Step-table hash in record for OTA detection.
- Periodic in-step checkpoint with rate cap.