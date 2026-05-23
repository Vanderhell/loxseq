#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "loxseq/loxseq.h"

#include "test_util.h"

#define STATIC_ASSERT(name, expr) typedef char static_assert_##name[(expr) ? 1 : -1]

/* Storage record layout checks (v1) */
STATIC_ASSERT(record_size_is_20_bytes, sizeof(loxseq_record_t) == 20);
STATIC_ASSERT(record_off_version_0, offsetof(loxseq_record_t, version) == 0);
STATIC_ASSERT(record_off_step_index_2, offsetof(loxseq_record_t, step_index) == 2);
STATIC_ASSERT(record_off_step_entered_4, offsetof(loxseq_record_t, step_entered_at_ms) == 4);
STATIC_ASSERT(record_off_crc16_18, offsetof(loxseq_record_t, crc16) == 18);

typedef struct {
    loxseq_record_t slot;
    int have;
    int read_rc;
    int write_rc;
    int erase_rc;
} test_storage_t;

static int ts_write(const void *buf, size_t len, void *user) {
    test_storage_t *ts = (test_storage_t *)user;
    if (ts->write_rc != 0) return ts->write_rc;
    if (len != sizeof(ts->slot)) return -1;
    memcpy(&ts->slot, buf, len);
    ts->have = 1;
    return 0;
}

static int ts_read(void *buf, size_t len, void *user) {
    test_storage_t *ts = (test_storage_t *)user;
    if (ts->read_rc != 0) return ts->read_rc;
    if (!ts->have) return -1;
    if (len != sizeof(ts->slot)) return -1;
    memcpy(buf, &ts->slot, len);
    return 0;
}

static int ts_erase(void *user) {
    test_storage_t *ts = (test_storage_t *)user;
    if (ts->erase_rc != 0) return ts->erase_rc;
    ts->have = 0;
    memset(&ts->slot, 0, sizeof(ts->slot));
    return 0;
}

static void ts_reset(test_storage_t *ts) {
    memset(ts, 0, sizeof(*ts));
}

static loxseq_storage_t make_storage(test_storage_t *ts) {
    loxseq_storage_t s;
    s.write_checkpoint = ts_write;
    s.read_checkpoint = ts_read;
    s.erase_checkpoint = ts_erase;
    s.user = ts;
    return s;
}

static loxseq_step_status_t step_running(loxseq_t *seq, uint32_t now_ms, void *user) {
    (void)seq;
    (void)now_ms;
    (void)user;
    return LOXSEQ_STEP_RUNNING;
}

static loxseq_step_status_t step_done(loxseq_t *seq, uint32_t now_ms, void *user) {
    (void)seq;
    (void)now_ms;
    (void)user;
    return LOXSEQ_STEP_DONE;
}

typedef struct {
    int called;
} call_counter_t;

static loxseq_step_status_t step_count_calls(loxseq_t *seq, uint32_t now_ms, void *user) {
    (void)seq;
    (void)now_ms;
    call_counter_t *c = (call_counter_t *)user;
    c->called++;
    return LOXSEQ_STEP_RUNNING;
}

static bool precond_false(void *ctx) {
    (void)ctx;
    return false;
}

static loxseq_step_status_t branch_without_setting(loxseq_t *seq, uint32_t now_ms, void *user) {
    (void)seq;
    (void)now_ms;
    (void)user;
    return LOXSEQ_STEP_BRANCH;
}

static loxseq_step_status_t branch_to_1(loxseq_t *seq, uint32_t now_ms, void *user) {
    (void)now_ms;
    (void)user;
    (void)loxseq_set_next_step(seq, 1);
    return LOXSEQ_STEP_BRANCH;
}

static void write_record_with_crc(test_storage_t *ts, const loxseq_record_t *r_in) {
    loxseq_record_t r = *r_in;
    r.crc16 = loxseq_crc16(&r, sizeof(r) - sizeof(r.crc16));
    ts->slot = r;
    ts->have = 1;
}

static void test_crc16_known_vector(void) {
    const char *s = "123456789";
    TEST_ASSERT_EQ_INT(loxseq_crc16(s, 9), 0x29B1);
}

