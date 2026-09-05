# Data Model — V0

Status: **Design baseline**

Exact C++ syntax may evolve during implementation. Semantics must remain consistent with approved business rules.

## General rules

- Celsius is the canonical internal temperature unit.
- UI may convert to other units.
- Use optional/absent values instead of fake sentinel temperatures.
- Timer durations use monotonic time.
- Runtime state and persisted configuration are separate concepts.
- Configuration objects and timer runtime state must not be conflated.
- Avoid repeated heap allocation inside the critical control cycle.
- Use distinct `Duration` and `MonotonicTimePoint` types.

M5 constructs trusted, bounded simulation configuration during initialization.
No universal numeric limits for probe count, text/recipe size, or timer range are
asserted yet: M9 determines hardware-specific probe capacity and M10 defines
validation limits for persisted/untrusted configuration before runtime
allocation. `smoker_core` must not hard-code the currently available capacity of
six as a product invariant.

## IDs

Initial local IDs may use lightweight integer types, for example:

```cpp
using RecipeId = uint32_t;
using SessionId = uint32_t;
using ProbeId = uint8_t;
using AlarmId = uint32_t;
```

Do not introduce a UUID dependency until a real requirement exists.

## Temperature

Conceptual model:

```cpp
struct Temperature {
    float celsius;
};
```

Implementation should protect invariants where practical.

## ChamberState

```cpp
struct ChamberState {
    std::optional<Temperature> currentTemperature;
    std::optional<Temperature> targetTemperature;
};
```

`targetTemperature == null` means monitoring only; heater demand is OFF.

## FoodProbe

Required semantics:

```cpp
enum class ProbeRole {
    Meat,
    AmbientMonitor,
    Unassigned
};

struct FoodProbe {
    ProbeId id;
    std::string name;
    ProbeRole role;

    std::optional<Temperature> currentTemperature;
    std::optional<Temperature> targetTemperature;

    bool enabled;
    bool alarmEnabled;
};
```

`currentTemperature == null` may represent:

- no reading yet;
- disconnected probe;
- invalid reading.

Food-probe data never becomes chamber-control input.

M5 separates probe data into:

- immutable device/default configuration (`id`, `name`, `role`, default target,
  enabled, and alarm-enabled values);
- mutable active-session settings copied from scalar defaults on Start;
- mutable readings/connectivity/alarm latch state.

Live probe commands update only active-session settings. Persisting defaults and
recovery state remains M10 work.

## Monotonic time

Conceptually:

```cpp
using Duration = std::chrono::milliseconds;
using MonotonicTimePoint = std::chrono::time_point<MonotonicClock, Duration>;
```

The clock is a domain tag/injected port, not a dependency on an ESP-IDF clock.
Time points and durations are not interchangeable.

## Timer configuration

```cpp
enum class TimerStartConditionType {
    Immediate,
    ChamberTemperatureAtLeast,
    ProbeTemperatureAtLeast
};

struct TimerStartCondition {
    TimerStartConditionType type;
    std::optional<Temperature> temperature;
    std::optional<ProbeId> probeId;
};

enum class TimerCompletionAction {
    Notify,
    StopSession
};

struct StageTimer {
    Duration duration;
    TimerStartCondition startCondition;
    TimerCompletionAction completionAction;
};
```

The concrete `Duration` type may use `std::chrono`.

For `ProbeTemperatureAtLeast`, a disabled probe or a disconnected/invalid probe
has no usable reading, so an unstarted timer remains waiting. After re-enable or
reconnection, a later valid sample at or above the threshold may start it. Once
started, subsequent probe availability does not pause or reset the timer.

## Timer runtime state

Timer configuration and timer execution state are separate.

```cpp
struct TimerRuntimeState {
    bool started;
    bool completed;
    std::optional<MonotonicTimePoint> startedAt;
    Duration elapsed;
};
```

A power-recovery implementation must persist sufficient timer state to restore the approved behavior.

## Stage

V0:

