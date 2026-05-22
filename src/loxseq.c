#include "loxseq/loxseq.h"

#include <string.h>

static uint16_t crc16_ccitt_false(const uint8_t *data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021)
                                 : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

uint16_t loxseq_crc16(const void *buf, size_t len) {
    if (!buf) return 0;
    return crc16_ccitt_false((const uint8_t *)buf, len);
}

static loxseq_err_t write_checkpoint(const loxseq_t *seq) {
    if (!seq || !seq->storage || !seq->storage->write_checkpoint) {
        return LOXSEQ_ERR_INVALID_ARG;
    }

    loxseq_record_t r;
    memset(&r, 0, sizeof(r));
    r.version = LOXSEQ_RECORD_VERSION;
    r.step_index = (uint16_t)seq->current_step;
    r.step_entered_at_ms = seq->step_entered_at_ms;
    r.sequence_started_at_ms = seq->sequence_started_at_ms;
    r.generation = seq->generation;
    r.reboot_count_in_step = seq->reboot_count_in_step;
    r.flags = 0;
    r.crc16 = loxseq_crc16(&r, sizeof(r) - sizeof(r.crc16));

    int rc = seq->storage->write_checkpoint(&r, sizeof(r), seq->storage->user);
    return (rc == 0) ? LOXSEQ_OK : LOXSEQ_ERR_STORAGE;
}

static bool read_checkpoint(const loxseq_t *seq, loxseq_record_t *out) {
    if (!seq || !out || !seq->storage || !seq->storage->read_checkpoint) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    int rc = seq->storage->read_checkpoint(out, sizeof(*out), seq->storage->user);
    if (rc != 0) return false;
    if (out->version != LOXSEQ_RECORD_VERSION) return false;
    uint16_t crc = loxseq_crc16(out, sizeof(*out) - sizeof(out->crc16));
    if (crc != out->crc16) return false;
    return true;
}

static loxseq_recovery_verdict_t downgrade_policy(loxseq_resume_policy_t policy,
                                                  loxseq_reboot_reason_t reason) {
    switch (policy) {
        case LOXSEQ_RESUME_AT_STEP:
            if (reason == LOXSEQ_REBOOT_PANIC || reason == LOXSEQ_REBOOT_OTA) {
                return LOXSEQ_RECOVERY_OPERATOR;
            }
            return LOXSEQ_RECOVERY_RESUME;

        case LOXSEQ_RESUME_FROM_START:
            if (reason == LOXSEQ_REBOOT_PANIC) return LOXSEQ_RECOVERY_OPERATOR;
            if (reason == LOXSEQ_REBOOT_OTA) return LOXSEQ_RECOVERY_SAFE_INIT;
            return LOXSEQ_RECOVERY_RESTART;

        case LOXSEQ_NEVER_RESUME:
            return LOXSEQ_RECOVERY_SAFE_INIT;

        case LOXSEQ_OPERATOR_DECIDES:
        default:
            return LOXSEQ_RECOVERY_OPERATOR;
    }
}

loxseq_err_t loxseq_init(loxseq_t *seq,
                         const loxseq_step_def_t *steps,
                         size_t count,
                         const loxseq_storage_t *storage) {
    if (!seq || !steps || count == 0 || !storage) return LOXSEQ_ERR_INVALID_ARG;
    if (!storage->write_checkpoint || !storage->read_checkpoint) {
        return LOXSEQ_ERR_INVALID_ARG;
    }

    memset(seq, 0, sizeof(*seq));
    seq->state = LOXSEQ_STATE_IDLE;
    seq->current_step = -1;
    seq->steps = steps;
    seq->step_count = count;
    seq->storage = storage;
    seq->pending_next_step = -1;
    seq->initialised = true;
    return LOXSEQ_OK;
}

loxseq_recovery_verdict_t loxseq_recover(loxseq_t *seq, loxseq_reboot_reason_t reason) {
    if (!seq || !seq->initialised) return LOXSEQ_RECOVERY_COLD_START;

    loxseq_record_t r;
    if (!read_checkpoint(seq, &r)) {
        return LOXSEQ_RECOVERY_COLD_START;
    }
    if (r.step_index >= seq->step_count) {
        return LOXSEQ_RECOVERY_COLD_START;
    }

    loxseq_resume_policy_t pol = seq->steps[r.step_index].resume_policy;
    return downgrade_policy(pol, reason);
}

