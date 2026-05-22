# loxseq

Power-loss-aware step sequencer for embedded C firmware.

loxseq is a small, heap-free **C99** library that runs a sequence of named steps,
persistently checkpoints progress, and on reboot computes a **recovery verdict**
from:

- the caller-supplied reboot reason (power loss / watchdog / panic / OTA)
- the **per-step resume policy** (resume / restart / safe-init / operator)

It is designed for devices where **the physical world state cannot be trusted
across a power event** (valves, heaters, motors, fluids, pressure).

## Why loxseq (the problem it solves)

A typical linear  recipe in firmware has two hard questions:

1. *Where were we when power failed?*
2. *Is it safe to continue?* (and if yes: continue how?)

A plain FSM can store a state index, but it usually doesn’t define:

- how to persist it safely (power-loss tolerant writes)
- what to do on boot when the last run died mid-step
- how to downgrade aggressive resume when reboot reason is suspicious

loxseq is the glue between your step table and your storage backend.

## Key features

- **Persistent checkpointing** (fixed-size record with CRC)
- **Per-step recovery policy** with conservative downgrades
- **No heap, no globals, no floats** (caller-owned state)
- **Single call per tick**: you drive time (loxseq_tick(seq, now_ms))
- **Branching support** (loxseq_set_next_step + LOXSEQ_STEP_BRANCH)

## Concepts (read this first)

### Step
A step is a caller-provided definition:

- 	ag: human-readable identifier
- ction: runs from loxseq_tick and returns a loxseq_step_status_t
- 	imeout_ms: optional hard timeout
- esume_policy: what to do if reboot occurs inside the step

### Recovery verdict
On boot you call:

- loxseq_recover(seq, reboot_reason) › returns loxseq_recovery_verdict_t

The verdict tells the application what the safe next action is:

- COLD_START: no valid checkpoint
- RESUME: continue step (idempotent action required)
- RESTART: restart the step from its beginning
- SAFE_INIT: abort to a safe init path
- OPERATOR: stop and ask a human

### Storage hooks
loxseq does not implement storage. You provide:

- write_checkpoint(buf,len,user)
- ead_checkpoint(buf,len,user)
- optional erase_checkpoint(user)

See docs/storage-contract.md for the exact contract and recommended patterns.

## Quick start

### 1) Add to your project

- Public header: include/loxseq/loxseq.h
- Implementation: src/loxseq.c

You can either vendor the files or consume via CMake (see below).

### 2) Define steps

`c
#include loxseq/loxseq.h

static loxseq_step_status_t step_fill(loxseq_t *s, uint32_t now_ms, void *u);
static loxseq_step_status_t step_heat(loxseq_t *s, uint32_t now_ms, void *u);

enum { S_FILL, S_HEAT, S_DONE, STEP_COUNT };

static const loxseq_step_def_t steps[STEP_COUNT] = {
    [S_FILL] = {
        .tag = fill,
        .action = step_fill,
        .timeout_ms = 60 * 1000,
        .resume_policy = LOXSEQ_RESUME_AT_STEP,
    },
    [S_HEAT] = {
        .tag = heat,
        .action = step_heat,
        .timeout_ms = 10 * 60 * 1000,
        .resume_policy = LOXSEQ_RESUME_FROM_START,
    },
    [S_DONE] = {
        .tag = done,
        .action = step_done,
        .timeout_ms = 0,
        .resume_policy = LOXSEQ_NEVER_RESUME,
    },
};
`

### 3) Provide a storage backend

Minimal RAM backend (tests only):

`c
static uint8_t ram_buf[64];
static size_t ram_len;

static int ram_write(const void *buf, size_t len, void *user) {
    (void)user;
    if (len > sizeof(ram_buf)) return -1;
    memcpy(ram_buf, buf, len);
    ram_len = len;
    return 0;
}

static int ram_read(void *buf, size_t len, void *user) {
    (void)user;
    if (ram_len == 0) return -1;
    if (len != ram_len) return -1;
    memcpy(buf, ram_buf, len);
    return 0;
}

static int ram_erase(void *user) {
    (void)user;
    ram_len = 0;
    return 0;
}

static const loxseq_storage_t storage = {
    .write_checkpoint = ram_write,
    .read_checkpoint  = ram_read,
    .erase_checkpoint = ram_erase,
};
`

Real products should implement power-loss tolerance (two-slot, nvlog, FRAM…).

### 4) Boot flow

`c
loxseq_t seq;
loxseq_init(&seq, steps, STEP_COUNT, &storage);

loxseq_recovery_verdict_t v = loxseq_recover(&seq, reboot_reason);

switch (v) {
    case LOXSEQ_RECOVERY_COLD_START:
        loxseq_start_fresh(&seq, now_ms);
        break;

    case LOXSEQ_RECOVERY_RESUME:
        loxseq_start_resume(&seq, now_ms);
        break;

    case LOXSEQ_RECOVERY_RESTART:
        /* you can restart explicitly by choosing RESTART semantics */
        loxseq_start_resume(&seq, now_ms);
        break;

    case LOXSEQ_RECOVERY_SAFE_INIT:
        loxseq_start_safe_init(&seq, now_ms);
        break;

    case LOXSEQ_RECOVERY_OPERATOR:
        /* show UI first; then call loxseq_operator_resolve(...) */
        loxseq_start_operator_wait(&seq);
        break;
}
`

### 5) Main loop

`c
for (;;) {
    uint32_t now_ms = platform_now_ms();
    (void)loxseq_tick(&seq, now_ms);
}
`

## Cookbook

### Idempotent actions (RESUME_AT_STEP)
If you resume inside a step, its ction may run again after reboot.
That must be safe.

Good pattern:

- command a target state (set_valve_open(V1, true)) every time
- verify state (feedback / sensor)
- return DONE only when feedback confirms it

Avoid:

- one-shot pulses
- increments
- do exactly once operations (dosing)

### Branching

`c
if (need_rinse) {
    loxseq_set_next_step(seq, S_RINSE);
    return LOXSEQ_STEP_BRANCH;
}
return LOXSEQ_STEP_DONE;
`

### Timeouts
If 	imeout_ms != 0 and step age exceeds timeout, loxseq_tick fails the
sequence with LOXSEQ_ERR_TIMEOUT and moves the state to FAILED.

## Building and testing

`sh
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build --output-on-failure -C Release
`

Targets:

- loxseq (library)
- loxseq_example_minimal
- loxseq_example_batch_reactor
- loxseq_tests
- loxseq_headercheck (ensures headers are self-contained)

## Releases

- CI: .github/workflows/ci.yml
- Tag releases: push a tag like 0.1.0 to create a GitHub Release with ZIPs
  for each OS (workflow: .github/workflows/release.yml).

## Documentation index

- docs/resume-model.md — policies + downgrade table
- docs/storage-contract.md — storage hook contract + backend patterns
- docs/integration.md — integration notes (microboot/nvlog/loxperm…)
- docs/limitations.md — what loxseq intentionally does not do

## License

MIT (see LICENSE).