static void test_init_invalid_args(void) {
    test_storage_t ts;
    ts_reset(&ts);
    loxseq_storage_t storage = make_storage(&ts);

    static const loxseq_step_def_t steps[] = {
        { .tag = "s0", .action = step_done, .timeout_ms = 0, .resume_policy = LOXSEQ_RESUME_AT_STEP },
    };

    loxseq_t seq;
    TEST_ASSERT_EQ_INT(loxseq_init(NULL, steps, 1, &storage), LOXSEQ_ERR_INVALID_ARG);
    TEST_ASSERT_EQ_INT(loxseq_init(&seq, NULL, 1, &storage), LOXSEQ_ERR_INVALID_ARG);
    TEST_ASSERT_EQ_INT(loxseq_init(&seq, steps, 0, &storage), LOXSEQ_ERR_INVALID_ARG);
    TEST_ASSERT_EQ_INT(loxseq_init(&seq, steps, 1, NULL), LOXSEQ_ERR_INVALID_ARG);

    loxseq_storage_t bad = storage;
    bad.read_checkpoint = NULL;
    TEST_ASSERT_EQ_INT(loxseq_init(&seq, steps, 1, &bad), LOXSEQ_ERR_INVALID_ARG);
}

static void test_recover_sets_last_recovery(void) {
    test_storage_t ts;
    ts_reset(&ts);
    loxseq_storage_t storage = make_storage(&ts);

    static const loxseq_step_def_t steps[] = {
        { .tag = "s0", .action = step_done, .timeout_ms = 0, .resume_policy = LOXSEQ_RESUME_AT_STEP },
        { .tag = "s1", .action = step_done, .timeout_ms = 0, .resume_policy = LOXSEQ_RESUME_FROM_START },
    };

    loxseq_t seq;
    TEST_ASSERT_EQ_INT(loxseq_init(&seq, steps, 2, &storage), LOXSEQ_OK);

    /* No record => cold start */
    loxseq_recovery_verdict_t v = loxseq_recover(&seq, LOXSEQ_REBOOT_NORMAL);
    TEST_ASSERT_EQ_INT(v, LOXSEQ_RECOVERY_COLD_START);
    TEST_ASSERT_EQ_INT(seq.last_recovery, LOXSEQ_RECOVERY_COLD_START);

    TEST_ASSERT_EQ_INT(loxseq_start_fresh(&seq, 100), LOXSEQ_OK);
    v = loxseq_recover(&seq, LOXSEQ_REBOOT_NORMAL);
    TEST_ASSERT_EQ_INT(v, LOXSEQ_RECOVERY_RESUME);
    TEST_ASSERT_EQ_INT(seq.last_recovery, LOXSEQ_RECOVERY_RESUME);
}

static void test_recover_read_failure_is_cold_start(void) {
    test_storage_t ts;
    ts_reset(&ts);
    ts.read_rc = -1;
    loxseq_storage_t storage = make_storage(&ts);

    static const loxseq_step_def_t steps[] = {
        { .tag = "s0", .action = step_done, .timeout_ms = 0, .resume_policy = LOXSEQ_RESUME_AT_STEP },
    };

    loxseq_t seq;
    TEST_ASSERT_EQ_INT(loxseq_init(&seq, steps, 1, &storage), LOXSEQ_OK);
    TEST_ASSERT_EQ_INT(loxseq_recover(&seq, LOXSEQ_REBOOT_POWER_LOSS), LOXSEQ_RECOVERY_COLD_START);
}

static void test_recover_corrupted_crc_is_cold_start(void) {
    test_storage_t ts;
    ts_reset(&ts);
    loxseq_storage_t storage = make_storage(&ts);

    static const loxseq_step_def_t steps[] = {
        { .tag = "s0", .action = step_done, .timeout_ms = 0, .resume_policy = LOXSEQ_RESUME_AT_STEP },
    };

    loxseq_t seq;
    TEST_ASSERT_EQ_INT(loxseq_init(&seq, steps, 1, &storage), LOXSEQ_OK);
    TEST_ASSERT_EQ_INT(loxseq_start_fresh(&seq, 0), LOXSEQ_OK);

    /* Corrupt one byte in-place. */
    ((uint8_t *)&ts.slot)[4] ^= 0xFFU;
    TEST_ASSERT_EQ_INT(loxseq_recover(&seq, LOXSEQ_REBOOT_NORMAL), LOXSEQ_RECOVERY_COLD_START);
}

