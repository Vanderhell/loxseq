#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "loxseq/loxseq.h"

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

static const loxseq_storage_t ram_storage = {
    .write_checkpoint = ram_write,
    .read_checkpoint = ram_read,
    .erase_checkpoint = ram_erase,
    .user = NULL,
};

static loxseq_step_status_t step_done(loxseq_t *seq, uint32_t now_ms, void *user) {
    (void)seq;
    (void)now_ms;
    (void)user;
    return LOXSEQ_STEP_DONE;
}

static void test_crc16_known_vector(void) {
    const char *s = "123456789";
    /* CRC-16/CCITT-FALSE("123456789") == 0x29B1 */
    assert(loxseq_crc16(s, 9) == 0x29B1);
}

static void test_recover_downgrade_table(void) {
    static const loxseq_step_def_t steps[] = {
        { .tag = "s0", .action = step_done, .timeout_ms = 0, .resume_policy = LOXSEQ_RESUME_AT_STEP },
        { .tag = "s1", .action = step_done, .timeout_ms = 0, .resume_policy = LOXSEQ_RESUME_FROM_START },
    };

    loxseq_t seq;
    assert(loxseq_init(&seq, steps, 2, &ram_storage) == LOXSEQ_OK);

    assert(loxseq_start_fresh(&seq, 0) == LOXSEQ_OK);

    assert(loxseq_recover(&seq, LOXSEQ_REBOOT_NORMAL) == LOXSEQ_RECOVERY_RESUME);
    assert(loxseq_recover(&seq, LOXSEQ_REBOOT_PANIC) == LOXSEQ_RECOVERY_OPERATOR);

    /* Advance into step 1 and checkpoint. */
    assert(loxseq_tick(&seq, 1) == LOXSEQ_OK);
    assert(seq.current_step == 1);

    assert(loxseq_recover(&seq, LOXSEQ_REBOOT_NORMAL) == LOXSEQ_RECOVERY_RESTART);
    assert(loxseq_recover(&seq, LOXSEQ_REBOOT_OTA) == LOXSEQ_RECOVERY_SAFE_INIT);
}

static void test_happy_path_completes_and_erases_checkpoint(void) {
    static const loxseq_step_def_t steps[] = {
        { .tag = "a", .action = step_done, .timeout_ms = 0, .resume_policy = LOXSEQ_RESUME_AT_STEP },
        { .tag = "b", .action = step_done, .timeout_ms = 0, .resume_policy = LOXSEQ_RESUME_AT_STEP },
    };

    loxseq_t seq;
    assert(loxseq_init(&seq, steps, 2, &ram_storage) == LOXSEQ_OK);
    assert(loxseq_start_fresh(&seq, 0) == LOXSEQ_OK);

    assert(loxseq_tick(&seq, 10) == LOXSEQ_OK);
    assert(loxseq_tick(&seq, 20) == LOXSEQ_OK);

    assert(loxseq_state(&seq) == LOXSEQ_STATE_COMPLETE);
    assert(loxseq_recover(&seq, LOXSEQ_REBOOT_NORMAL) == LOXSEQ_RECOVERY_COLD_START);
}

int main(void) {
    test_crc16_known_vector();
    test_recover_downgrade_table();
    test_happy_path_completes_and_erases_checkpoint();
    return 0;
}