```cpp
struct Stage {
    StageId id;
    std::string name;
    std::optional<Temperature> chamberTarget;
    std::optional<StageTimer> timer;
};
```

Do not add fan, smoke intensity, humidity, branches, loops, or multi-stage transitions in V0.

## Recipe

V0:

```cpp
struct Recipe {
    RecipeId id;
    std::string name;
    Stage stage;
};
```

Long-term direction is multiple stages, but V0 intentionally uses one stage.

Do not implement a collection of stages until the roadmap explicitly reaches that feature.

## Session

```cpp
enum class SessionStatus {
    Idle,
    Running,
    Stopped,
    Fault
};

enum class StopReason {
    None,
    User,
    TimerCompleted,
    Fault,
    RecoveryNotAllowed
};

struct Session {
    SessionId id;
    SessionStatus status;

    Recipe recipeSnapshot;

    MonotonicTimePoint startedAt;
    std::optional<MonotonicTimePoint> stoppedAt;

    std::optional<Temperature> activeChamberTarget;

    TimerRuntimeState timer;
    StopReason stopReason;
};
```

M5 stores monotonic session points because no wall clock exists yet. A future
display/audit timestamp is additional data and must never replace monotonic
duration calculation.
Once a fault ends the session, its `stoppedAt` remains the original fault stop
point when the resolved fault is acknowledged and the status becomes `STOPPED`.

## HeaterDemand

Domain-level heater output is normalized.

Conceptual invariant:

`0 <= percent <= 100`

Example API:

```cpp
class HeaterDemand {
public:
    static HeaterDemand off();
    static HeaterDemand fromPercent(float percent);

    float percent() const;
};
```

No SSR/GPIO/time-window semantics belong here.

## Fault

Initial fault family:

```cpp
enum class FaultCode {
    None,
    ChamberSensorInvalid,
    ChamberOverTemperature,
    ControlLoopFailure,
    ConfigurationInvalid
};

struct Fault {
    FaultCode code;
    MonotonicTimePoint occurredAt;
    bool latched;
};
```

Future fault codes are added only when real capabilities exist.

## Alarm

Initial alarm family:

```cpp
enum class AlarmCode {
    ProbeTargetReached,
    ProbeDisconnected,
    TimerCompleted
};

struct Alarm {
    AlarmId id;
    AlarmCode code;
    std::optional<ProbeId> probeId;
    MonotonicTimePoint occurredAt;
    bool acknowledged;
    bool resolved;
};
```

Alarm semantics must never implicitly change heater control.

Acknowledgement records user attention. Resolution records that the associated
condition/session lifecycle is no longer active. `activeAlarms` exposes alarms
where `resolved == false`; acknowledged active alarms remain visible.

## Event

Initial event examples:

```text
SessionStarted
SessionStopped
ChamberTargetChanged
ProbeTargetChanged
ProbeTargetReached
ProbeDisconnected
ProbeReconnected
TimerStarted
TimerCompleted
FaultRaised
FaultAcknowledged
DeviceBooted
PowerRecovery
CommandQueueOverflow
```

A single lightweight event model is preferred over a large inheritance tree.

## RuntimeState

Conceptually owned only by `SmokerApplication`:

```cpp
struct RuntimeState {
    std::optional<Session> session;

    ChamberState chamber;
    ProbeCollection probes;

    HeaterDemand heaterDemand;

    std::optional<Fault> activeFault;
    AlarmCollection activeAlarms;
};
```

The concrete collection type is an implementation choice.

Do not allocate/grow collections repeatedly inside the critical control loop.

## Commands

V0 command concepts, added only as milestones require them:

```text
StartSession
StopSession
SetChamberTarget
SetProbeTarget
SetProbeEnabled
SetProbeAlarmEnabled
AcknowledgeAlarm
Acknowledge/ClearResolvedFault
PrepareFirmwareUpdate (internal)
FinishFirmwareUpdate (internal)
```

Commands are the only external mutation path into `SmokerApplication`.

