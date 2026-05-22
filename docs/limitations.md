# loxseq — Limitations and non-goals

## Not a safety library

`loxseq` is not safety-rated. No SIL, no IEC 61511, no ISO 26262.

A power-loss-aware sequencer is part of a larger safety case that
includes mechanical interlocks, alarm rationalisation, operator
training, and validated emergency procedures. `loxseq` is one
mechanism; it does not constitute a safety system.

## Not a recipe / batch manager

`loxseq` runs the one sequence you configure. There is no recipe
selection, parameter scaling, ingredient management, or batch ID
tracking. Those live above `loxseq`.

## Not a graph FSM

Steps are an ordered array. `set_next_step` allows branching, but the
underlying model is linear with jumps, not a general graph. For
arbitrary graphs, use `microfsm` (and consider whether `loxseq` adds
value over it for your case).

## Single sequence per instance

One `loxseq_t` runs one sequence. For parallel coordinated sequences,
instantiate multiple. There is no built-in synchronisation primitive
between them; use `microbus` for events or share storage via the
application.

## Resume policies are advisory

`loxseq` honours the declared policy, with the documented downgrade
table for crash reboots. It cannot verify that your action is actually
idempotent. If you declare `RESUME_AT_STEP` for a non-idempotent
action, the library will resume it; the consequences are on you.

## No clock recovery

`loxseq` does not know how long the outage was. After resume,
`step_entered_at_ms` is from the previous power session.
`loxseq_step_age_ms(now_ms)` returns `now_ms - step_entered_at_ms`,
which can yield arbitrary values across reboots.

If your step has a real-time timeout and you need it to hold across
reboots, store wall-clock time in the record's `flags`/reserved fields
(via a future extension) or maintain it in your application's own
NV state.

## Storage hooks are caller's responsibility

If the storage hook lies (claims a write succeeded when it did not),
`loxseq` cannot detect it until the next read. The CRC catches
corruption but not data loss. Choose your backend carefully.

## Record format is v1

Future versions of `loxseq` may extend the record. Forward compatibility
is intended (version byte refuses unknown formats), but not yet
exercised. Do not assume that v0.1 records will be readable by
arbitrary future versions until that contract is explicit.

## CRC is CRC-16/CCITT-FALSE

Single-error detection is robust, but for very long records (we are
not), or for adversarial corruption (we are not protecting against
that), a stronger checksum would be appropriate. The 16-bit CRC is a
trade for record size on flash.

## OTA reboot complication

If an OTA updates the firmware and the new firmware has a different
step layout (added, removed, or re-ordered steps), the saved record's
`step_index` no longer means the same thing. The library does not
detect this.

Mitigations:
- Include a step-table hash in the record (planned for v0.2).
- Force `LOXSEQ_RECOVERY_SAFE_INIT` after every OTA (current downgrade
  table does this).
- Track firmware version explicitly in your application's storage.

## Clock requirements

Same as the rest of the Lox family: caller-supplied monotonic
`uint32_t` ms. Wrap is handled. Single-step timeout maximum is half
the wrap window (~24.85 days).

## Re-entrancy

Two threads on different sequencers: safe.
Two threads on the same sequencer: requires external synchronisation.
The library has no internal mutex.

## Known TODOs before v1.0

- Implementation.
- Test suite (sequence runners, recovery scenarios, CRC corruption,
  storage backend faults).
- libFuzzer harness over storage read paths.
- Memory profile on Cortex-M0+ with 32-step sequence.
- Step-table hash in record for OTA detection.
- Periodic in-step checkpoint with rate cap.
- Documentation of safe-init step convention (currently informal:
  application reserves a step or exposes a separate sequence).
