# Business Rules — V0

Status: **Approved baseline**

These rules describe product behavior independently of ESP-IDF, GPIO, FreeRTOS, sensor chips, SSR models, UI framework, or cloud services.

`V0` in this document means the product behavior baseline, not a claim that
every rule is implemented by M5. M0-M5 deliver the simulated
application/control slice. Rules requiring real hardware or persistence are
scheduled by `docs/ROADMAP.md`; `docs/TRACEABILITY.md` records the current
implementation and validation status of every rule.

## Core smoker rules

### BR-001 — One active session

At most one cooking session may be active at a time.

### BR-002 — Chamber and food temperatures are different concepts

Chamber temperature is the process-control temperature.

Food-probe temperatures are measurements associated with individual products/pieces.

### BR-003 — One authoritative chamber temperature

Exactly one chamber-temperature source is authoritative for heater control.

Additional chamber/ambient measurements may exist later for monitoring or diagnostics, but they do not implicitly participate in control.

### BR-004 — 1..N food probes

The controller supports one or more food probes.

Current available external probe capacity is six, but the domain model must not hard-code six as a universal product rule.

### BR-005 — Food probes never control the heater

A food probe must never directly:

- change heater output;
- change chamber setpoint;
- participate in the chamber control algorithm.

A probe threshold may trigger an event, alarm, notification, or timer-start condition.

### BR-006 — Food-probe properties

Each probe exposes at least:

- `id`
- `name`
- `role`
- `currentTemperature`
- `targetTemperature`
- `enabled`
- `alarmEnabled`

### BR-007 — Probe target behavior

Reaching a food-probe target emits an event/alarm when alarms are enabled.

It does not stop the session or change heater control in V0.

In the M5 slice, target alarms are active-session behavior and latch once per
session/target configuration. They are re-armed by an explicit target change or
new session. Acknowledgement and lifecycle resolution are separate concepts.

### BR-008 — Recipes are stage-based; V0 has one stage

The long-term recipe model supports stages.

V0 implements exactly one stage.

Future multi-stage capability must not be implemented early.

### BR-009 — Preheat/cook/keep-warm are not special domain states

Names such as `Preheat`, `Cook`, or `Keep warm` are stage labels/configurations, not hard-coded state-machine concepts.

### BR-010 — Optional stage timer

A stage may have an optional timer.

A timer may start:

- immediately;
- when chamber temperature reaches a configured threshold;
- when a selected food probe reaches a configured threshold.

For a selected food-probe start condition, disabled, disconnected, or otherwise
invalid probe input is treated as an absent reading: the timer remains unstarted.
Re-enabling or reconnecting the probe may start the timer on a later valid sample
that meets the threshold.

Once started, the timer continues even if the triggering temperature later falls below the threshold.

### BR-011 — Power-loss recovery is user-configurable

A persisted device setting determines whether an interrupted active session may resume after reboot/power recovery.

V0 setting:

`resumeAfterPowerFailure: true | false`

No advanced outage-duration heuristics are required in V0.

Persistence and recovery are implemented at M10. M0-M5 must still start with
heating OFF, but they do not claim to implement BR-011.

### BR-012 — Recipe values are user-selected process parameters

The controller executes the configured recipe/process.

There is no separate `FoodSafetyRule` or `FoodSafe` domain engine in V0.

This permits use cases such as monitoring/cold smoking with an external smoke source.

The controller does not certify food as safe.

## Session rules

### SR-001 — Session represents one run

A `Session` represents one smoker run.

### SR-002 — Start snapshots the recipe

Starting creates a new session using a snapshot of the selected recipe/configuration.

Editing the saved recipe later must not silently change the active session.

### SR-003 — Manual Stop is terminal

Manual Stop:

1. forces heater demand to OFF;
2. stops the timer;
3. ends the session;
4. records the stop reason.

A manually stopped session does not auto-resume.

### SR-004 — Live chamber-target change

The active chamber target may be changed during a running session within device safety limits.

This does not automatically modify the saved recipe.

### SR-005 — Live probe-target change

Probe targets may be changed during a running session.

This does not automatically modify the saved recipe.

It also does not modify persisted/default probe configuration. A new session
starts from the configured probe defaults; persistence of those defaults is M10.

### SR-006 — V0 session states

V0 session states are:

- `IDLE`
- `RUNNING`
- `STOPPED`
- `FAULT`

Do not add `PAUSED`, `PREHEAT`, `HOLD`, or `DONE` as V0 states.

### SR-007 — No Pause in V0

Pause behavior is explicitly deferred.

## Recipe and timer rules

### RR-001 — Recipe is reusable configuration

Recipe is reusable configuration, not runtime session state.

### RR-002 — Exactly one stage in V0

V0 recipes contain exactly one stage.

### RR-003 — V0 stage contents

A V0 stage contains:

- name;
- optional chamber target;
- optional timer.

A missing chamber target means chamber monitoring only and heater forced OFF.

### TR-001 — V0 timer-start conditions

Supported timer-start conditions:

- immediate;
- chamber temperature at least X;
- selected probe temperature at least X.