static void test_recover_unsupported_version_is_cold_start(void) {
    test_storage_t ts;
    ts_reset(&ts);
    loxseq_storage_t storage = make_storage(&ts);

    static const loxseq_step_def_t steps[] = {
        { .tag = "s0", .action = step_done, .timeout_ms = 0, .resume_policy = LOXSEQ_RESUME_AT_STEP },
    };

    loxseq_t seq;
    TEST_ASSERT_EQ_INT(loxseq_init(&seq, steps, 1, &storage), LOXSEQ_OK);
    TEST_ASSERT_EQ_INT(loxseq_start_fresh(&seq, 0), LOXSEQ_OK);

    ts.slot.version = (uint8_t)(LOXSEQ_RECORD_VERSION + 1U);
    TEST_ASSERT_EQ_INT(loxseq_recover(&seq, LOXSEQ_REBOOT_NORMAL), LOXSEQ_RECOVERY_COLD_START);
}

static void test_recover_out_of_range_step_is_cold_start(void) {
    test_storage_t ts;
    ts_reset(&ts);
    loxseq_storage_t storage = make_storage(&ts);

    static const loxseq_step_def_t steps[] = {
        { .tag = "s0", .action = step_done, .timeout_ms = 0, .resume_policy = LOXSEQ_RESUME_AT_STEP },
    };

    loxseq_t seq;
    TEST_ASSERT_EQ_INT(loxseq_init(&seq, steps, 1, &storage), LOXSEQ_OK);

    loxseq_record_t r;
    memset(&r, 0, sizeof(r));
    r.version = LOXSEQ_RECORD_VERSION;
    r.step_index = 99;
    r.step_entered_at_ms = 1;
    r.sequence_started_at_ms = 1;
    r.generation = 1;
    r.reboot_count_in_step = 0;
    r.flags = 0;
    write_record_with_crc(&ts, &r);

    TEST_ASSERT_EQ_INT(loxseq_recover(&seq, LOXSEQ_REBOOT_NORMAL), LOXSEQ_RECOVERY_COLD_START);
}

static void test_start_resume_restart_step_entered_semantics(void) {
    test_storage_t ts;
    ts_reset(&ts);
    loxseq_storage_t storage = make_storage(&ts);

    static const loxseq_step_def_t steps[] = {
        { .tag = "s0", .action = step_running, .timeout_ms = 0, .resume_policy = LOXSEQ_RESUME_FROM_START },
    };

    loxseq_t seq;
    TEST_ASSERT_EQ_INT(loxseq_init(&seq, steps, 1, &storage), LOXSEQ_OK);

    TEST_ASSERT_EQ_INT(loxseq_start_fresh(&seq, 1000), LOXSEQ_OK);
    TEST_ASSERT_EQ_U32(seq.step_entered_at_ms, 1000);

    /* Resume preserves original timestamp */
    TEST_ASSERT_EQ_INT(loxseq_start_resume(&seq, 5000), LOXSEQ_OK);
    TEST_ASSERT_EQ_U32(seq.step_entered_at_ms, 1000);

    /* Restart resets timestamp */
    TEST_ASSERT_EQ_INT(loxseq_start_restart(&seq, 6000), LOXSEQ_OK);
    TEST_ASSERT_EQ_U32(seq.step_entered_at_ms, 6000);
}

