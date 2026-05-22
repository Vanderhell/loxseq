# loxseq — Integration with the Lox family

## Composition map

```
   reboot reason
   (from microboot)
        │
        ▼
   ┌──────────┐                          ┌──────────────┐
   │ loxseq   │◄── storage hooks ────────│ nvlog/loxdb  │
   │          │                          └──────────────┘
   └────┬─────┘
        │
   step action (your fn)
        │
   ├── reads conditions from loxperm (step gating)
   ├── checks loxalarm state (abort if alarms)
   ├── emits microlog events
   ├── uses microtimer for timing inside step
   └── coordinates via microbus for cross-module events
```

## With `microboot`

`microboot` provides the reboot reason. Pass it to `loxseq_recover`:

```c
microboot_reason_t mb_reason = microboot_get_reason();

loxseq_reboot_reason_t seq_reason;
switch (mb_reason) {
    case MICROBOOT_REASON_NORMAL:      seq_reason = LOXSEQ_REBOOT_NORMAL;      break;
    case MICROBOOT_REASON_POWER:       seq_reason = LOXSEQ_REBOOT_POWER_LOSS;  break;
    case MICROBOOT_REASON_WATCHDOG:    seq_reason = LOXSEQ_REBOOT_WATCHDOG;    break;
    case MICROBOOT_REASON_PANIC:       seq_reason = LOXSEQ_REBOOT_PANIC;       break;
    case MICROBOOT_REASON_OTA:         seq_reason = LOXSEQ_REBOOT_OTA;         break;
    default:                           seq_reason = LOXSEQ_REBOOT_UNKNOWN;     break;
}

loxseq_recovery_verdict_t v = loxseq_recover(&batch, seq_reason);
```

## With `nvlog`

The natural storage backend. See `docs/storage-contract.md` for the
hook implementation.

## With `loxperm`

Each step can declare a permissive precondition. The step is gated by
the chain:

```c
static const loxseq_step_def_t batch_steps[] = {
    [S_HEAT] = {
        .tag = "heat",
        .action           = action_heat,
        .precondition     = (loxseq_precond_fn)loxperm_is_permitted,
        .precondition_ctx = &heat_perms,
        .timeout_ms       = 600000,
        .resume_policy    = LOXSEQ_RESUME_AT_STEP,
    },
    ...
};
```

If `loxperm_is_permitted()` returns false, the action does not run and
the tick is a no-op. The sequence does not advance until the
permissive clears.

## With `loxalarm`

Two patterns. First, gate step entry on alarms being clear:

```c
static bool no_critical_alarms(void *ctx) {
    (void)ctx;
    return !lox_alarm_needs_attention(&pressure_alarm) &&
           !lox_alarm_needs_attention(&temp_alarm);
}

step.precondition = no_critical_alarms;
```

Second, abort sequence on alarm:

```c
static loxseq_step_status_t action_heat(loxseq_t *seq, uint32_t now_ms,
                                        void *user) {
    if (lox_alarm_is_active(&pressure_alarm)) {
        return LOXSEQ_STEP_FAILED;
    }
    /* normal heating logic */
}
```

## With `microtimer`

For timed waits inside a step:

```c
static loxseq_step_status_t action_hold(loxseq_t *seq, uint32_t now_ms,
                                        void *user) {
    if (loxseq_step_age_ms(seq, now_ms) >= 30 * 60 * 1000) {
        return LOXSEQ_STEP_DONE;
    }
    return LOXSEQ_STEP_RUNNING;
}
```

`loxseq_step_age_ms` gives the elapsed time in the current step from
its `step_entered_at_ms` to `now_ms`. After a resume with
`RESUME_AT_STEP`, this elapsed time is preserved (it counts from the
*original* entry, before the reboot). After `RESUME_FROM_START`, it
resets to 0.

## With `microlog`

Log every step transition. `loxseq` does not log internally; it relies
on the application to emit events from the action callback:

```c
static loxseq_step_status_t wrapping_action(loxseq_t *seq, uint32_t now_ms,
                                            void *user) {
    static int last_step = -1;
    if (seq->current_step != last_step) {
        microlog_emit_event(&log, "STEP_ENTER",
                            loxseq_current_tag(seq));
        last_step = seq->current_step;
    }
    return real_action(seq, now_ms, user);
}
```

Or use a simple hook on transitions if you instrument `tick`.

## With `microsh`

Operator shell commands:

```c
static int sh_seq_status(int argc, char **argv) {
    (void)argc; (void)argv;
    printf("state: %d, step: %d (%s), age: %u ms\n",
           loxseq_state(&batch),
           batch.current_step,
           loxseq_current_tag(&batch),
           loxseq_step_age_ms(&batch, now_ms()));
    return 0;
}

static int sh_seq_abort(int argc, char **argv) {
    (void)argc; (void)argv;
    return loxseq_abort(&batch, now_ms());
}

static int sh_seq_operator(int argc, char **argv) {
    if (argc < 2) return -1;
    loxseq_recovery_verdict_t v = LOXSEQ_RECOVERY_SAFE_INIT;
    if      (!strcmp(argv[1], "resume"))    v = LOXSEQ_RECOVERY_RESUME;
    else if (!strcmp(argv[1], "restart"))   v = LOXSEQ_RECOVERY_RESTART;
    else if (!strcmp(argv[1], "safe-init")) v = LOXSEQ_RECOVERY_SAFE_INIT;
    else return -1;
    return loxseq_operator_resolve(&batch, v, now_ms());
}

microsh_register(&sh, "seq",          sh_seq_status);
microsh_register(&sh, "seq-abort",    sh_seq_abort);
microsh_register(&sh, "seq-operator", sh_seq_operator);
```

## With `microfsm`

A complex step's action can itself be a `microfsm` state machine.
`loxseq` handles the outer step transitions; `microfsm` handles the
inner sub-states of a single step.

```c
static loxseq_step_status_t action_complex_heat(loxseq_t *seq,
                                                uint32_t now_ms,
                                                void *user) {
    microfsm_run(&heat_fsm, now_ms);
    if (microfsm_in_state(&heat_fsm, HEAT_DONE)) {
        return LOXSEQ_STEP_DONE;
    }
    if (microfsm_in_state(&heat_fsm, HEAT_FAULT)) {
        return LOXSEQ_STEP_FAILED;
    }
    return LOXSEQ_STEP_RUNNING;
}
```

## With `loxguard`

Wrap each action invocation in a guard block to capture faults without
crashing the sequencer:

```c
LOX_GUARD_BLOCK(seq_tick_guard) {
    loxseq_tick(&batch, now_ms);
}

if (LOX_GUARD_FAILED(seq_tick_guard)) {
    /* the step action faulted; abort and go safe */
    loxseq_abort(&batch, now_ms);
}
```
