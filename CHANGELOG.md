# Changelog

## [Unreleased]

### Added
- `loxseq_start_restart()` for `LOXSEQ_RECOVERY_RESTART` start semantics.
- CMake options: `LOXSEQ_BUILD_TESTS`, `LOXSEQ_BUILD_EXAMPLES`, `LOXSEQ_WARNINGS_AS_ERRORS`.
- CMake alias target: `loxseq::loxseq`.
- CMake package export for `find_package(loxseq CONFIG REQUIRED)`.
- Docs: `docs/test-plan.md`, `docs/release-checklist.md`, `docs/evidence-matrix.md`.
- Public API (include/loxseq/loxseq.h).
- Implementation (src/loxseq.c).
- CMake build with examples + tests.
- CI workflow (.github/workflows/ci.yml).
- Docs: resume model, storage contract, integration, limitations.
- Examples: examples/minimal.c, examples/batch_reactor.c.
- On-disk record format v1 with CRC-16/CCITT-FALSE.

### Fixed
- `LOXSEQ_RECOVERY_RESTART` public start path now resets `step_entered_at_ms`.
- `loxseq_recover()` updates `seq->last_recovery`.
- Reboot counter saturation logic uses the checkpoint record value.
- Unit tests no longer rely on `assert()` and now cover storage and recovery fault cases.
- Documentation and examples corrected to use `loxseq_start_restart()` for RESTART.

### Not yet
- libFuzzer harness over storage read paths.
- Cortex-M0+ memory profile with 32-step sequence.
- Step-table hash in record for OTA detection.
- Periodic in-step checkpoint with rate cap.
