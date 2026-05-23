/*
 * loxseq.h - Power-loss-aware step sequencer for embedded C firmware.
 *
 * SPDX-License-Identifier: MIT
 *
 * Public API. Caller-owned state, no heap, no floats, no globals.
 *
 * A loxseq_t runs a fixed sequence of steps. After each transition it
 * writes a small versioned checkpoint to a caller-provided storage
 * backend. On reboot, loxseq_recover() inspects the saved record and
 * the reboot reason, then returns a recovery verdict.
 */

#ifndef LOXSEQ_H
#define LOXSEQ_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------- */
/* Version                                                                */
/* ---------------------------------------------------------------------- */

#define LOXSEQ_VERSION_MAJOR 0
#define LOXSEQ_VERSION_MINOR 1
#define LOXSEQ_VERSION_PATCH 0

#define LOXSEQ_RECORD_VERSION 1

/* ---------------------------------------------------------------------- */
/* Errors                                                                 */
/* ---------------------------------------------------------------------- */

typedef enum {
    LOXSEQ_OK              = 0,
    LOXSEQ_ERR_INVALID_ARG = -1,
    LOXSEQ_ERR_STATE       = -2,
    LOXSEQ_ERR_STORAGE     = -3,
    LOXSEQ_ERR_FORMAT      = -4,
    LOXSEQ_ERR_CRC         = -5,
    LOXSEQ_ERR_VERSION     = -6,
    LOXSEQ_ERR_TIMEOUT     = -7,
    LOXSEQ_ERR_NOT_FOUND   = -8,
} loxseq_err_t;

/* ---------------------------------------------------------------------- */
/* Resume policy per step                                                 */
/* ---------------------------------------------------------------------- */

typedef enum {
    LOXSEQ_OPERATOR_DECIDES  = 0, /* default: present prompt */
    LOXSEQ_RESUME_AT_STEP    = 1, /* continue this step      */
    LOXSEQ_RESUME_FROM_START = 2, /* restart this step       */
    LOXSEQ_NEVER_RESUME      = 3, /* abort, go to safe init  */
} loxseq_resume_policy_t;

/* ---------------------------------------------------------------------- */
/* Reboot reason (caller-supplied)                                        */
/* ---------------------------------------------------------------------- */

typedef enum {
    LOXSEQ_REBOOT_UNKNOWN    = 0,
    LOXSEQ_REBOOT_NORMAL     = 1, /* clean shutdown          */
    LOXSEQ_REBOOT_POWER_LOSS = 2, /* brownout / power fail   */
    LOXSEQ_REBOOT_WATCHDOG   = 3,
    LOXSEQ_REBOOT_PANIC      = 4,
    LOXSEQ_REBOOT_OTA        = 5, /* firmware just updated   */
} loxseq_reboot_reason_t;

/* ---------------------------------------------------------------------- */
/* Recovery verdict                                                       */
/* ---------------------------------------------------------------------- */

typedef enum {
    LOXSEQ_RECOVERY_COLD_START = 0, /* no saved sequence to resume   */
    LOXSEQ_RECOVERY_RESUME     = 1, /* safe to continue              */
    LOXSEQ_RECOVERY_RESTART    = 2, /* restart current step          */
    LOXSEQ_RECOVERY_SAFE_INIT  = 3, /* abort sequence, go safe       */
    LOXSEQ_RECOVERY_OPERATOR   = 4, /* halt; require operator action */
} loxseq_recovery_verdict_t;

/* ---------------------------------------------------------------------- */
/* Step status returned by action callback                                */
/* ---------------------------------------------------------------------- */

typedef enum {
    LOXSEQ_STEP_RUNNING = 0, /* keep calling                       */
    LOXSEQ_STEP_DONE    = 1, /* advance to next step               */
    LOXSEQ_STEP_FAILED  = 2, /* abort sequence                     */
    LOXSEQ_STEP_BRANCH  = 3, /* jump to step set via set_next_step */
} loxseq_step_status_t;