static void test_operator_resolve_restart_resets_step_entered(void) {
    test_storage_t ts;
    ts_reset(&ts);
    loxseq_storage_t storage = make_storage(&ts);

    static const loxseq_step_def_t steps[] = {
        { .tag = "s0", .action = step_running, .timeout_ms = 0, .resume_policy = LOXSEQ_OPERATOR_DECIDES },
    };

    loxseq_t seq;
    TEST_ASSERT_EQ_INT(loxseq_init(&seq, steps, 1, &storage), LOXSEQ_OK);
    TEST_ASSERT_EQ_INT(loxseq_start_fresh(&seq, 100), LOXSEQ_OK);
    TEST_ASSERT_EQ_U32(seq.step_entered_at_ms, 100);

    TEST_ASSERT_EQ_INT(loxseq_start_operator_wait(&seq), LOXSEQ_OK);
    TEST_ASSERT_EQ_INT(loxseq_state(&seq), LOXSEQ_STATE_AWAIT_OP);

    TEST_ASSERT_EQ_INT(loxseq_operator_resolve(&seq, LOXSEQ_RECOVERY_RESTART, 900), LOXSEQ_OK);
    TEST_ASSERT_EQ_INT(loxseq_state(&seq), LOXSEQ_STATE_RUNNING);
    TEST_ASSERT_EQ_U32(seq.step_entered_at_ms, 900);
}

static void test_start_resume_out_of_range_step_is_format_error(void) {
    test_storage_t ts;
    ts_reset(&ts);
    loxseq_storage_t storage = make_storage(&ts);

    static const loxseq_step_def_t steps[] = {
        { .tag = "s0", .action = step_running, .timeout_ms = 0, .resume_policy = LOXSEQ_RESUME_AT_STEP },
    };

    loxseq_t seq;
    TEST_ASSERT_EQ_INT(loxseq_init(&seq, steps, 1, &storage), LOXSEQ_OK);

    loxseq_record_t r;
    memset(&r, 0, sizeof(r));
    r.version = LOXSEQ_RECORD_VERSION;
    r.step_index = 2;
    r.step_entered_at_ms = 123;
    r.sequence_started_at_ms = 100;
    r.generation = 1;
    r.reboot_count_in_step = 0;
    write_record_with_crc(&ts, &r);

    TEST_ASSERT_EQ_INT(loxseq_start_resume(&seq, 1000), LOXSEQ_ERR_FORMAT);
}

static void test_reboot_counter_increment_and_saturation(void) {
    test_storage_t ts;
    ts_reset(&ts);
    loxseq_storage_t storage = make_storage(&ts);

    static const loxseq_step_def_t steps[] = {
        { .tag = "s0", .action = step_running, .timeout_ms = 0, .resume_policy = LOXSEQ_RESUME_AT_STEP },
    };

    loxseq_t seq;
    TEST_ASSERT_EQ_INT(loxseq_init(&seq, steps, 1, &storage), LOXSEQ_OK);
    TEST_ASSERT_EQ_INT(loxseq_start_fresh(&seq, 0), LOXSEQ_OK);

    /* Inject record value and resume; should increment based on record. */
    ts.slot.reboot_count_in_step = 7;
    ts.slot.crc16 = loxseq_crc16(&ts.slot, sizeof(ts.slot) - sizeof(ts.slot.crc16));
    TEST_ASSERT_EQ_INT(loxseq_start_resume(&seq, 1), LOXSEQ_OK);
    TEST_ASSERT_EQ_INT(seq.reboot_count_in_step, 8);

    /* Saturate at 0xFF based on record value. */
    ts.slot.reboot_count_in_step = 0xFF;
    ts.slot.crc16 = loxseq_crc16(&ts.slot, sizeof(ts.slot) - sizeof(ts.slot.crc16));
    TEST_ASSERT_EQ_INT(loxseq_start_resume(&seq, 2), LOXSEQ_OK);
    TEST_ASSERT_EQ_INT(seq.reboot_count_in_step, 0xFF);
}

static void test_branch_without_target_fails(void) {
    test_storage_t ts;
    ts_reset(&ts);
    loxseq_storage_t storage = make_storage(&ts);

    static const loxseq_step_def_t steps[] = {
        { .tag = "s0", .action = branch_without_setting, .timeout_ms = 0, .resume_policy = LOXSEQ_RESUME_AT_STEP },
    };

    loxseq_t seq;
    TEST_ASSERT_EQ_INT(loxseq_init(&seq, steps, 1, &storage), LOXSEQ_OK);
    TEST_ASSERT_EQ_INT(loxseq_start_fresh(&seq, 0), LOXSEQ_OK);

    TEST_ASSERT_EQ_INT(loxseq_tick(&seq, 1), LOXSEQ_ERR_STATE);
    TEST_ASSERT_EQ_INT(loxseq_state(&seq), LOXSEQ_STATE_FAILED);
}

