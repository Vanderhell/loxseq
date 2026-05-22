# loxseq

Power-loss-aware step sequencer for embedded C firmware.

`loxseq` is a small, heap-free C99 library that checkpoints step progress to caller-provided storage and computes a recovery verdict on reboot.

## Key ideas

- Persistent checkpoint record (CRC-protected)
- Per-step resume policy with conservative downgrades
- No heap / globals / floats; caller-owned state
- Tick-driven execution: you provide `now_ms`
- Optional branching via `set_next_step` + `BRANCH`

## Minimal integration

### 1. Step table

```c
#include <loxseq/loxseq.h>

enum {
    S_FILL,
    S_HEAT,
    S_DRAIN,
    STEP_COUNT
};

static loxseq_step_status_t step_fill(loxseq_t *s, uint32_t now_ms, void *u);
static loxseq_step_status_t step_heat(loxseq_t *s, uint32_t now_ms, void *u);
static loxseq_step_status_t step_drain(loxseq_t *s, uint32_t now_ms, void *u);

static const loxseq_step_def_t steps[STEP_COUNT] = {
    [S_FILL] = {
        .tag = "fill",
        .action = step_fill,
        .timeout_ms = 60000,
        .resume_policy = LOXSEQ_RESUME_AT_STEP
    },
    [S_HEAT] = {
        .tag = "heat",
        .action = step_heat,
        .timeout_ms = 600000,
        .resume_policy = LOXSEQ_RESUME_FROM_START
    },
    [S_DRAIN] = {
        .tag = "drain",
        .action = step_drain,
        .timeout_ms = 120000,
        .resume_policy = LOXSEQ_NEVER_RESUME
    },
};