/* ---------------------------------------------------------------------- */
/* Step action callback                                                   */
/* ---------------------------------------------------------------------- */

struct loxseq_s;
typedef struct loxseq_s loxseq_t;

typedef loxseq_step_status_t (*loxseq_action_fn)(loxseq_t *seq,
                                                 uint32_t now_ms,
                                                 void *user);

typedef bool (*loxseq_precond_fn)(void *ctx);

/* ---------------------------------------------------------------------- */
/* Step definition (caller-owned, const)                                  */
/* ---------------------------------------------------------------------- */

typedef struct {
    /* Human-readable tag. Not copied. */
    const char *tag;

    /* Action invoked from loxseq_tick. Must be idempotent if
     * resume_policy == LOXSEQ_RESUME_AT_STEP. */
    loxseq_action_fn action;

    /* Optional precondition gate; if non-null and returns false, the
     * step does not run and the sequence stays at this step (the tick
     * is effectively a no-op). Useful for loxperm integration. */
    loxseq_precond_fn precondition;
    void *precondition_ctx;

    /* Step timeout. 0 = no timeout. If exceeded, sequence transitions
     * to failed state. */
    uint32_t timeout_ms;

    /* Resume policy if a reboot occurs while this step is current. */
    loxseq_resume_policy_t resume_policy;

    /* Opaque user pointer passed to the action. */
    void *user;
} loxseq_step_def_t;

/* ---------------------------------------------------------------------- */
/* Storage hook                                                           */
/* ---------------------------------------------------------------------- */

typedef int (*loxseq_write_fn)(const void *buf, size_t len, void *user);
typedef int (*loxseq_read_fn)(void *buf, size_t len, void *user);
typedef int (*loxseq_erase_fn)(void *user);

typedef struct {
    loxseq_write_fn write_checkpoint;
    loxseq_read_fn read_checkpoint;
    loxseq_erase_fn erase_checkpoint;
    void *user;
} loxseq_storage_t;

/* ---------------------------------------------------------------------- */
/* On-disk record (version 1)                                             */
/* ---------------------------------------------------------------------- */

typedef struct {
    uint8_t version;
    uint8_t reserved;
    uint16_t step_index;
    uint32_t step_entered_at_ms; /* monotonic ms when step started */
    uint32_t sequence_started_at_ms;
    uint32_t generation; /* monotonically increasing       */
    uint8_t reboot_count_in_step;
    uint8_t flags;
    uint16_t crc16; /* covers all bytes except itself  */
} loxseq_record_t;

/* ---------------------------------------------------------------------- */
/* Sequence state                                                         */
/* ---------------------------------------------------------------------- */

typedef enum {
    LOXSEQ_STATE_IDLE     = 0,
    LOXSEQ_STATE_RUNNING  = 1,
    LOXSEQ_STATE_PAUSED   = 2,
    LOXSEQ_STATE_AWAIT_OP = 3, /* operator must confirm resume   */
    LOXSEQ_STATE_FAILED   = 4,
    LOXSEQ_STATE_COMPLETE = 5,
} loxseq_state_t;

struct loxseq_s {
    /* --- public-readable --- */
    loxseq_state_t state;
    int current_step; /* -1 if idle/complete */
    uint32_t step_entered_at_ms;
    uint32_t sequence_started_at_ms;
    uint32_t generation;
    uint8_t reboot_count_in_step;
    loxseq_recovery_verdict_t last_recovery;

    /* --- internal --- */
    const loxseq_step_def_t *steps;
    size_t step_count;
    const loxseq_storage_t *storage;
    int pending_next_step; /* -1 if none      */
    bool initialised;
};

/* ---------------------------------------------------------------------- */
/* Lifecycle                                                              */
/* ---------------------------------------------------------------------- */

loxseq_err_t loxseq_init(loxseq_t *seq,
                         const loxseq_step_def_t *steps,
                         size_t count,
                         const loxseq_storage_t *storage);