static void test_branch_to_another_step(void) {
    test_storage_t ts;
    ts_reset(&ts);
    loxseq_storage_t storage = make_storage(&ts);

    static const loxseq_step_def_t steps[] = {
        { .tag = "s0", .action = branch_to_1, .timeout_ms = 0, .resume_policy = LOXSEQ_RESUME_AT_STEP },
        { .tag = "s1", .action = step_running, .timeout_ms = 0, .resume_policy = LOXSEQ_RESUME_AT_STEP },
    };

    loxseq_t seq;
    TEST_ASSERT_EQ_INT(loxseq_init(&seq, steps, 2, &storage), LOXSEQ_OK);
    TEST_ASSERT_EQ_INT(loxseq_start_fresh(&seq, 10), LOXSEQ_OK);

    TEST_ASSERT_EQ_INT(loxseq_tick(&seq, 20), LOXSEQ_OK);
    TEST_ASSERT_EQ_INT(seq.current_step, 1);
    TEST_ASSERT_EQ_U32(seq.step_entered_at_ms, 20);
}

static void test_precondition_false_skips_action(void) {
    test_storage_t ts;
    ts_reset(&ts);
    loxseq_storage_t storage = make_storage(&ts);

    call_counter_t counter;
    counter.called = 0;

    loxseq_step_def_t steps[] = {
        { .tag = "s0",
          .action = step_count_calls,
          .user = &counter,
          .precondition = precond_false,
          .timeout_ms = 0,
          .resume_policy = LOXSEQ_RESUME_AT_STEP },
    };

    loxseq_t seq;
    TEST_ASSERT_EQ_INT(loxseq_init(&seq, steps, 1, &storage), LOXSEQ_OK);
    TEST_ASSERT_EQ_INT(loxseq_start_fresh(&seq, 0), LOXSEQ_OK);

    TEST_ASSERT_EQ_INT(loxseq_tick(&seq, 1), LOXSEQ_OK);
    TEST_ASSERT_EQ_INT(counter.called, 0);
    TEST_ASSERT_EQ_INT(seq.current_step, 0);
}

static void test_timeout_fails_and_returns_timeout(void) {
    test_storage_t ts;
    ts_reset(&ts);
    loxseq_storage_t storage = make_storage(&ts);

    call_counter_t counter;
    counter.called = 0;

    loxseq_step_def_t steps[] = {
        { .tag = "s0",
          .action = step_count_calls,
          .user = &counter,
          .timeout_ms = 10,
          .resume_policy = LOXSEQ_RESUME_AT_STEP },
    };

    loxseq_t seq;
    TEST_ASSERT_EQ_INT(loxseq_init(&seq, steps, 1, &storage), LOXSEQ_OK);
    TEST_ASSERT_EQ_INT(loxseq_start_fresh(&seq, 0), LOXSEQ_OK);

    /* age is now_ms - entered_at, so 11 triggers timeout */
    TEST_ASSERT_EQ_INT(loxseq_tick(&seq, 11), LOXSEQ_ERR_TIMEOUT);
    TEST_ASSERT_EQ_INT(loxseq_state(&seq), LOXSEQ_STATE_FAILED);
    TEST_ASSERT_EQ_INT(counter.called, 0);
}

static void test_pause_resume_and_tick_while_paused(void) {
    test_storage_t ts;
    ts_reset(&ts);
    loxseq_storage_t storage = make_storage(&ts);

    call_counter_t counter;
    counter.called = 0;

    loxseq_step_def_t steps[] = {
        { .tag = "s0", .action = step_count_calls, .user = &counter, .timeout_ms = 0, .resume_policy = LOXSEQ_RESUME_AT_STEP },
    };

    loxseq_t seq;
    TEST_ASSERT_EQ_INT(loxseq_init(&seq, steps, 1, &storage), LOXSEQ_OK);
    TEST_ASSERT_EQ_INT(loxseq_start_fresh(&seq, 0), LOXSEQ_OK);

    TEST_ASSERT_EQ_INT(loxseq_pause(&seq), LOXSEQ_OK);
    TEST_ASSERT_EQ_INT(loxseq_tick(&seq, 1), LOXSEQ_OK);
    TEST_ASSERT_EQ_INT(counter.called, 0);

    TEST_ASSERT_EQ_INT(loxseq_resume_paused(&seq, 2), LOXSEQ_OK);
    TEST_ASSERT_EQ_INT(loxseq_tick(&seq, 3), LOXSEQ_OK);
    TEST_ASSERT_EQ_INT(counter.called, 1);
}

