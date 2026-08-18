# Control and Safety — V0

Status: **Approved baseline**

This file specifies firmware/product safety behavior. It does not replace electrical design review or independent hardware protection.

The rules span the product V0 roadmap. M0-M5 validate the simulated safety gate
and configure watchdog support; M6A target-validates the current simulated
runtime and TWDT behavior. These milestones do not implement
persistence/recovery or prove external electrical behavior. See
`docs/TRACEABILITY.md` for the validation level of each rule.

## Control rules

### CR-001 — Authoritative chamber temperature only

Only the authoritative chamber temperature participates in heater control.

Food probes do not participate in the heater-control algorithm.

### CR-002 — Chamber target is optional

A stage may have no chamber target.

When chamber target is absent:

- chamber temperature may still be measured/logged;
- heater demand must remain OFF.

This supports monitoring/cold-smoke use cases with external equipment.

### CR-003 — Domain control returns normalized demand

Control logic produces a normalized heater demand, conceptually `0..100%`.

The domain does not directly command GPIO/SSR ON/OFF timing.

Conversion from normalized demand to an electrical switching strategy belongs to the platform/heater driver.

### CR-004 — Live target changes respect safety limits

The active chamber target may be changed while running, but it must remain within device safety configuration.

### CR-005 — Heating requires RUNNING

Heating is allowed only while session status is `RUNNING`.

For `IDLE`, `STOPPED`, `FAULT`, and boot/recovery initialization, heater output is OFF.

### CR-006 — PID is an implementation detail

The business requirement is to control chamber temperature around its target.

PID/hysteresis/tuning/window timing are technical implementation choices and are not fixed by V0 business rules.

D055 and M8 select Espressif's official `espressif/pid_ctrl` component for the
future real controller. This technical selection does not change the rule:
PID output is only a requested normalized demand, safety is applied afterward,
and no tuning value is approved before real M6B/M7/M8 hardware validation.

## Safety rules

### SF-001 — Safety overrides everything

Priority:

```text
Safety
  > user/session/recipe intent
  > temperature-control demand
  > heater driver
```

Safety may always force heater demand to zero.

### SF-002 — Invalid authoritative chamber measurement

If the authoritative chamber temperature becomes invalid according to the implemented validity policy:

- raise `FAULT`;
- force heater OFF.

Do not continue by using the last known temperature.

The exact transient/read-retry policy is technical and must be documented when the real sensor driver is implemented.

### SF-003 — Independent device maximum chamber temperature

Device configuration has a maximum chamber-temperature safety limit independent of recipe target.

A recipe/live target cannot override this limit.

If measured chamber temperature exceeds the configured safety maximum:

- raise `FAULT`;
- force heater OFF.

The numeric value is intentionally deferred until actual smoker/heater/hardware protection is confirmed.

### SF-004 — Boot/reset begins safe

Every power-on/reset starts with heating disabled.

Before heating can be permitted:

- critical initialization must complete;
- reset reason should be recorded where available;
- required configuration must be valid;
- authoritative chamber sensing must be valid;
- session recovery policy must be evaluated.

M0-M5 implement the initial OFF write, structural simulation configuration
checks, valid authoritative simulated input, and the synchronous safety gate.
Reset-reason recording and recovery-policy evaluation are M10 work after the
target controller board is identified at M6A. Therefore M5 is not a complete
implementation of all SF-004 bullets.

### SF-005 — Detect control-path stalls

The implementation must use appropriate watchdog mechanisms so a stalled critical control path is detectable.

Watchdog design is a technical architecture concern; the business requirement is that a software stall must not be treated as normal control operation.

M5 configures and compiles startup TWDT initialization, a five-second timeout,
panic/reset behavior, and explicit `ControlTask` subscription/reset. M6A
target-validates this behavior: a deliberate seven-second subscribed-task stall
identified `ControlTask` as overdue, triggered panic/reset, and produced the
watchdog reset reason on the following boot. The temporary stall was removed
and the normal image restored and revalidated.