/* Inspect the saved record and the reboot reason; return the verdict.
 * Does not start or advance the sequencer. May update seq->last_recovery.
 * Call once at boot. */
loxseq_recovery_verdict_t loxseq_recover(loxseq_t *seq, loxseq_reboot_reason_t reason);

/* ---------------------------------------------------------------------- */
/* Start variants                                                         */
/* ---------------------------------------------------------------------- */

/* Start fresh from step 0. Discards any prior checkpoint. */
loxseq_err_t loxseq_start_fresh(loxseq_t *seq, uint32_t now_ms);

/* Continue from the saved checkpoint (RESUME semantics): preserves the
 * saved step_entered_at_ms. Only valid after a loxseq_recover() that
 * returned RESUME. */
loxseq_err_t loxseq_start_resume(loxseq_t *seq, uint32_t now_ms);

/* Restart the current step from the saved checkpoint (RESTART
 * semantics): step_entered_at_ms is reset to now_ms. Only valid after a
 * loxseq_recover() that returned RESTART. */
loxseq_err_t loxseq_start_restart(loxseq_t *seq, uint32_t now_ms);

/* Skip the saved sequence; go to a safe state. Caller defines what
 * "safe" means by what actions the safe_init step performs. */
loxseq_err_t loxseq_start_safe_init(loxseq_t *seq, uint32_t now_ms);

/* Enter LOXSEQ_STATE_AWAIT_OP after a loxseq_recover() that returned
 * LOXSEQ_RECOVERY_OPERATOR. Loads the saved record so the application
 * can show which step was active. */
loxseq_err_t loxseq_start_operator_wait(loxseq_t *seq);

/* Resolve LOXSEQ_RECOVERY_OPERATOR: operator chose one of:
 *   - LOXSEQ_RECOVERY_RESUME
 *   - LOXSEQ_RECOVERY_RESTART
 *   - LOXSEQ_RECOVERY_SAFE_INIT
 * Other values are invalid. */
loxseq_err_t loxseq_operator_resolve(loxseq_t *seq,
                                     loxseq_recovery_verdict_t verdict,
                                     uint32_t now_ms);

/* ---------------------------------------------------------------------- */
/* Main loop                                                              */
/* ---------------------------------------------------------------------- */

/* Run one iteration. Calls the current step's action exactly once
 * (or zero times if the precondition gates it). Handles step
 * transitions, timeouts, branching, completion. */
loxseq_err_t loxseq_tick(loxseq_t *seq, uint32_t now_ms);

/* ---------------------------------------------------------------------- */
/* Step actions can use these                                             */
/* ---------------------------------------------------------------------- */

/* Set the next step index. Action must then return LOXSEQ_STEP_BRANCH. */
loxseq_err_t loxseq_set_next_step(loxseq_t *seq, size_t index);

/* Time spent in current step (since entry). */
uint32_t loxseq_step_age_ms(const loxseq_t *seq, uint32_t now_ms);

/* ---------------------------------------------------------------------- */
/* Control                                                                */
/* ---------------------------------------------------------------------- */

loxseq_err_t loxseq_pause(loxseq_t *seq);
loxseq_err_t loxseq_resume_paused(loxseq_t *seq, uint32_t now_ms);
loxseq_err_t loxseq_abort(loxseq_t *seq, uint32_t now_ms);
loxseq_err_t loxseq_complete(loxseq_t *seq, uint32_t now_ms);

/* ---------------------------------------------------------------------- */
/* Introspection                                                          */
/* ---------------------------------------------------------------------- */

const char *loxseq_current_tag(const loxseq_t *seq);
loxseq_state_t loxseq_state(const loxseq_t *seq);

/* ---------------------------------------------------------------------- */
/* CRC helper exposed for testing                                         */
/* ---------------------------------------------------------------------- */

uint16_t loxseq_crc16(const void *buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* LOXSEQ_H */
