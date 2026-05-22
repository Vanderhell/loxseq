# loxseq — Storage contract

`loxseq` does not contain a storage backend. It calls back into a
caller-supplied `loxseq_storage_t` with fixed-size records. This
document is the contract those hooks must satisfy.

## The record

```c
typedef struct {
    uint8_t  version;                  /* = LOXSEQ_RECORD_VERSION (1) */
    uint8_t  reserved;
    uint16_t step_index;
    uint32_t step_entered_at_ms;
    uint32_t sequence_started_at_ms;
    uint32_t generation;
    uint8_t  reboot_count_in_step;
    uint8_t  flags;
    uint16_t crc16;
} loxseq_record_t;
```

`sizeof(loxseq_record_t)` is 24 bytes on typical 32-bit targets, with
4-byte alignment. The struct layout is stable: subsequent versions may
extend the reserved field but will not break version 1 layout.

The CRC covers all bytes of the record except the `crc16` field itself.
Use `loxseq_crc16()` (CRC-16/CCITT-FALSE) or supply the same algorithm.

## The hooks

```c
typedef struct {
    int (*write_checkpoint)(const void *buf, size_t len, void *user);
    int (*read_checkpoint) (void *buf, size_t len, void *user);
    int (*erase_checkpoint)(void *user);
    void *user;
} loxseq_storage_t;
```

All three return 0 on success, negative on failure.

### write_checkpoint

Called every time loxseq advances to a new step.

Requirements:
- **Atomic** with respect to power loss: a half-written record must be
  detectable. The recommended implementation keeps two slots and
  alternates; on read, the slot with the highest generation and a
  valid CRC wins.
- **Bounded latency**: should complete within a few milliseconds. The
  sequencer assumes a write returns before the next tick.
- **Flash-friendly**: writes are infrequent (one per step transition),
  but if your step count is high or your steps short, choose a backend
  with wear levelling.

### read_checkpoint

Called once during `loxseq_recover()`. Should return the most recent
valid record, or a negative value if no record exists.

If the backend keeps multiple slots, this is where you pick the best
one (highest `generation`, valid CRC).

### erase_checkpoint

Called when the sequence completes normally via `loxseq_complete()`.
The next `loxseq_recover()` should then return COLD_START.

## Two-slot pattern (recommended)

```c
/* In a NOR flash sector with two record-sized slots: */
typedef struct {
    loxseq_record_t a;
    loxseq_record_t b;
} two_slot_t;

static int my_write(const void *buf, size_t len, void *user) {
    (void)user;
    if (len != sizeof(loxseq_record_t)) return -1;

    loxseq_record_t a, b;
    flash_read(SLOT_A_OFFSET, &a, sizeof(a));
    flash_read(SLOT_B_OFFSET, &b, sizeof(b));

    bool a_valid = (loxseq_crc16(&a, sizeof(a)-2) == a.crc16);
    bool b_valid = (loxseq_crc16(&b, sizeof(b)-2) == b.crc16);

    /* Write to the older slot (or to A if neither is valid). */
    uint32_t target = SLOT_A_OFFSET;
    if (a_valid && b_valid) {
        target = (a.generation > b.generation) ? SLOT_B_OFFSET
                                               : SLOT_A_OFFSET;
    } else if (a_valid) {
        target = SLOT_B_OFFSET;
    }

    flash_erase_page(target);
    return flash_write(target, buf, len);
}

static int my_read(void *buf, size_t len, void *user) {
    (void)user;
    if (len != sizeof(loxseq_record_t)) return -1;

    loxseq_record_t a, b;
    flash_read(SLOT_A_OFFSET, &a, sizeof(a));
    flash_read(SLOT_B_OFFSET, &b, sizeof(b));

    bool a_valid = (loxseq_crc16(&a, sizeof(a)-2) == a.crc16);
    bool b_valid = (loxseq_crc16(&b, sizeof(b)-2) == b.crc16);

    if (!a_valid && !b_valid) return -1;
    if ( a_valid && !b_valid) { memcpy(buf, &a, len); return 0; }
    if (!a_valid &&  b_valid) { memcpy(buf, &b, len); return 0; }
    memcpy(buf, a.generation > b.generation ? &a : &b, len);
    return 0;
}
```

## nvlog backend

If you use `nvlog`, the backend is even simpler — nvlog already
guarantees atomic append and power-loss safety. The whole storage hook
is approximately:

```c
static int nvlog_write_cb(const void *buf, size_t len, void *user) {
    return nvlog_append((nvlog_t *)user, KEY_SEQ_RECORD, buf, len);
}
static int nvlog_read_cb(void *buf, size_t len, void *user) {
    size_t n = len;
    return nvlog_read_latest((nvlog_t *)user, KEY_SEQ_RECORD, buf, &n);
}
static int nvlog_erase_cb(void *user) {
    return nvlog_drop_key((nvlog_t *)user, KEY_SEQ_RECORD);
}

loxseq_storage_t storage = {
    .write_checkpoint = nvlog_write_cb,
    .read_checkpoint  = nvlog_read_cb,
    .erase_checkpoint = nvlog_erase_cb,
    .user             = &my_nvlog,
};
```

## RAM-only backend (for testing)

```c
static loxseq_record_t ram_slot;
static bool            ram_valid;

static int ram_write(const void *buf, size_t len, void *user) {
    (void)user;
    if (len != sizeof(ram_slot)) return -1;
    memcpy(&ram_slot, buf, len);
    ram_valid = true;
    return 0;
}

static int ram_read(void *buf, size_t len, void *user) {
    (void)user;
    if (!ram_valid)              return -1;
    if (len != sizeof(ram_slot)) return -1;
    memcpy(buf, &ram_slot, len);
    return 0;
}

static int ram_erase(void *user) {
    (void)user;
    ram_valid = false;
    return 0;
}
```

This is what `examples/minimal.c` uses. It has no power-loss safety
and is for unit tests only.

## Write rate analysis

A typical 8-step sequence completing once has 8 writes. If you have a
100 000 erase-cycle NOR flash and run the sequence 100 times per day,
that is 800 writes/day or 36 years before reaching the endurance
limit on a single slot (single-slot scheme; with two slots, double).

If your sequence completes 1000 times per day, the same analysis gives
3.6 years. At that rate, plan for wear levelling or move to FRAM.

`loxseq` itself does no in-step periodic checkpointing in v0.1. If
you need it later, the rate cap will be exposed as a configuration.