### SF-006 — Brownout/reset recovery

Reset/brownout reason should be recorded when available.

Recovery then follows `resumeAfterPowerFailure`.

Even when automatic resume is enabled, heater output starts OFF and is allowed only after critical validation.

SF-006 is scheduled for M10 and is not implemented by the M0-M5 simulated slice.

### SF-007 — V0 faults are latched

A V0 fault does not automatically restart heating when the underlying signal recovers.

The fault may be acknowledged/cleared only after the condition is no longer active.

Clearing a fault does not itself restart heating; a user/recovery action must explicitly return the session to an allowed running state.

### SF-008 — Food-probe failures are alarms

A food-probe failure/disconnection is not a heater-control fault because food probes are not control inputs.

Session control may continue.

### SF-009 — Network/UI/cloud failures do not affect local control

Loss of:

- Wi-Fi;
- browser;
- phone;
- cloud service;

must not stop or alter local control/timers/safety.

M12 enforces this structurally: HTTP commands cross a bounded SPSC mailbox,
snapshots cross a non-blocking preallocated exchange, and no network callback
can call `SmokerApplication::submit()` or write heater output. `ControlTask`
continues when connectivity initialization fails. Final-board Wi-Fi-loss testing
during RUNNING remains required before M12 completion.

M14 applies the same auxiliary boundary to history: post-cycle observations use
a bounded drop-on-full SPSC mailbox, and `HistoryTask` alone owns storage. A
history failure never becomes an application fault or heater/timer input.

M15 applies it to Blynk as well. MQTT callbacks may only publish bounded
transport requests and consume immutable snapshots; they cannot submit to the
application, write heater output, or block `ControlTask`. Blynk/Internet loss
can make remote status stale or unavailable, but cannot alter an active local
session.

### SF-010 — No safety bypass

No UI/API/debug command may bypass safety to force heater ON.

Any future manual heater test must still pass through the same safety gate.

Remote Blynk controls are UI commands under this rule. In particular, a remote
Start is an ordinary `StartSessionCommand`, not a heater command, and must
receive the application's semantic acceptance before the UI reports success.

### SF-011 — Independent hardware protection is required

Firmware and the SSR are not the only safety barriers.

The final electrical design must include an independent means of interrupting heater power if software or SSR control fails.

Exact hardware is intentionally deferred until component inventory/design is finalized.

SF-011 cannot be validated before M6B external-hardware identification and
independent electrical design review. M6A controller-board validation, a
successful build, or simulation is never evidence of this rule.

### SF-012 — SSR stuck-ON detection is future work

Detection of unexpected heater current while commanded OFF is a desired future capability.

It is not part of V0 and must not be simulated as if real current sensing exists.

## OTA safety rules

### OTA-001

OTA implementation belongs to `smoker_app`/`smoker_platform`, never `smoker_core`.

M13 keeps semantic-version/status helpers and the ESP-IDF service in
`smoker_platform`; `smoker_app` owns only the permission commands and Start
interlock. The architecture guardrail rejects platform includes from core.

### OTA-002

Firmware installation and update-triggered reboot are forbidden while a session is `RUNNING`.

M13 checks the immutable session snapshot before admission and then requires a
correlated, semantically accepted `PrepareFirmwareUpdateCommand`. Start remains
rejected until internal finish or successful pending-image validation.

An M15 Blynk install request uses this same path. Blynk cannot stop a running
session implicitly, override the permission result, provide another download
URL, or force reboot as part of OTA admission.

### OTA-003

V0 may check update availability while running.

Download/install occurs only when not running.

The manual descriptor-only check does not reserve application state. The
check parses the real ESP image prefix and rejects a non-ESP32-S3 chip ID before
offering an update. The install path repeats the descriptor validation after
permission is granted. Automatic redirects are disabled, every manually
followed hop must remain HTTPS, and finite permission/check/install deadlines
release the application interlock on failure.