static void test_abort_sets_failed(void) {
    test_storage_t ts;
    ts_reset(&ts);
    loxseq_storage_t storage = make_storage(&ts);

    static const loxseq_step_def_t steps[] = {
        { .tag = "s0", .action = step_running, .timeout_ms = 0, .resume_policy = LOXSEQ_RESUME_AT_STEP },
    };

    loxseq_t seq;
    TEST_ASSERT_EQ_INT(loxseq_init(&seq, steps, 1, &storage), LOXSEQ_OK);
    TEST_ASSERT_EQ_INT(loxseq_start_fresh(&seq, 0), LOXSEQ_OK);
    TEST_ASSERT_EQ_INT(loxseq_abort(&seq, 1), LOXSEQ_OK);
    TEST_ASSERT_EQ_INT(loxseq_state(&seq), LOXSEQ_STATE_FAILED);
}

static void test_complete_erases_checkpoint_and_erase_failure_is_storage_error(void) {
    test_storage_t ts;
    ts_reset(&ts);
    loxseq_storage_t storage = make_storage(&ts);

    static const loxseq_step_def_t steps[] = {
        { .tag = "a", .action = step_done, .timeout_ms = 0, .resume_policy = LOXSEQ_RESUME_AT_STEP },
        { .tag = "b", .action = step_done, .timeout_ms = 0, .resume_policy = LOXSEQ_RESUME_AT_STEP },
    };

    loxseq_t seq;
    TEST_ASSERT_EQ_INT(loxseq_init(&seq, steps, 2, &storage), LOXSEQ_OK);
    TEST_ASSERT_EQ_INT(loxseq_start_fresh(&seq, 0), LOXSEQ_OK);

    TEST_ASSERT_EQ_INT(loxseq_tick(&seq, 10), LOXSEQ_OK);
    TEST_ASSERT_EQ_INT(loxseq_tick(&seq, 20), LOXSEQ_OK);

    TEST_ASSERT_EQ_INT(loxseq_state(&seq), LOXSEQ_STATE_COMPLETE);
    TEST_ASSERT_EQ_INT(ts.have, 0);

    /* Force erase failure and complete again. */
    ts_reset(&ts);
    storage = make_storage(&ts);
    TEST_ASSERT_EQ_INT(loxseq_init(&seq, steps, 2, &storage), LOXSEQ_OK);
    TEST_ASSERT_EQ_INT(loxseq_start_fresh(&seq, 0), LOXSEQ_OK);
    ts.erase_rc = -1;
    TEST_ASSERT_EQ_INT(loxseq_complete(&seq, 0), LOXSEQ_ERR_STORAGE);
}

static void test_write_failure_on_transition_fails_sequence(void) {
    test_storage_t ts;
    ts_reset(&ts);
    loxseq_storage_t storage = make_storage(&ts);

    static const loxseq_step_def_t steps[] = {
        { .tag = "a", .action = step_done, .timeout_ms = 0, .resume_policy = LOXSEQ_RESUME_AT_STEP },
        { .tag = "b", .action = step_running, .timeout_ms = 0, .resume_policy = LOXSEQ_RESUME_AT_STEP },
    };

    loxseq_t seq;
    TEST_ASSERT_EQ_INT(loxseq_init(&seq, steps, 2, &storage), LOXSEQ_OK);
    TEST_ASSERT_EQ_INT(loxseq_start_fresh(&seq, 0), LOXSEQ_OK);

    ts.write_rc = -1;
    TEST_ASSERT_EQ_INT(loxseq_tick(&seq, 10), LOXSEQ_ERR_STORAGE);
    TEST_ASSERT_EQ_INT(loxseq_state(&seq), LOXSEQ_STATE_FAILED);
}