static loxseq_err_t start_from_record(loxseq_t *seq,
                                      const loxseq_record_t *r,
                                      bool restart_step,
                                      uint32_t now_ms) {
    if (!seq || !r) return LOXSEQ_ERR_INVALID_ARG;
    if ((size_t)r->step_index >= seq->step_count) return LOXSEQ_ERR_FORMAT;

    seq->state = LOXSEQ_STATE_RUNNING;
    seq->current_step = (int)r->step_index;
    seq->sequence_started_at_ms = r->sequence_started_at_ms;
    seq->generation = r->generation;

    if (seq->reboot_count_in_step < 0xFF) {
        seq->reboot_count_in_step = (uint8_t)(r->reboot_count_in_step + 1);
    } else {
        seq->reboot_count_in_step = 0xFF;
    }

    if (restart_step) {
        seq->step_entered_at_ms = now_ms;
    } else {
        seq->step_entered_at_ms = r->step_entered_at_ms;
    }

    seq->pending_next_step = -1;

    seq->generation++;
    return write_checkpoint(seq);
}

loxseq_err_t loxseq_start_fresh(loxseq_t *seq, uint32_t now_ms) {
    if (!seq || !seq->initialised) return LOXSEQ_ERR_INVALID_ARG;

    if (seq->storage && seq->storage->erase_checkpoint) {
        (void)seq->storage->erase_checkpoint(seq->storage->user);
    }

    seq->state = LOXSEQ_STATE_RUNNING;
    seq->current_step = 0;
    seq->step_entered_at_ms = now_ms;
    seq->sequence_started_at_ms = now_ms;
    seq->generation = 1;
    seq->reboot_count_in_step = 0;
    seq->pending_next_step = -1;
    return write_checkpoint(seq);
}

loxseq_err_t loxseq_start_safe_init(loxseq_t *seq, uint32_t now_ms) {
    return loxseq_start_fresh(seq, now_ms);
}

loxseq_err_t loxseq_start_operator_wait(loxseq_t *seq) {
    if (!seq || !seq->initialised) return LOXSEQ_ERR_INVALID_ARG;
    loxseq_record_t r;
    if (!read_checkpoint(seq, &r)) return LOXSEQ_ERR_NOT_FOUND;
    if (r.step_index >= seq->step_count) return LOXSEQ_ERR_FORMAT;

    seq->state = LOXSEQ_STATE_AWAIT_OP;
    seq->current_step = (int)r.step_index;
    seq->step_entered_at_ms = r.step_entered_at_ms;
    seq->sequence_started_at_ms = r.sequence_started_at_ms;
    seq->generation = r.generation;
    seq->reboot_count_in_step = r.reboot_count_in_step;
    seq->pending_next_step = -1;
    return LOXSEQ_OK;
}
loxseq_err_t loxseq_start_resume(loxseq_t *seq, uint32_t now_ms) {
    if (!seq || !seq->initialised) return LOXSEQ_ERR_INVALID_ARG;
    loxseq_record_t r;
    if (!read_checkpoint(seq, &r)) return LOXSEQ_ERR_NOT_FOUND;
    return start_from_record(seq, &r, false, now_ms);
}

loxseq_err_t loxseq_operator_resolve(loxseq_t *seq,
                                     loxseq_recovery_verdict_t verdict,
                                     uint32_t now_ms) {
    if (!seq || !seq->initialised) return LOXSEQ_ERR_INVALID_ARG;
    if (seq->state != LOXSEQ_STATE_AWAIT_OP) return LOXSEQ_ERR_STATE;

    if (verdict == LOXSEQ_RECOVERY_SAFE_INIT) {
        return loxseq_start_safe_init(seq, now_ms);
    }

    loxseq_record_t r;
    if (!read_checkpoint(seq, &r)) return LOXSEQ_ERR_NOT_FOUND;
    if (verdict == LOXSEQ_RECOVERY_RESUME) {
        return start_from_record(seq, &r, false, now_ms);
    }
    if (verdict == LOXSEQ_RECOVERY_RESTART) {
        return start_from_record(seq, &r, true, now_ms);
    }
    return LOXSEQ_ERR_INVALID_ARG;
}

loxseq_err_t loxseq_set_next_step(loxseq_t *seq, size_t index) {
    if (!seq || !seq->initialised) return LOXSEQ_ERR_INVALID_ARG;
    if (seq->state != LOXSEQ_STATE_RUNNING) return LOXSEQ_ERR_STATE;
    if (index >= seq->step_count) return LOXSEQ_ERR_INVALID_ARG;
    seq->pending_next_step = (int)index;
    return LOXSEQ_OK;
}

uint32_t loxseq_step_age_ms(const loxseq_t *seq, uint32_t now_ms) {
    if (!seq) return 0;
    return now_ms - seq->step_entered_at_ms;
}