Every initial M13 and OTA application image must carry an ESP-IDF RSA-3072
signature. `esp_ota_end()` rejects an image whose signature does not match the
public-key digest from the currently running signed app. The private key is
excluded from source and ordinary CI, and the tag-restricted release step verifies
its output against the versioned public key before publication. This protects
the network/release-asset path, not a physical attacker able to rewrite the
currently running app or bootloader while hardware Secure Boot is disabled.

### OTA-004

The M13 custom table provides `otadata` and two equal 3 MiB OTA slots on the
M6A-confirmed 16 MiB flash. App rollback is enabled. The first migration from
M12 requires a full serial flash.

### OTA-005

After installing new firmware, critical controller initialization must succeed before the new image is marked valid.

For `PENDING_VERIFY`, Start is blocked and five consecutive ControlTask cycles
must be `IDLE`, have a valid authoritative chamber measurement, have no fault,
command heater OFF, and reset TWDT successfully. A fault, ten-second timeout,
mark-valid error, runtime-context allocation failure, or ControlTask/OtaTask
creation failure triggers rollback and reboot. This prevents a failed critical
bootstrap from leaving a pending image alive until a manual reset. This is
software policy; target proof remains pending until the documented two-slot
scenario is run.

### OTA-006

OTA/network behavior must never become a dependency of the critical control loop.

One low-priority static `OtaTask` on core 0 owns SNTP, HTTPS, flash, and reboot.
It is not subscribed to TWDT. Its stack and task control block are pinned to
internal DRAM so flash-cache suspension cannot make an executing task's stack
inaccessible through PSRAM. ControlTask exchanges bounded atomic Prepare and
Finish signals and publishes the evidence already produced by its normal
cycle. Prepare is retried before Finish may be consumed, preventing a timeout
from overtaking and orphaning its reservation. Loss of Internet or synchronized
time makes the update fail explicitly without stopping local control. If
`OtaTask` cannot be created on an ordinary boot, the firmware service reports
`FAILED` and rejects check/install requests rather than accepting work that no
task can process.

## M14 history safety boundary

Durable history is evidence, never an input to control or recovery. The
post-safety `ControlTask` path may only copy a complete immutable observation
into a preallocated SPSC mailbox; it cannot wait for storage, acquire the
flash-operation owner, encode a record, or call a flash API. Overflow drops an
observation and increments history health without raising an application fault.

Only the low-priority core-0 `HistoryTask` writes the history partition and it
is outside TWDT. OTA defers new history leases and serializes its flash work
with any bounded operation already in progress. Torn/corrupt history,
uninitialized media, repeated write failure, unavailable UTC, and Wi-Fi loss
can make history incomplete or `DEGRADED`/`FAILED`; none may alter the session,
timer, authoritative chamber input, safety gate, or final heater command. This
software isolation is not evidence of sensor, SSR, thermal, or independent
electrical-safety behavior.

## M15 remote-access safety boundary

Blynk is a replaceable, non-critical transport. A platform-owned low-priority
service may connect to Blynk over MQTT/TLS, translate an allowlisted control
into the existing bounded mailbox, and project immutable snapshots outward.
Only `ControlTask` submits the translated command. Remote status is
change-driven and may be delayed or dropped; no control decision may wait for
its publication or for a Blynk acknowledgement.

Status coalescing enforces a five-second minimum interval between complete
status publications and sends nothing while the normalized projection is
unchanged. Critical notifications and correlated command results use separate
bounded messages, but they are evidence only. MQTT keepalive/Blynk presence is
not an independent safety monitor.

The remote command path must not restore control datastreams on reconnect. In
particular, Start and OTA installation require a new live user action after
every disconnect. Token loss, broker failure, quota exhaustion, notification
failure, and stale cloud visualization fail remote access, not local control.
Target validation with simulated inputs is not evidence that remote heating of
the final appliance is electrically or thermally safe.
