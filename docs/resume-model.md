# loxseq — Resume model

`loxseq` is built around one central question: **what should happen if
the device reboots while a sequence was running?**

The answer is never simply "continue where you left off". Sometimes
that is right, sometimes it is dangerous, sometimes it depends on
operator judgement. This document describes the four resume policies
and when each is appropriate.

## The four policies

### LOXSEQ_RESUME_AT_STEP

Continue this step from where it left off.

**Required precondition:** the action must be idempotent. If the step
"open valve V1" reboots midway, the resumed action must call
"open valve V1" again without harmful consequence.

**Good fit:**
- Steps that command a state and verify it (open valve, wait for
  position feedback).
- Steady-state phases (heat to setpoint, hold for duration).
- Pure-software steps (compute, log, message).

**Bad fit:**
- Steps that issue a *transient* command (pulse output, increment a
  counter). On resume, the increment happens twice.
- Steps whose duration matters and was already partly elapsed when
  power failed. The library tracks `step_entered_at_ms`, but on
  resume that timestamp is from the previous power session. Either
  reset it (RESUME_FROM_START semantics) or have your action read it
  and decide.

### LOXSEQ_RESUME_FROM_START

The sequence is at the right step; restart the step's logic from its
own beginning. `step_entered_at_ms` is updated to `now_ms` on resume.

**Good fit:**
- Steps with a definite "start phase" that is itself idempotent
  (initialise PID, command actuator to home, etc.) but the elapsed time
  inside the step should not carry over.
- Heating cycles where the temperature has dropped during the outage
  and the resumed step must re-establish setpoint.

**Bad fit:**
- Steps that change state and must not be redone (e.g. dispense exactly
  one dose).

### LOXSEQ_NEVER_RESUME

Abort the sequence. Go to safe init.

**Good fit:**
- Drain / discharge / vent steps that interact with materials of
  unknown state after a power event.
- Calibration sequences where partial completion produces invalid
  data.
- High-risk steps (sterilisation, dosing, pressure tests) where the
  cost of incorrect resume far exceeds the cost of restarting the
  whole sequence.

What "safe init" means is your responsibility. Typically it means
running a special initialisation sequence that brings all actuators to
a defined neutral state (valves closed, heaters off, motors stopped).

### LOXSEQ_OPERATOR_DECIDES

Present the question to a human. The sequencer enters
`LOXSEQ_STATE_AWAIT_OP` and stops processing until the application
calls `loxseq_operator_resolve()` with a verdict.

**Good fit:**
- The default. If you have not analysed the resume behaviour of a step,
  do not let the firmware decide.
- Steps where the right answer depends on circumstances the firmware
  cannot know (was the operator present? was material loaded? was the
  outage 2 seconds or 2 hours?).

**Implementation note:** the verdict the operator provides must be one
of RESUME, RESTART, or SAFE_INIT.

## The decision flow on reboot

```
                  loxseq_recover()
                       │
        reads saved checkpoint
                       │
   ┌───────────────────┼───────────────────┐
   ▼                                       ▼
no record / corrupted                  valid record
   │                                       │
   ▼                                       ▼
COLD_START                       inspect reboot_reason
                                           │
                       ┌───────────────────┼───────────────────┐
                       ▼                                       ▼
                 NORMAL                              POWER_LOSS / WATCHDOG /
                                                    PANIC / OTA
                       │                                       │
                       ▼                                       ▼
              honour step's                          honour step's
              resume_policy                          resume_policy
                                                    (but downgrade
                                                    aggressive policies)
```

The policy is per-step. The reboot reason can downgrade an aggressive
policy: a step set to `RESUME_AT_STEP` will become `OPERATOR_DECIDES` if
the reboot was a `PANIC`, because the system was not in a known state at
the moment of crash.

The downgrade table:

| Step policy            | NORMAL    | POWER_LOSS | WATCHDOG  | PANIC     | OTA       |
|------------------------|-----------|------------|-----------|-----------|-----------|
| RESUME_AT_STEP         | resume    | resume     | resume    | operator  | operator  |
| RESUME_FROM_START      | restart   | restart    | restart   | operator  | safe_init |
| NEVER_RESUME           | safe_init | safe_init  | safe_init | safe_init | safe_init |
| OPERATOR_DECIDES       | operator  | operator   | operator  | operator  | operator  |

The downgrades are conservative on purpose: a PANIC means the firmware
was in an unexpected state; an OTA means the firmware that ran the
step is no longer the firmware running now (its step indices may not
even match).

## Practical examples

### Example: batch reactor

```
S_PURGE         RESUME_FROM_START   (re-start purge if reboot)
S_FILL          RESUME_AT_STEP      (idempotent: fill to level setpoint)
S_HEAT          RESUME_AT_STEP      (idempotent: heat to setpoint)
S_DOSE          NEVER_RESUME        (cannot dose twice)
S_MIX           RESUME_AT_STEP      (idempotent if mixer is open-loop)
S_DRAIN         OPERATOR_DECIDES    (depends on what's in the tank)
```

### Example: stepper-based dispenser

```
S_HOME          RESUME_FROM_START   (always re-home after reboot)
S_LOAD          OPERATOR_DECIDES    (was something loaded?)
S_DISPENSE      NEVER_RESUME        (cannot partial-dispense safely)
S_PURGE         RESUME_FROM_START
```

### Example: charging cycle

```
S_PRECHECK      RESUME_FROM_START
S_BULK          RESUME_AT_STEP       (current limit re-applied)
S_ABSORPTION    RESUME_AT_STEP
S_FLOAT         RESUME_AT_STEP
S_BALANCE       OPERATOR_DECIDES     (cell voltages were mid-balance)
```

## What `loxseq` does NOT decide for you

- Whether your action is *actually* idempotent. You declare a policy;
  the library trusts you.
- Whether the physical state outside the firmware (fluid levels,
  positions, temperatures) is consistent with the saved step.
- Whether the operator has actually returned to the panel before you
  trust an `OPERATOR_DECIDES` answer.
- How long the outage was. That information is rarely available
  precisely; if you have a backup RTC, your application can use it.

`loxseq` is the mechanism. The safety case is yours.
