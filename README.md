# loxseq

[![CI](https://github.com/Vanderhell/loxseq/actions/workflows/ci.yml/badge.svg)](https://github.com/Vanderhell/loxseq/actions/workflows/ci.yml)
[![Release](https://github.com/Vanderhell/loxseq/actions/workflows/release.yml/badge.svg)](https://github.com/Vanderhell/loxseq/actions/workflows/release.yml)

Power-loss-aware step sequencer for embedded C firmware.

loxseq is a small, heap-free C99 library that checkpoints step progress to caller-provided storage and computes a recovery verdict on reboot.

## Key ideas

- Persistent checkpoint record (CRC-protected)
- Per-step resume policy with conservative downgrades
- No heap / globals / floats; caller-owned state
- Tick-driven execution: you provide now_ms
- Optional branching via loxseq_set_next_step + LOXSEQ_STEP_BRANCH

## Minimal integration (examples)

### 1) Step table

    #include "loxseq/loxseq.h"

    enum { S_FILL, S_HEAT, S_DRAIN, STEP_COUNT };

    static loxseq_step_status_t step_fill(loxseq_t *s, uint32_t now_ms, void *u);
    static loxseq_step_status_t step_heat(loxseq_t *s, uint32_t now_ms, void *u);
    static loxseq_step_status_t step_drain(loxseq_t *s, uint32_t now_ms, void *u);

    static const loxseq_step_def_t steps[STEP_COUNT] = {
        [S_FILL]  = { .tag = "fill",  .action = step_fill,  .timeout_ms = 60000,  .resume_policy = LOXSEQ_RESUME_AT_STEP },
        [S_HEAT]  = { .tag = "heat",  .action = step_heat,  .timeout_ms = 600000, .resume_policy = LOXSEQ_RESUME_FROM_START },
        [S_DRAIN] = { .tag = "drain", .action = step_drain, .timeout_ms = 120000, .resume_policy = LOXSEQ_NEVER_RESUME },
    };

### 2) Storage hooks (RAM example, tests only)

    #include <string.h>

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

    static int ram_erase(void *user) { (void)user; ram_len = 0; return 0; }

    static const loxseq_storage_t storage = {
        .write_checkpoint = ram_write,
        .read_checkpoint  = ram_read,
        .erase_checkpoint = ram_erase,
    };

### 3) Boot flow

    loxseq_t seq;
    loxseq_init(&seq, steps, STEP_COUNT, &storage);

    loxseq_recovery_verdict_t v = loxseq_recover(&seq, reboot_reason);
    switch (v) {
        case LOXSEQ_RECOVERY_COLD_START: loxseq_start_fresh(&seq, now_ms); break;
        case LOXSEQ_RECOVERY_RESUME:     loxseq_start_resume(&seq, now_ms); break;
        case LOXSEQ_RECOVERY_RESTART:    loxseq_start_resume(&seq, now_ms); break;
        case LOXSEQ_RECOVERY_SAFE_INIT:  loxseq_start_safe_init(&seq, now_ms); break;
        case LOXSEQ_RECOVERY_OPERATOR:   loxseq_start_operator_wait(&seq); break;
    }

## Build & test

    cmake -S . -B build
    cmake --build build --config Release
    ctest --test-dir build --output-on-failure -C Release

## Docs

- docs/resume-model.md
- docs/storage-contract.md
- docs/integration.md
- docs/limitations.md

## License

MIT (see LICENSE).