They are submitted to `SmokerApplication` only by the owning `ControlTask`; the
application API is not thread-safe. At M12 the HTTP task first uses a separate
SPSC transport mailbox, which `ControlTask` drains before `tick()`. Both fixed
queues reserve Stop admission. The application-owned queue may coalesce only a
consecutive trailing Stop; the cross-core SPSC transport never coalesces and
admits each Stop as a distinct FIFO entry. Application and transport overflow
counters are separately observable.

The M5 Boolean submission result is an admission result, not a semantic command
result. `true` means queued or coalesced; validation still occurs during
`tick()`, where invalid commands publish `CommandRejected`. `false` means the
bounded queue refused admission. For M12 HTTP commands, a nonzero 32-bit
correlation ID accompanies the command through both queues. The application
records a bounded `CommandResultView {correlationId, semanticAccepted}` for
processed, coalesced, or application-overflow outcomes.
Correlation IDs coalesced into one trailing application Stop remain in bounded
queue storage and receive that Stop's processed semantic result; they are not
marked semantically accepted during admission.

## Snapshot

A read-only view for UI/network consumers may contain:

```text
sessionStatus
sessionElapsed
chamberTemperature
chamberTarget
heaterDemand
timer
probe snapshots
active alarms
active fault
command queue overflow count
bounded correlated command results
firmwareUpdateActive
```

Snapshot types must not give callers mutable access to runtime/domain state.

M12 retains the allocating, by-value `SmokerSnapshot` for compatibility and
adds an allocation-free synchronous `SmokerSnapshotView`. Its spans refer to
application-owned probe/alarm caches and must not cross tasks directly.
`ControlTask` copies the view into a preallocated triple exchange whose readers
receive move-only atomic leases. The exchange also owns fixed storage for the
bounded command-result history.

`firmwareUpdateActive` is application-owned and does not extend
`SessionStatus`. Prepare sets it only outside `RUNNING`; Start is rejected while
it is true; Finish clears it. The platform OTA status is separately bounded:
current/available versions, 0..100 progress, installation permission, a
128-byte error, and exactly `IDLE`, `CHECKING`, `UP_TO_DATE`, `AVAILABLE`,
`WAITING_PERMISSION`, `INSTALLING`, `REBOOTING`, `VALIDATING`, or `FAILED`.

M14 adds monotonic `sessionElapsed` to both snapshot forms. The application
computes it from session start and stop points; UTC is not part of application
runtime state.

## M15 Blynk remote projection

M15 does not add cloud-owned domain/runtime state. `BlynkRemoteStatus` is a
bounded platform projection derived by value from an immutable application
snapshot plus the existing bounded platform firmware status. It contains only
the user-visible fields configured as Blynk datastreams, including:

```text
session status and elapsed display value
chamber current/target temperature
normalized heater demand
timer state/display value
bounded probe readings/settings
active-alarm summary
optional fault code
current/available firmware version
OTA state/progress/error summary
```

For the Blynk projection only, the available-version display contains the
available release tag in `AVAILABLE`, `Latest` in `UP_TO_DATE`, and is empty in
other OTA states. The separate firmware-state field remains unchanged.

The immutable application snapshot includes `timer_configured` so the platform
can distinguish no timer (`NONE`) from a configured timer which has not started
(`WAITING`). This presence bit is application-owned state, not cloud state.

Values are normalized to their Blynk display precision before equality is
evaluated. The adapter retains exactly a `lastPublished` projection and a
newest `pending` projection. A difference marks `pending` dirty; subsequent
differences replace it. The dirty projection may publish only after the
five-second minimum interval, then becomes `lastPublished`. If equality holds,
time passage alone creates no new status message. Connect/reconnect explicitly
publishes the newest current projection once.

Correlated command results and critical events are separate bounded message
types. `LastCommandResult` is not a `BlynkRemoteStatus` field and is never
serialized into `batch_ds`; only IDs currently tracked in the bounded Blynk
pending list can be projected from shared application results. Disconnect
drops unpublished feedback and events. They may be emitted immediately and are
not fields whose delivery changes `lastPublished`. MQTT/Blynk delivery state is
likewise not application runtime state.

