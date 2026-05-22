# loxseq

Power-loss-aware step sequencer for embedded C firmware.

loxseq is a small, heap-free C99 library that runs a sequence of named steps,
persistently checkpoints the current step, and on reboot decides — based on a
caller-supplied policy — whether to resume, restart a step, go to safe init, or
require operator intervention.

`c
#include  loxseq/loxseq.h

enum { S_FILL, S_HEAT, S_MIX, S_DRAIN, STEP_COUNT };

static const loxseq_step_def_t batch_steps[STEP_COUNT] = {
    [S_FILL]  = { .tag = fill,  .action = step_fill,  .timeout_ms = 60000 },
    [S_HEAT]  = { .tag = heat,  .action = step_heat,  .timeout_ms = 600000,
                  .resume_policy = LOXSEQ_RESUME_FROM_START },
    [S_MIX]   = { .tag = mix,   .action = step_mix,   .timeout_ms = 300000,
                  .resume_policy = LOXSEQ_RESUME_AT_STEP },
    [S_DRAIN] = { .tag = drain, .action = step_drain, .timeout_ms = 120000,
                  .resume_policy = LOXSEQ_NEVER_RESUME },
};

static loxseq_t batch;
loxseq_init(&batch, batch_steps, STEP_COUNT, &storage_hooks);

/* on boot */
loxseq_recovery_verdict_t v = loxseq_recover(&batch, reboot_reason);
if (v == LOXSEQ_RECOVERY_RESUME) {
    loxseq_start_resume(&batch, now_ms);
} else if (v == LOXSEQ_RECOVERY_RESTART) {
    /* treat as operator decision or restart explicitly */
    loxseq_start_resume(&batch, now_ms);
} else if (v == LOXSEQ_RECOVERY_SAFE_INIT) {
    loxseq_start_safe_init(&batch, now_ms);
} else if (v == LOXSEQ_RECOVERY_OPERATOR) {
    loxseq_start_operator_wait(&batch);
    show_recovery_prompt();
}

/* main loop */
loxseq_tick(&batch, now_ms);
`

## Docs

- docs/resume-model.md
- docs/storage-contract.md
- docs/integration.md
- docs/limitations.md

## Building

`sh
cmake -S . -B build
cmake --build build
ctest --test-dir build
`

Examples:
- loxseq_example_minimal
- loxseq_example_batch_reactor

## License

MIT (see LICENSE).