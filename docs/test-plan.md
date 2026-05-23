# Test plan

This repository uses a small dependency-free C test executable (`loxseq_tests`)
driven by `ctest`.

## Categories

### API argument validation
- invalid `loxseq_init()` arguments

### Storage / record integrity
- storage read failure -> `LOXSEQ_RECOVERY_COLD_START`
- corrupted CRC -> `LOXSEQ_RECOVERY_COLD_START`
- unsupported record version -> `LOXSEQ_RECOVERY_COLD_START`
- out-of-range step index -> `LOXSEQ_RECOVERY_COLD_START` (recover) / `LOXSEQ_ERR_FORMAT` (start)
- write failure on step transition -> `LOXSEQ_ERR_STORAGE` and failed state
- erase failure on complete -> `LOXSEQ_ERR_STORAGE`

### Recovery start semantics
- RESUME preserves `step_entered_at_ms`
- RESTART resets `step_entered_at_ms` to the supplied `now_ms`
- operator resolve with `LOXSEQ_RECOVERY_RESTART` resets `step_entered_at_ms`
- `last_recovery` matches the last returned verdict

### Sequencer behavior
- branching: missing target fails; valid branch jumps steps
- precondition gating: action not called, step not advanced
- timeout: `LOXSEQ_ERR_TIMEOUT` and failed state
- pause/resume: tick is a no-op while paused
- abort: sets failed state
- completion: reaches complete state and erases checkpoint

### Time model edge cases
- `loxseq_step_age_ms()` wraparound behavior for `uint32_t`

### Record layout assumptions (hosted builds)
- compile-time checks for `loxseq_record_t` size and key field offsets