Blynk control datastreams represent live user gestures, not desired-state
ownership. They map only to the existing external command set or to the
existing M13 firmware check/install request. Remote Start carries its intent and
optional chamber target in one bounded message which constructs one
`StartSessionCommand`; the exact Push-button release value `0` is ignored before
application admission/correlation and no cross-message target is retained.
Start and OTA-install controls are never read back or synchronized after
reconnect. A firmware request contains an operation and correlation identity only; it
contains no URL, image, signing key, or heater command.

## Persistence groups

### M15 Blynk device configuration

The dedicated `fumuri_blynk` NVS namespace stores one fixed-size, versioned,
CRC-protected blob containing:

```text
regional_endpoint
template_id
device_token
```

The blob is accepted only when its exact version, lengths, CRC, regional
`*.blynk.cloud` endpoint, template identifier, and printable non-empty token
validate. Missing/corrupt data disables only the Blynk adapter. The token is
unencrypted at rest in M15 and is never copied into application snapshots or
HTTP/UI data.

### M12 connectivity and authentication state

Wi-Fi configuration and the device credential are separate persistence
concerns. The network update schema contains exactly:

```text
ssid
wifi_password (8..63 characters, WPA2/WPA3 Personal)
```

It never contains `device_password`. The device password is changed only by an
authenticated current/new-password operation. Its initial value remains
`smoker257500` until optionally replaced.

The concrete M12 NVS representation keeps those concerns separate while making
each update atomic: one fixed, versioned blob contains `ssid` plus
`wifi_password`, and another contains the device password plus its initial/
claimed marker. Boot migrates the legacy individual keys into these blobs.
Invalid or unreadable blobs are not silently erased or combined with legacy
fields.
Legacy authentication read errors, corrupt values, and claimed-without-password
state also fail migration. Only a genuinely missing/unclaimed legacy state may
initialize the approved fixed product password.

The HTTP session is ephemeral, not persisted: one 256-bit token, a monotonic
idle-expiration point, and an active flag. A new login replaces it; logout,
password replacement, expiry, and reboot invalidate it. Browsers receive only
the `HttpOnly` cookie, and no token belongs in snapshots or JavaScript storage.

### Persisted device configuration

Examples:

```text
saved recipes
probe names/defaults
device safety maximum temperature
resumeAfterPowerFailure
```

### Persisted active-session recovery state

Persist only enough to restore approved session behavior:

```text
session id/status
recipe snapshot
active chamber target
timer state
started timestamp
probe session configuration
```

### M14 durable session history

History is separate from both device configuration and M10 recovery state. A
history record is an immutable platform projection with:

```text
kind: START | SAMPLE | CHANGE | END
historyId: uint64 durable local identity
sequence: uint32 within one stored session
applicationSessionId
session status and stop reason
monotonic session elapsed
optional Unix UTC seconds
chamber current/target temperature
normalized heater demand
timer state
probe readings and active-session settings
active alarms
optional fault code
```

`historyId` is serialized to JSON as a decimal string so browser number
precision cannot alter it. It is distinct from `SessionId`, which is application
runtime identity and may restart after reboot.

The list projection is `HistorySessionSummary`: start/end UTC when present,
elapsed time, periodic-sample count, final status/stop reason, and explicit
`active`, `interrupted`, and `truncated` flags. `HistoryHealth` exposes exactly
`READY`, `DEGRADED`, or `FAILED` plus capacity/usage, mailbox-drop, corrupt-
record, and write-error counters.

Control publishes only while a session is active: immediate START/END and
semantic-change records, plus periodic samples every 60 seconds while RUNNING.
Repeated high-frequency periodic flash telemetry is forbidden. Storage is a
bounded circular history, not a fixed-retention promise, and it cannot resume a
session or change application state.
