/*
 * examples/batch_reactor.c
 *
 * Realistic batch reactor sequence with:
 *   - per-step resume policy
 *   - precondition gating (loxperm-style)
 *   - recovery flow after a power-loss reboot
 */

#include <stdio.h>
#include <string.h>
#include "loxseq/loxseq.h"

/* --- fake persistent storage --- */

static uint8_t  nv_buf[64];
static size_t   nv_len = 0;
static bool     simulate_power_loss = false;

static int nv_write(const void *buf, size_t len, void *user) {
    (void)user;
    if (simulate_power_loss) return -1;
    if (len > sizeof(nv_buf)) return -1;
    memcpy(nv_buf, buf, len);
    nv_len = len;
    return 0;
}
static int nv_read(void *buf, size_t len, void *user) {
    (void)user;
    if (nv_len == 0 || len != nv_len) return -1;
    memcpy(buf, nv_buf, len);
    return 0;
}
static int nv_erase(void *user) { (void)user; nv_len = 0; return 0; }

static const loxseq_storage_t nv_storage = {
    .write_checkpoint = nv_write,
    .read_checkpoint  = nv_read,
    .erase_checkpoint = nv_erase,
};

/* --- preconditions (would normally be loxperm chains) --- */

static bool always_true(void *ctx) { (void)ctx; return true; }

/* --- actions --- */

#define STEP_DURATION_MS 2000

static loxseq_step_status_t do_step(loxseq_t *s, uint32_t now, void *u) {
    const char *name = (const char *)u;
    printf("  [t=%6u] running '%s' (age=%u)\n",
           now, name, loxseq_step_age_ms(s, now));
    if (loxseq_step_age_ms(s, now) >= STEP_DURATION_MS)
        return LOXSEQ_STEP_DONE;
    return LOXSEQ_STEP_RUNNING;
}

enum { S_PURGE, S_FILL, S_HEAT, S_DOSE, S_MIX, S_DRAIN, STEP_COUNT };

static const loxseq_step_def_t steps[STEP_COUNT] = {
    [S_PURGE] = { .tag="purge",
                  .action=do_step, .user="purge",
                  .precondition=always_true,
                  .timeout_ms=30000,
                  .resume_policy=LOXSEQ_RESUME_FROM_START },
    [S_FILL]  = { .tag="fill",
                  .action=do_step, .user="fill",
                  .precondition=always_true,
                  .timeout_ms=30000,
                  .resume_policy=LOXSEQ_RESUME_AT_STEP },
    [S_HEAT]  = { .tag="heat",
                  .action=do_step, .user="heat",
                  .precondition=always_true,
                  .timeout_ms=60000,
                  .resume_policy=LOXSEQ_RESUME_AT_STEP },
    [S_DOSE]  = { .tag="dose",
                  .action=do_step, .user="dose",
                  .precondition=always_true,
                  .timeout_ms=10000,
                  .resume_policy=LOXSEQ_NEVER_RESUME },
    [S_MIX]   = { .tag="mix",
                  .action=do_step, .user="mix",
                  .precondition=always_true,
                  .timeout_ms=30000,
                  .resume_policy=LOXSEQ_RESUME_AT_STEP },
    [S_DRAIN] = { .tag="drain",
                  .action=do_step, .user="drain",
                  .precondition=always_true,
                  .timeout_ms=30000,
                  .resume_policy=LOXSEQ_OPERATOR_DECIDES },
};

/* --- run one phase of operation --- */

static void run_until_done_or_interrupted(loxseq_t *seq,
                                          uint32_t start_t,
                                          uint32_t max_t,
                                          uint32_t interrupt_t) {
    for (uint32_t t = start_t; t <= max_t; t += 500) {
        if (t == interrupt_t) {
            printf("\n*** POWER LOSS at t=%u, step=%s ***\n\n",
                   t, loxseq_current_tag(seq));
            return;
        }
        loxseq_tick(seq, t);
        if (loxseq_state(seq) == LOXSEQ_STATE_COMPLETE) {
            printf("  [t=%6u] sequence COMPLETE\n", t);
            return;
        }
        if (loxseq_state(seq) == LOXSEQ_STATE_FAILED) {
            printf("  [t=%6u] sequence FAILED\n", t);
            return;
        }
    }
}

int main(void) {
    /* --- session 1: run until power loss in S_HEAT --- */
    printf("=== session 1 (cold start) ===\n");
    loxseq_t seq;
    loxseq_init(&seq, steps, STEP_COUNT, &nv_storage);

    loxseq_recovery_verdict_t v =
        loxseq_recover(&seq, LOXSEQ_REBOOT_NORMAL);
    if (v == LOXSEQ_RECOVERY_COLD_START) {
        loxseq_start_fresh(&seq, 0);
    }

    run_until_done_or_interrupted(&seq, 0, 30000, /*interrupt at*/ 6500);

    /* --- power loss happens here --- */
    printf("=== session 2 (after power loss) ===\n");
    loxseq_t seq2;
    loxseq_init(&seq2, steps, STEP_COUNT, &nv_storage);

    v = loxseq_recover(&seq2, LOXSEQ_REBOOT_POWER_LOSS);
    printf("recovery verdict after power loss: %d\n", v);

    switch (v) {
        case LOXSEQ_RECOVERY_RESUME:
            printf("  -> resuming step %d (%s)\n",
                   seq2.current_step,
                   steps[seq2.current_step].tag);
            loxseq_start_resume(&seq2, 7000);
            run_until_done_or_interrupted(&seq2, 7000, 30000, UINT32_MAX);
            break;

        case LOXSEQ_RECOVERY_RESTART:
            printf("  -> restarting step\n");
            loxseq_start_restart(&seq2, 7000);
            run_until_done_or_interrupted(&seq2, 7000, 30000, UINT32_MAX);
            break;

        case LOXSEQ_RECOVERY_SAFE_INIT:
            printf("  -> going to safe init\n");
            loxseq_start_safe_init(&seq2, 7000);
            break;

        case LOXSEQ_RECOVERY_OPERATOR:
            printf("  -> awaiting operator\n");
            (void)loxseq_start_operator_wait(&seq2);
            /* Operator says: resume */
            (void)loxseq_operator_resolve(&seq2, LOXSEQ_RECOVERY_RESUME, 7000);
            run_until_done_or_interrupted(&seq2, 7000, 30000, UINT32_MAX);
            break;

        default:
            loxseq_start_fresh(&seq2, 7000);
            break;
    }

    return 0;
}