static void test_current_tag_and_invalid_states(void) {
    test_storage_t ts;
    ts_reset(&ts);
    loxseq_storage_t storage = make_storage(&ts);

    static const loxseq_step_def_t steps[] = {
        { .tag = "a", .action = step_running, .timeout_ms = 0, .resume_policy = LOXSEQ_RESUME_AT_STEP },
    };

    loxseq_t seq;
    TEST_ASSERT_EQ_INT(loxseq_init(&seq, steps, 1, &storage), LOXSEQ_OK);
    TEST_ASSERT(loxseq_current_tag(&seq) == NULL);

    TEST_ASSERT_EQ_INT(loxseq_start_fresh(&seq, 0), LOXSEQ_OK);
    TEST_ASSERT(loxseq_current_tag(&seq) != NULL);
    TEST_ASSERT(strcmp(loxseq_current_tag(&seq), "a") == 0);

    (void)loxseq_complete(&seq, 0);
    TEST_ASSERT(loxseq_current_tag(&seq) == NULL);
}

static void test_step_age_wraparound(void) {
    loxseq_t seq;
    memset(&seq, 0, sizeof(seq));
    seq.step_entered_at_ms = 0xFFFFFFF0U;
    TEST_ASSERT_EQ_U32(loxseq_step_age_ms(&seq, 0x00000010U), 0x20U);
}

static void test_recover_downgrade_table(void) {
    test_storage_t ts;
    ts_reset(&ts);
    loxseq_storage_t storage = make_storage(&ts);

    static const loxseq_step_def_t steps[] = {
        { .tag = "s0", .action = step_done, .timeout_ms = 0, .resume_policy = LOXSEQ_RESUME_AT_STEP },
        { .tag = "s1", .action = step_done, .timeout_ms = 0, .resume_policy = LOXSEQ_RESUME_FROM_START },
    };

    loxseq_t seq;
    TEST_ASSERT_EQ_INT(loxseq_init(&seq, steps, 2, &storage), LOXSEQ_OK);
    TEST_ASSERT_EQ_INT(loxseq_start_fresh(&seq, 0), LOXSEQ_OK);

    TEST_ASSERT_EQ_INT(loxseq_recover(&seq, LOXSEQ_REBOOT_NORMAL), LOXSEQ_RECOVERY_RESUME);
    TEST_ASSERT_EQ_INT(loxseq_recover(&seq, LOXSEQ_REBOOT_PANIC), LOXSEQ_RECOVERY_OPERATOR);

    /* Advance into step 1 and checkpoint. */
    TEST_ASSERT_EQ_INT(loxseq_tick(&seq, 1), LOXSEQ_OK);
    TEST_ASSERT_EQ_INT(seq.current_step, 1);

    TEST_ASSERT_EQ_INT(loxseq_recover(&seq, LOXSEQ_REBOOT_NORMAL), LOXSEQ_RECOVERY_RESTART);
    TEST_ASSERT_EQ_INT(loxseq_recover(&seq, LOXSEQ_REBOOT_OTA), LOXSEQ_RECOVERY_SAFE_INIT);
}

int main(void) {
    test_crc16_known_vector();
    test_init_invalid_args();
    test_recover_sets_last_recovery();
    test_recover_read_failure_is_cold_start();
    test_recover_corrupted_crc_is_cold_start();
    test_recover_unsupported_version_is_cold_start();
    test_recover_out_of_range_step_is_cold_start();
    test_start_resume_restart_step_entered_semantics();
    test_operator_resolve_restart_resets_step_entered();
    test_start_resume_out_of_range_step_is_format_error();
    test_reboot_counter_increment_and_saturation();
    test_branch_without_target_fails();
    test_branch_to_another_step();
    test_precondition_false_skips_action();
    test_timeout_fails_and_returns_timeout();
    test_pause_resume_and_tick_while_paused();
    test_abort_sets_failed();
    test_complete_erases_checkpoint_and_erase_failure_is_storage_error();
    test_write_failure_on_transition_fails_sequence();
    test_current_tag_and_invalid_states();
    test_step_age_wraparound();
    test_recover_downgrade_table();

    return g_test_failures ? 1 : 0;
}
