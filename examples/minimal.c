/*
 * examples/minimal.c - simplest possible loxseq usage with RAM storage.
 */

#include <stdio.h>
#include <string.h>
#include "loxseq/loxseq.h"

/* --- fake RAM-backed storage (test only) ---------------------------- */

static uint8_t ram_buf[64];
static size_t  ram_len = 0;

static int ram_write(const void *buf, size_t len, void *user) {
    (void)user;
    if (len > sizeof(ram_buf)) return -1;
    memcpy(ram_buf, buf, len);
    ram_len = len;
    return 0;
}
static int ram_read(void *buf, size_t len, void *user) {
    (void)user;
    if (ram_len == 0)         return -1;
    if (len != ram_len)       return -1;
    memcpy(buf, ram_buf, len);
    return 0;
}
static int ram_erase(void *user) { (void)user; ram_len = 0; return 0; }

static const loxseq_storage_t ram_storage = {
    .write_checkpoint = ram_write,
    .read_checkpoint  = ram_read,
    .erase_checkpoint = ram_erase,
};

/* --- step actions --------------------------------------------------- */

static loxseq_step_status_t step_fill(loxseq_t *s, uint32_t now, void *u) {
    (void)u;
    printf("  [t=%5u] fill\n", now);
    if (loxseq_step_age_ms(s, now) >= 2000) return LOXSEQ_STEP_DONE;
    return LOXSEQ_STEP_RUNNING;
}
static loxseq_step_status_t step_mix(loxseq_t *s, uint32_t now, void *u) {
    (void)u;
    printf("  [t=%5u] mix\n", now);
    if (loxseq_step_age_ms(s, now) >= 3000) return LOXSEQ_STEP_DONE;
    return LOXSEQ_STEP_RUNNING;
}
static loxseq_step_status_t step_drain(loxseq_t *s, uint32_t now, void *u) {
    (void)u;
    printf("  [t=%5u] drain\n", now);
    if (loxseq_step_age_ms(s, now) >= 2000) return LOXSEQ_STEP_DONE;
    return LOXSEQ_STEP_RUNNING;
}

/* --- step table ----------------------------------------------------- */

enum { S_FILL, S_MIX, S_DRAIN, STEP_COUNT };

static const loxseq_step_def_t steps[STEP_COUNT] = {
    [S_FILL]  = { .tag = "fill",  .action = step_fill,
                  .timeout_ms = 30000,
                  .resume_policy = LOXSEQ_RESUME_AT_STEP },
    [S_MIX]   = { .tag = "mix",   .action = step_mix,
                  .timeout_ms = 30000,
                  .resume_policy = LOXSEQ_RESUME_AT_STEP },
    [S_DRAIN] = { .tag = "drain", .action = step_drain,
                  .timeout_ms = 30000,
                  .resume_policy = LOXSEQ_NEVER_RESUME },
};

int main(void) {
    loxseq_t seq;
    loxseq_init(&seq, steps, STEP_COUNT, &ram_storage);

    /* Pretend cold boot. */
    loxseq_recovery_verdict_t v = loxseq_recover(&seq,
                                                 LOXSEQ_REBOOT_NORMAL);
    printf("recover verdict: %d\n", v);

    loxseq_start_fresh(&seq, 0);

    /* Simulated loop. */
    for (uint32_t t = 0; t < 8000; t += 500) {
        loxseq_tick(&seq, t);
        if (loxseq_state(&seq) == LOXSEQ_STATE_COMPLETE ||
            loxseq_state(&seq) == LOXSEQ_STATE_FAILED) {
            break;
        }
    }

    printf("final state: %d\n", loxseq_state(&seq));
    return 0;
}