A probe-threshold timer waits while its selected probe has no enabled, valid
reading. It may start when a later enabled/reconnected sample reaches X.

### TR-002 — Timer trigger is one-way

Once the timer starts, later temperature movement does not pause/reset it.

### TR-003 — Timer completion action

V0 timer completion supports:

- `NOTIFY`
- `STOP_SESSION`

Default behavior should be notification unless a recipe explicitly requests stopping.

## Event/alarm/fault semantics

### EV-001 — Events

Events are informational facts such as:

- session started;
- timer started;
- target changed;
- probe connected/disconnected;
- device booted.

### EV-002 — Alarms

Alarms request user attention but do not inherently alter heater control.

Examples:

- food-probe target reached;
- non-authoritative food probe disconnected;
- timer completed with notify action.

Alarm lifecycle uses two independent facts:

- `acknowledged`: the user has seen/acknowledged the alarm;
- `resolved`: the associated session/condition no longer makes the alarm active.

Snapshots expose unresolved alarms as active, including unresolved alarms that
have already been acknowledged. Probe target/disconnection alarms are not
raised outside `RUNNING`. Reconnection resolves a disconnection alarm; ending a
session resolves its active session alarms.

### EV-003 — Faults

Faults represent conditions where normal safe control cannot continue.

A fault forces heater OFF.

## History rules

### HR-001 — History exists only for sessions

Durable telemetry is recorded only from the start through the end of a cooking
session. Idle operation outside a session is not telemetry.

### HR-002 — Bounded sampling plus immediate lifecycle/change records

While a session is `RUNNING`, the controller records one complete control
snapshot every 60 seconds. Start, end, target/probe configuration, timer,
alarm, and fault transitions are recorded on the first observed control cycle
rather than waiting for the next periodic sample.

### HR-003 — History is auxiliary

History corruption, queue overflow, write failure, flash maintenance, OTA
activity, missing UTC, and network loss must never alter session state, timer
behavior, safety evaluation, or heater output. Repeated storage failure changes
history health to `FAILED`; it does not raise an application fault.

### HR-004 — Local circular retention

History uses a bounded 4 MiB internal-flash circular log. Completed sessions
are evicted oldest first. No fixed retention duration is promised; APIs report
actual capacity and usage. If one session fills the partition, its newest pages
remain available and the session is marked truncated.

### HR-005 — Monotonic elapsed time is authoritative

Every record includes monotonic session elapsed time. Synchronized Unix UTC is
optional display/audit context and may first appear during a session; clock
synchronization or correction never changes elapsed duration or timers.

### HR-006 — Reboot interruption is visible, not recovery

A durable session without an end record after reboot is exposed as
interrupted. History does not resume heating or implement M10 session recovery.

### HR-007 — History access is authenticated and read-only

History APIs and the chart are available only on the authenticated operational
STA surface. Commissioning SoftAP cannot read history. M14 provides no erase,
delete, CSV, upload, or safety-bypass operation.

### HR-008 — Durable history identity is independent

Each stored session receives a monotonically ordered 64-bit history identifier
that survives reboot while retained. It is independent of application session
IDs, which may restart after reboot, and is represented as a decimal string in
JSON.

## Remote-access rules

### RA-001 — Blynk is an auxiliary personal client

M15 serves one owner and one home smoker through Blynk. Blynk availability,
connectivity, notification delivery, retention, or quota never participates in
local control, timers, safety evaluation, or heater output.

### RA-002 — Remote commands preserve local semantics

Remote Start, Stop, setting, acknowledgement, resolved-fault clear, and OTA
requests enter through the same bounded application command and permission
paths as their local equivalents. MQTT delivery is not semantic success, and
no remote action may command heater output directly or bypass validation and
safety.

A remote Start gesture carries its Start intent and optional chamber target in
one bounded message. A split multi-message Start protocol must fail closed
rather than infer a default target or retain a target for a later message. The
exact `0` emitted when a Push control is released is a UI reset gesture, not a
Start command; it is ignored without application work or remote-error feedback.

### RA-003 — Remote status is change-driven and throttled

The device publishes one current remote-status projection after Blynk
connect/reconnect. It then publishes only after a normalized user-visible value
changes, at most once in any five-second interval. Changes inside the interval
are coalesced into the newest complete projection. An unchanged projection is
never retransmitted merely because time passed.

### RA-004 — Critical remote feedback is immediate and separate

Correlated command results and configured critical events such as faults,
alarms, session completion, and OTA result may be published immediately. They
do not force an unchanged status snapshot and are not used as control or safety
inputs.

### RA-005 — Reconnect never replays an energizing command

A Blynk reconnect must not replay, restore, or synchronize a retained Start or
OTA-install control value. Start and install each require a new live user
request and remain subject to their existing application interlocks.

### RA-006 — Remote OTA reuses the fixed signed M13 path

Blynk may request an update check or installation, but it does not supply an
arbitrary URL or image. The ESP32 retrieves the latest fixed public GitHub
Release asset and applies all M13 version, target, signature, running-session,
rollback, and validation rules.