static const loxseq_step_def_t *current_def(const loxseq_t *seq) {
    if (!seq) return NULL;
    if (seq->current_step < 0) return NULL;
    if ((size_t)seq->current_step >= seq->step_count) return NULL;
    return &seq->steps[seq->current_step];
}

loxseq_err_t loxseq_tick(loxseq_t *seq, uint32_t now_ms) {
    if (!seq || !seq->initialised) return LOXSEQ_ERR_INVALID_ARG;

    if (seq->state == LOXSEQ_STATE_PAUSED || seq->state == LOXSEQ_STATE_AWAIT_OP) {
        return LOXSEQ_OK;
    }
    if (seq->state != LOXSEQ_STATE_RUNNING) {
        return LOXSEQ_ERR_STATE;
    }

    const loxseq_step_def_t *def = current_def(seq);
    if (!def || !def->action) {
        seq->state = LOXSEQ_STATE_FAILED;
        return LOXSEQ_ERR_STATE;
    }

    if (def->precondition && !def->precondition(def->precondition_ctx)) {
        return LOXSEQ_OK;
    }

    if (def->timeout_ms != 0 && loxseq_step_age_ms(seq, now_ms) > def->timeout_ms) {
        seq->state = LOXSEQ_STATE_FAILED;
        return LOXSEQ_ERR_TIMEOUT;
    }

    loxseq_step_status_t st = def->action(seq, now_ms, def->user);
    if (st == LOXSEQ_STEP_RUNNING) {
        return LOXSEQ_OK;
    }

    if (st == LOXSEQ_STEP_FAILED) {
        seq->state = LOXSEQ_STATE_FAILED;
        return LOXSEQ_OK;
    }

    if (st == LOXSEQ_STEP_BRANCH) {
        if (seq->pending_next_step < 0) {
            seq->state = LOXSEQ_STATE_FAILED;
            return LOXSEQ_ERR_STATE;
        }
        seq->current_step = seq->pending_next_step;
        seq->pending_next_step = -1;
    } else if (st == LOXSEQ_STEP_DONE) {
        seq->current_step++;
    } else {
        seq->state = LOXSEQ_STATE_FAILED;
        return LOXSEQ_ERR_STATE;
    }

    if (seq->current_step >= (int)seq->step_count) {
        return loxseq_complete(seq, now_ms);
    }

    seq->step_entered_at_ms = now_ms;
    seq->reboot_count_in_step = 0;
    seq->generation++;

    loxseq_err_t wrc = write_checkpoint(seq);
    if (wrc != LOXSEQ_OK) {
        seq->state = LOXSEQ_STATE_FAILED;
        return wrc;
    }
    return LOXSEQ_OK;
}

loxseq_err_t loxseq_pause(loxseq_t *seq) {
    if (!seq || !seq->initialised) return LOXSEQ_ERR_INVALID_ARG;
    if (seq->state != LOXSEQ_STATE_RUNNING) return LOXSEQ_ERR_STATE;
    seq->state = LOXSEQ_STATE_PAUSED;
    return LOXSEQ_OK;
}

loxseq_err_t loxseq_resume_paused(loxseq_t *seq, uint32_t now_ms) {
    if (!seq || !seq->initialised) return LOXSEQ_ERR_INVALID_ARG;
    if (seq->state != LOXSEQ_STATE_PAUSED) return LOXSEQ_ERR_STATE;
    seq->state = LOXSEQ_STATE_RUNNING;
    (void)now_ms;
    return LOXSEQ_OK;
}

loxseq_err_t loxseq_abort(loxseq_t *seq, uint32_t now_ms) {
    if (!seq || !seq->initialised) return LOXSEQ_ERR_INVALID_ARG;
    (void)now_ms;
    seq->state = LOXSEQ_STATE_FAILED;
    return LOXSEQ_OK;
}

loxseq_err_t loxseq_complete(loxseq_t *seq, uint32_t now_ms) {
    if (!seq || !seq->initialised) return LOXSEQ_ERR_INVALID_ARG;
    (void)now_ms;
    seq->state = LOXSEQ_STATE_COMPLETE;
    seq->current_step = -1;
    if (seq->storage && seq->storage->erase_checkpoint) {
        int rc = seq->storage->erase_checkpoint(seq->storage->user);
        if (rc != 0) return LOXSEQ_ERR_STORAGE;
    }
    return LOXSEQ_OK;
}

const char *loxseq_current_tag(const loxseq_t *seq) {
    const loxseq_step_def_t *def = current_def(seq);
    return def ? def->tag : NULL;
}

loxseq_state_t loxseq_state(const loxseq_t *seq) {
    return seq ? seq->state : LOXSEQ_STATE_IDLE;
}

