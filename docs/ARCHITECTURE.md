# Architecture — V0

Status: **Approved baseline**

## Principles

1. Domain/control logic is separated from ESP-IDF and hardware.
2. Mutable runtime state has one writer.
3. Critical heater control is deterministic and local.
4. Safety is evaluated synchronously before the heater command.
5. Network/UI/storage/OTA are non-critical services.
6. V0 intentionally avoids unnecessary concurrency and future scaffolding.

## Layers

### `smoker_core`

Pure platform-independent C++.

Contains domain values and algorithms such as:

- `Temperature`
- `Session`
- `Recipe`
- `Stage`
- `StageTimer`
- `FoodProbe`
- `HeaterDemand`
- `SafetyLimits`
- event/alarm/fault domain types
- session/control/safety logic

Must not depend on:

- ESP-IDF;
- FreeRTOS;
- GPIO;
- SPI/I2C/UART;
- NVS;
- Wi-Fi/BLE;
- OTA;
- concrete sensor/heater drivers.

Core logic receives values/time and returns decisions.

### `smoker_app`

Application orchestration.

Responsibilities:

- own mutable runtime state;
- process commands;
- call domain/control logic;
- read sensors through ports;
- write final heater demand through a port;
- publish immutable snapshots/events;
- coordinate persistence/recovery;
- coordinate OTA permission at the application level.

Application ports belong here, for example when required:

- `IChamberSensor`
- `IFoodProbeSource`
- `IHeaterOutput`
- `IClock`
- `ISessionStore`
- `IEventSink`
- `IFirmwareUpdater`

Do not create ports before a milestone needs them.

### `smoker_platform`

ESP-IDF/hardware-specific implementations.

Current M15, active M7, and inactive M8/M9 software implementations:

- real MAX31865 chamber source plus simulated food-probe sources;
- simulated heater output;
- ESP-IDF ordinary mixed-I/O runtime owning the single FreeRTOS `ControlTask`,
  task watchdog subscription, and bounded event sink.
- dedicated-NVS Wi-Fi configuration, STA/SoftAP fallback, mDNS, an ESP-IDF HTTP
  server, asynchronous 2.4 GHz scanning, captive-portal DNS/DHCP discovery, and
  the embedded Fumuri local UI;
- an SPSC command mailbox and preallocated triple snapshot exchange separating
  HTTP from mutable application state;
- one static low-priority core-0 `OtaTask` for SNTP, HTTPS OTA, image
  verification, boot selection, and rollback, with application permission
  crossing only bounded command/snapshot transports.
- a 16-entry SPSC history-observation mailbox and one static low-priority
  core-0 `HistoryTask` owning a versioned raw-flash circular log and read-only
  history queries. Storage failure is observable but never enters control or
  safety state.
- a platform-only Blynk Device MQTT/TLS adapter, two bounded command mailboxes,
  shared atomic external IDs, UART0/NVS credential provisioning, and one static
  low-priority core-0 `BlynkTask`. Remote loss cannot enter control or safety.
- an active MAX31865 chamber-sensor adapter behind `IChamberSensor`, with a
  host-testable result/configuration policy, target-only SPI-bus ownership,
  exact configuration access, and a RAII backend over pinned driver 1.0.8.
- an inactive dual-ADS1115 food-probe adapter behind `IFoodProbeSource`, with
  one host-testable staged acquisition owner, timestamped per-probe caches,
  mandatory injected calibration/validity, and a target-only RAII backend over
  the pinned 1.1.14 API. Production composition remains simulated.
- an application-owned `IChamberController` boundary, deterministic adapter
  retaining production's M2 100/0 behavior, and an inactive host-testable PID
  adapter with a target-only RAII backend over exact-pinned `pid_ctrl` 0.3.1
  float APIs. Production does not compose the PID adapter.

Future examples:

- SSR heater output;
- NVS session/config store;
- reset-reason adapter;

## Dependency direction

```text
                 smoker_core
                     ▲
                     │
                 smoker_app
                  ▲     ▲
                  │     │
                main  smoker_platform
```

`smoker_platform` implements ports owned by `smoker_app`.

`main` is the composition root.

## `app_main`

`main/app_main.cpp` must remain thin.

Its job is to:

- initialize required platform services;
- construct concrete adapters;
- construct the application;
- start required tasks/services.

Business rules do not belong in `app_main.cpp`.

At M15 plus M7 activation, `app_main` composes the built-in ordinary mixed-I/O
configuration and delegates task/runtime mechanics to `smoker_platform`.

### Active M7 MAX31865 boundary

The ordinary runtime's sole authoritative chamber source is now
`Max31865ChamberSensor`. Its target-only composition owns the SPI2 bus, an
`EspMonotonicClock`, the pinned-driver descriptor/backend, and the chamber
adapter in that construction order; destruction performs checked exact
converter shutdown and descriptor removal before freeing the bus. Food probes
and heater output remain simulated, control remains the deterministic M2 100/0
adapter, and the real PID adapter remains inactive. No SSR or heater GPIO path
is composed.

The active implementation is `ordinary_runtime.hpp`/`ordinary_runtime.cpp`.
The centralized ordinary configuration is SPI2, GPIO12 SCK, GPIO11 MOSI,
GPIO13 MISO, GPIO10 CS, mode 1 through driver 1.0.8, 100 kHz, PT100 nominal
100 ohm, three-wire, 50 Hz, ITS-90, active `0xD1`, terminal `0x11`, and
provisional Rref 430.0 ohm. It also explicitly owns a checked GPIO13 internal
pull-up and applies the supplier-documented inclusive -50..+200 C assembled
probe range as sensor-specific operational validity. The pin assignment,
PT100/three-wire facts, and range are
maintainer/probe documentation. SPI response, exact active/terminal readback,
and stable raw codes are connected T-pass evidence. The Rref and ITS-90 values
are operational choices corroborated by conversion math; 430.0 ohm is not a
measurement of the fitted reference resistor or its tolerance.

Startup initializes the bus, establishes the checked MISO pull-up, then permits
descriptor creation and writes and reads back
the complete exact `0xD1` byte, then waits at least the documented 66 ms first
50 Hz conversion boundary using the real monotonic clock. Only after that
boundary does ownership transfer to the sole `ControlTask`. The task calls
`SmokerApplication::tick()` at its existing cadence; safety evaluation remains
synchronous before the only, still-simulated heater write.

SPI-bus/pull setup, descriptor/configuration, or first-boundary failure leaves
the chamber adapter permanently unavailable until reboot but does not suppress
the ordinary runtime. `ControlTask` and observation/connectivity services still
start; the first tick publishes no chamber value, latches
`ChamberSensorInvalid`, reports `FAULT`, and retains exact OFF demand. Critical
runtime-context allocation or `ControlTask` creation failure remains an
immediate pending-image rollback path. A sensor-faulting pending image instead
reaches the normal validator, which cannot count a safe cycle and rolls back on
the published fault without weakening the five-cycle contract.

The backend deliberately avoids driver 1.0.8's configuration and fault-clear
read-modify-write helpers. Initialization and recovery use exact raw register
writes, exact active readback, and freshness invalidation. A faulting, failed,
non-finite, or out-of-range current sample is absent; the application latches
`ChamberSensorInvalid` and
the heater command is OFF. Recovery can only arm a fresh future conversion and
cannot reuse a cached temperature or automatically resume a latched session.
Shutdown writes and verifies exact terminal `0x11`, then removes the descriptor;
early failure and startup-failure paths preserve the same descriptor-before-bus
release ordering. After successful bus release, the owned GPIO13 pull is
restored to floating. A disconnected/high-impedance MISO therefore resolves
toward `0xff`, while stuck-low/raw-zero paths are rejected by exact configuration
verification and the temperature-validity policy.

Project-owned steady-state read code contains no explicit delay, task creation,
heap allocation, or `max31865_measure()` call. Host/source/build evidence does
not prove allocation or worst-case latency inside ESP-IDF/driver/SPI internals.
Connected diagnostic success also does not establish calibrated accuracy,
noise, response time, longer-duration ordinary-runtime behavior, controlled
open/short fault handling, module identity, fitted Rref, or physical quiescence
beyond the observed readbacks.

M7's defined connected ordinary-runtime activation is complete. On 2026-08-25
the signed 1,445,888-byte serial target, SHA-256
`4b1541202eaa7388b79c48f9f615fd7443959b5ad943f12652e3cfa4ea95ffcc`,
ran this exact composition through cycles 1, 60, and 180. Chamber readings were
25.7, 25.7, and 25.8 C over approximately 179 seconds while IDLE with no target
and simulated heater 0.0%. The three readings satisfy the intended at-least-
120-second functional observation; exact cycle 120 was not required. No
MAX31865, SPI, configuration, chamber-sensor, watchdog, rollback, unexpected-
reset, or diagnostic failure appeared. This short observation is not
calibration, controlled-fault, longer-duration, response/noise, heater-
interference, physical temperature-regulation, or hardware-safety evidence.
The heater/SSR path and production PID remained inactive.

That serial helper installed all-`0xff` initial OTA metadata in the no-factory
layout. ESP-IDF 6.0.2 consequently selected `ota_0` directly as
`ESP_OTA_IMG_VALID`; it did not create `ESP_OTA_IMG_PENDING_VERIFY`. The
`PENDING_VERIFY`/five-cycle criterion is therefore inapplicable and waived for
this serial activation only. OTA-005 remains unchanged for an actual OTA-
installed pending image: five consecutive safe cycles are still mandatory.
A future sensor-faulting pending-image target test remains separately pending
but is not an M7 completion criterion.

An additional target-only connected-board diagnostic is compiled only when
`CONFIG_SMOKER_MAX31865_CONNECTED_DIAGNOSTIC` is explicitly enabled; Kconfig
defaults it OFF. Its `app_main` branch is compile-time exclusive from the
ordinary runtime and never constructs `SmokerApplication`, `ControlTask`, or a
heater output. It first exercises a bounded mode-1 software-SPI register path
with datasheet timing margins, then cross-checks the pinned 1.0.8 driver. Both
paths reject MISO data that changes with internal pull-up/pull-down and use only
defined persistent configuration bits for complementary readback. The
diagnostic reports a finite set of raw RTD codes, `RRTD/RREF` ratios, and fault
bits, never temperature. Before either SPI implementation releases CS, bus, or
GPIO ownership, it writes and reads back an exact terminal configuration with
AUTO, VBIAS, 1-shot, fault-cycle, and fault-clear commands all zero. The
terminal `0x11` retains three-wire/50 Hz selection while leaving the converter
normally off and unbiased.

Driver 1.0.8's configuration setter is a read-modify-write which clears only
the persistent D7/D6/D4/D0 fields and can preserve self-clearing D5, D3:D2, and
D1. Diagnostic shutdown therefore uses an exact raw configuration transaction,
not that setter. The normal driver path performs checked, idempotent quiescence;
both software-SPI pin ownership and driver descriptor ownership retain bounded
destructor fallbacks for early returns. The software fallback first forces
mode-1 idle and a CS-high frame boundary so a shutdown write cannot append to
a partially failed transfer. Descriptor removal is attempted even after a
shutdown failure and occurs before `spi_bus_free()`. A normal-path
shutdown write/readback failure makes the diagnostic fail. Source/build
evidence proves this intended cleanup sequence only. The first connected run
on 2026-08-24 failed when software-SPI MISO followed its pulls. A later
corrected connected setup produced pull-independent `0x11`, exact software
patterns `0x00`/`0x91`/`0xD1`, ten stable raw samples with no transaction or
sensor fault, and exact software and driver terminal `0x11` readbacks. The
earlier failure remains part of the chronology; neither run is calibration or
controlled fault-injection evidence.

### Inactive M8 PID boundary

The first M8 slice adds `IChamberController` to `smoker_app`. It accepts the
authoritative chamber `Temperature` and active target and synchronously returns
either a typed normalized `HeaterDemand` request or explicit failure. Its
`reset()` operation clears/disables latent controller state and reports failure.
The port contains no ESP-IDF or component type and is injected explicitly into
every `SmokerApplication` construction site.

Production constructs `DeterministicChamberController`, a thin adapter around
the existing core 100% below target / 0% at-or-above-target calculation. The
real `PidChamberController` is compiled and host-tested but is not constructed
by the ordinary runtime. `Max31865ChamberSensor`,
`SimulatedFoodProbeSource`, and `SimulatedHeaterOutput` form the production
composition; there is no SSR output, heater GPIO assignment, or switching
window.

The official registry dependency is `espressif/pid_ctrl` exactly 0.3.1 at
component hash
`974be0666bb4d95f49677327dd8305781d04d8bae284fdde2fbadf06ca9d4979`.
Its mandatory `espressif/iqmath` 1.11.0~1 dependency is locked at
`39448db759b410373e543798167ca4670bbff3019cb290a2fe8e627221e71b9d`.
The reviewed component offers float and IQmath numeric backends and explicit
positional/incremental forms. It has no autotuning, plant-identification,
sample-period, or delta-time input.

The inactive target backend selects only the exact float APIs
`pid_new_control_block_f()`, `pid_compute_f()`,
`pid_reset_ctrl_block_f()`, and `pid_del_control_block_f()`. This matches the
project's float `Temperature`/`HeaterDemand` boundary and ESP-IDF 6.0.2's
ESP32-S3 `SOC_CPU_HAS_FPU`/single-precision DFPU evidence. The form remains
mandatory configuration rather than a production selection. Common gains and
normalized output bounds are explicit and finite. Positional form additionally
requires finite ordered accumulated-error bounds containing zero. Incremental
form rejects those project-visible bounds because upstream does not use them;
the target adapter supplies deterministic `0/0` only to the ignored upstream
struct fields. Test coefficients are fixtures, not tuning recommendations.

In positional form, reviewed 0.3.1 accumulates raw error once per call, clamps
that accumulated error before multiplying it by Ki, and differentiates error.
Incremental form does not read accumulated error or either positional bound;
it adds per-call error changes to retained output and clamps that output to the
common output limits. Both forms have implicit per-call gains because there is
no `dt`, both differentiate error rather than measurement, and neither provides
derivative filtering. With the project boundary fixed at target minus measured,
a target step can therefore produce derivative kick. These are activation and
tuning considerations, not approval of either form for production.

Upstream float creation allocates the control block with `calloc()`, so backend
creation is initialization work before `ControlTask` starts. Reviewed valid
compute/reset paths reuse that block and intentionally perform no allocation.
Project-owned request/reset paths add no allocation, task, I/O, logging, delay,
wait, or lock. Host allocation observation and source guardrails cover those
specific paths; they are not real target timing or thermal evidence.

The application computes a requested demand only while configuration is valid,
the session is `RUNNING`, a chamber value and target exist, and no fault/update
interlock is active. Construction issues the observable heater-OFF write before
its first controller reset callback. This application ordering does not replace
safe initialization inside a future real heater-output driver. Stop, an
effective target removal, invalid measurement, safety fault, firmware-update
interlock, and every other transition out of eligibility reset the controller
before the OFF write. Reported application-owned resets carry failure into
`ControlLoopFailure`; `PidChamberController` does not perform an ignored reset
from its destructor, while the target backend still releases its owned control
block through RAII. A later successful reset can only resolve the condition for
explicit clear. Clear leaves the session `STOPPED`, so a new Start is still
required. The cycle order remains controller request, synchronous safety
evaluation, safety gate, and the sole final heater write.

Non-Stop commands are evaluated as one control-cycle batch. Accepted target
removal followed by restoration in the same batch leaves an eligible final
state and does not create an intermediate output/reset boundary. RR-003 applies
when the target is absent at control evaluation; only accepted manual Stop has
the explicit OFF-cycle barrier required by SR-003 and D031.

`pid_ctrl` is a runtime PID engine, not an autotuner. Automatic tuning is a
separate future design decision and cannot be selected or safely tested before
the real chamber sensor, SSR/heater, smoker thermal plant, and independent
cutoff are available and validated. No simulated result is a production tuning
recommendation. Because the API has no `dt`, binding the call cadence and gains
to a measured, validated real control period remains an M8 activation gate.

### Inactive M9 dual-ADS1115 boundary

M9 software integration also leaves production composition unchanged. One
`Ads1115FoodProbeSource` owns the complete channel round robin and per-channel
cache, plus explicit `Unsynchronized`, `Idle`, or `Converting` state for each
physical ADC. Both devices begin unsynchronized after backend initialization:
pinned `ads111x_set_mode()` is a read-modify-write configuration transaction,
not proof that a conversion started before an MCU-only reset has completed.

Before first use and after quarantine, a service step calls the pinned busy API
once. An error or `busy=true` performs no configure/start/value operation and
leaves that ADC unsynchronized while the round robin advances so the other ADC
can progress. A successful `busy=false` synchronizes the device and discards
any pre-existing result, but recovery never starts a new conversion in that
same service step. Only a later step explicitly configures mux, gain, and data
rate, starts one single-shot conversion, calculates its monotonic deadline, and
returns.

For an active conversion, OS/busy is observed before the deadline is evaluated.
`busy=false` proves the converter idle and makes the latched result eligible for
read/calibration even when polled exactly at or after the deadline, because the
poll time does not reveal the completion time. `busy=true` before the deadline
retains the conversion. `busy=true` at or after it discards the affected sample
and quarantines that ADC until a later successful idle observation. A busy-read
error and every failed start have the same quarantine boundary because either
can leave conversion state unknown; no abandoned result is read or attributed
to another channel. Consecutive logical channels on that ADC are skipped while
unknown rather than blocking the healthy device.

Once `busy=false` has succeeded, a value-read failure invalidates only the
active sample because the ADC is known idle. Calibration/validity failure is
also probe-local. In pinned driver 1.1.14, mux/gain/rate setters use
`write_conf_bits()`, whose non-OS writes explicitly clear OS in the value sent
to the device. Therefore a configuration failure while a previously
synchronized converter is idle may leave partial configuration, but cannot
start a conversion; the attempted probe is invalidated and safe round-robin
scheduling continues without quarantine. The sequencer never sleeps, polls in
a loop, creates a task, or lets a previously latched result cross channels.

`IFoodProbeSource::read(probe_id)` performs no I2C work. It returns only a
timestamped cached `Temperature` while the configured maximum age has not
expired. Before the first successful conversion, after expiry, or after a
configure/start/busy/read/calibration/validity failure, that probe is absent;
other probe caches remain independent. Raw ADS1115 codes are passed to a
mandatory injected calibration/validity policy. The adapter supplies no probe
curve, divider, voltage, temperature, calibration, or channel-purpose default.

Every device record explicitly requires I2C port, SDA/SCL GPIO, clock, pull-up
policy, and address; every channel requires probe/device mapping, mux, gain, and
data rate. Conversion timeout and sample maximum age are also explicit. Two
devices on one port must use the same pins/clock/pull-up policy and distinct
addresses. Devices on separate non-overlapping buses may reuse an address.

The target-only backend owns exactly two `i2c_dev_t` descriptors and uses the
real 1.1.14 init/free, mode, mux, gain, rate, start, busy, and value APIs.
`ads111x_init_desc()` writes a driver-owned 1 MHz clock and creates a mutex; the
backend replaces that clock and applies the required pull-up policy before
`ads111x_set_mode()` performs the first I2C transaction. Production does not
call `i2cdev_init()` or construct this backend.

Connected evidence does not change that composition. One ADS1115 is currently
installed at 3.3 V on GPIO17 SDA/GPIO18 SCL, ADDR tied to GND (`0x48`),
ALERT/RDY disconnected, and 100 kHz; the second selected module is deferred.
Four networks were reported assembled as 3V3 through nominal 100 kOhm 0.1% to
each AIN node, with the intended NTC and 100 nF capacitor branches from that
node to GND, but only A3 was physically exercised with an NTC. A0-A2 have no
analog-response evidence.

The A3 runs used a temporary signed ESP-IDF 6.0.2 diagnostic built directly on
`i2c_master` APIs with internal SDA/SCL pull-ups enabled. They did not exercise
`Ads1115TargetBackend`, pinned `i2cdev`, the staged M9 sequencer, production
composition, ControlTask timing, or sustained acquisition. The first run began
from configuration word `0xF383` and stayed near zero (raw approximately -4
through -2), consistent with A3 being effectively grounded or missing its
high-side divider branch. After the wiring was corrected, a 20-sample room-
condition run began from `0x8583` and moved from raw 18329 to 18040, 2.2911 V
to 2.2550 V, and nominally calculated 227.10 kOhm to 215.79 kOhm. An
uncontrolled soldering-tool heating run began from `0x8583` and moved overall
from raw 8300 to 1174, 1.0375 V to 0.1468 V, and nominally calculated
45.86 kOhm to 4.65 kOhm. The latter had a small intermediate reversal and is
only a strong overall decrease consistent with NTC behavior.

Those resistance values use nominal 3.3 V and 100 kOhm values; neither the rail
nor the individual resistor was measured. The runs establish response only
through the connected A3 divider/jack/probe path, not temperature, R25, Beta,
curve identity, calibration, or accuracy. At diagnostic exit, `0x8583` was
written and immediately read as `0x0583`, consistent with OS/busy after a
one-shot start; terminal idle `0x8583` was not subsequently proved. That
procedure requires a later idle poll/readback before reuse. This limitation is
separate from the 2026-08-25 digital test which did verify terminal `0x8583`.
The last known board image after the A3 session was this temporary diagnostic,
not the ordinary firmware; repository production composition nevertheless
remains unchanged.

The TI Rev. E datasheet states conversion time is `1 / DR` and specifies
data-rate variation of +/-10%; the software validates the explicit stuck
deadline against that conversion-period floor. The approximately 25 us
single-shot power-up and scheduling/frontend margin have no specified maximum,
so the actual deadline remains mandatory configuration. Locked `i2cdev` may
wait up to `CONFIG_I2CDEV_TIMEOUT`, lazily create bus/device state, retry with
internal task delays, and allocate during initialization or recovery. Host
allocation observation and an API cross-build therefore do not prove real I2C
latency, allocation freedom, ControlTask suitability, or physical behavior.

## Runtime-state ownership

`SmokerApplication` is the single writer of mutable runtime state.

Forbidden pattern:

```text
Web/UI/OTA -> mutate Session directly
```

Required pattern:

```text
external actor
    -> Command
    -> SmokerApplication
    -> RuntimeState
```

External consumers receive immutable snapshots/events.

## Commands

Initial V0 command family may include, when the corresponding milestone is implemented:

- Start session
- Stop session
- Set active chamber target
- Set probe target
- Enable/disable probe
- Enable/disable probe alarm
- Acknowledge alarm
- Clear/acknowledge a resolved fault without automatically restarting heating
- Prepare/finish firmware update (internal orchestration only)

Do not create command types before they are needed.

### M5 command admission and ownership

`SmokerApplication::submit()` is owned and called by the single `ControlTask` in
M5. It is deliberately not a cross-task synchronization primitive. Future
UI/network tasks must use a platform/application transport introduced by their
own milestone rather than calling it concurrently.

Its Boolean result reports **admission only**. `true` means the command was put
in the queue, or that a consecutive trailing Stop was coalesced. It does not
mean the command has passed runtime/semantic validation. Semantic rejection is
reported as `CommandRejected` when the owning task processes the command.

The bounded queue reserves one admission slot for `StopSession`. A Stop is
coalesced and still reported as accepted only when the newest pending command is
already Stop. Any intervening command ends that coalescing run: a later Stop is
a distinct FIFO intent and uses the reserved admission slot. This prevents
`Stop -> Start -> Stop` from discarding the final Stop. Regular-command overflow:

- returns `false` to the caller;
- increments an observable cumulative snapshot counter;
- publishes `CommandQueueOverflow` during the next control cycle.

Callers must check the result of every non-Stop submission.

When correlated direct submissions coalesce into a trailing Stop, their bounded
correlation-ID list remains attached to that queued Stop. Every retained ID
receives the Stop's actual processed semantic result; coalescing never reports
semantic success before the original Stop has been validated.

An accepted manual Stop is also a control-cycle barrier. The application
finishes that tick with a safety-gated OFF write; commands queued after Stop
remain FIFO and are processed starting with the next tick. This prevents a
same-batch Start from erasing the observable safe-OFF cycle required by SR-003.

### M12 cross-task command transport

`SmokerApplication::submit()` remains single-owner and is called only by
`ControlTask`. The core-0 ESP HTTP task is the single producer of a 16-entry
SPSC mailbox; `ControlTask` on core 1 is its single consumer. Regular commands
can fill only 15 admissions, preserving one Stop admission under regular
saturation. The cross-core transport does not coalesce Stops: every admitted
Stop occupies its own FIFO entry, avoiding a producer/consumer race over whether
a previous Stop is still pending. A full transport returns `429`; the reserved
slot guarantees that regular commands alone cannot prevent the first Stop.

Before each `tick()`, `ControlTask` drains the transport only through the first
Stop, then lets the application's existing Stop barrier complete a
safety-gated OFF cycle. HTTP `202` means transport admission only; semantic
validation remains asynchronous in `SmokerApplication`. Each admitted HTTP
command carries a 32-bit correlation ID through the mailbox and bounded
application queue. A bounded result history in immutable snapshots reports the
semantic accept/reject decision, including application overflow and coalesced
Stop outcomes. Mailbox sequences are native 32-bit values; the ControlTask path
no longer uses ESP32-S3's global-lock-backed 64-bit atomic load/store helpers.
The fixed JSON admission body is completed before mailbox publication, so a
local cJSON allocation/serialization failure cannot return `503` after the
command has already been admitted. A transport send failure can still make the
client's observation ambiguous, as with any network command.

## Snapshot

A `SmokerSnapshot` is a read-only representation suitable for display/web/mobile consumers.

It may expose:

- session status;
- chamber current/target temperature;
- heater demand;
- timer status;
- probe snapshots;
- alarms;
- active fault.

UI code must not receive mutable domain/runtime objects.

`activeAlarms` contains only unresolved alarms. Acknowledged alarms remain
active until their lifecycle is resolved.

M12 adds `SmokerSnapshotView`, an allocation-free synchronous view backed by
application-owned pre-reserved caches. After each tick, `ControlTask` copies it
into a three-slot preallocated exchange. HTTP readers hold atomic read leases;
publication never waits, never serializes JSON, and drops an update if both
non-current buffers are leased. JSON and network work remain outside the
critical cycle.

M13 adds the application-owned Boolean `firmware_update_active` to both snapshot
forms. It is an interlock, not a new `SessionStatus`: while true, Start is
rejected. Installation permission cannot be obtained during `RUNNING`.

M14 adds `session_elapsed` to both snapshot forms. It is computed from monotonic
session points by `SmokerApplication`; history never derives duration from UTC.

## Probe configuration ownership

M5 keeps immutable device/default probe configuration separate from mutable
active-session settings. `SetProbeTarget`, `SetProbeEnabled`, and
`SetProbeAlarmEnabled` operate only on a `RUNNING` session. A new explicit Start
copies scalar defaults into fresh session settings without modifying saved
configuration or allocating in the critical cycle. Persisting defaults and
active-session recovery state remains M10 work.

A not-yet-started `ProbeTemperatureAtLeast` timer treats a disabled,
disconnected, or invalid selected probe as having no reading and continues to
wait. Re-enable/reconnection can satisfy the condition only on a later control
cycle that actually samples a valid reading at or above the threshold. Food
probe availability never becomes a heater-control fault.

M5 startup configuration is local, trusted, and allocated before the critical
task starts. It validates a non-empty probe collection and unique IDs, but does
not invent universal product limits. M9 must confirm device-specific probe
capacity; M10 must define and enforce persisted-input limits such as probe count,
text/recipe sizes, and timer ranges before allocating runtime state.

## V0 concurrency model

Use one critical `ControlTask`.

Do not split V0 into separate:

- sensor task;
- PID task;
- safety task;
- timer task;
- session task.

The inactive M8 slice retains this concurrency model. `SmokerApplication`
calls its injected controller synchronously in the owning `ControlTask`; the
production instance is still deterministic, while the compiled PID adapter is
not composed. Neither owns a task or bypasses the application. Requested
normalized demand proceeds through synchronous safety evaluation and the same
gate before the sole final heater write. Platform placement contains the
component's ESP-IDF types and keeps `smoker_core` host-portable.

A conceptual control cycle:

```text
1. Read authoritative chamber temperature and enabled food probes as raw values
2. Drain/process pending commands
3. Derive probe connectivity events and active-session alarms using new commands
4. Update session/timer state
5. Calculate requested heater demand or reset an ineligible controller
6. Validate measurements, evaluate safety, and latch controller failure
7. Apply safety override
8. Write final heater demand
9. Publish bounded events/alarms
10. Make the current snapshot available by value
```

Raw acquisition has no event/alarm side effects. This split means a probe/alarm
disable, Stop, or target change deterministically affects alarm derivation in
the same cycle. Exact internal ordering may be refined, but **the final heater
write must always occur after safety evaluation**.

All ports used directly by the critical cycle must have bounded, non-blocking
behavior appropriate to their adapter. `IEventSink::publish()` must enqueue or
store locally without network/storage I/O. A future network/storage consumer
must run outside the critical dependency chain.

The inactive M9 adapter is deliberately not placed in this cycle yet. Its
`service()`/cached-`read()` split proves software sequencing without pretending
the locked target I2C stack is sufficiently bounded. Activation and service
placement require connected timing evidence against the final ControlTask
budget; no separate sensor task is authorized.

At M14 the `ControlTask` retains its static 12 KiB stack and priority and is
pinned to core 1. Wi-Fi, TCP/IP, the default event loop, fallback timer, HTTP
server, and the only connectivity helper task—a static 4 KiB captive DNS
responder—run on core 0. The DNS task exists only while SoftAP is active and never submits
commands. Connectivity initialization failure is logged but does not prevent
local control from starting. M13 adds one static 16 KiB low-priority `OtaTask`
on core 0. It is not subscribed to TWDT and all SNTP, HTTPS, and flash work stays
outside `ControlTask`; only atomic bounded signals and immutable snapshots
cross that boundary.

M14 adds one static 12 KiB low-priority `HistoryTask` on core 0, also outside
TWDT. `ControlTask` publishes only a preallocated, 32-bit-sequenced SPSC
observation after the safety-gated tick and immutable snapshot publication. It
never reads UTC, locks a mutex, allocates, logs, queries flash, or waits for
history. A full mailbox drops an observation and increments an observable
counter.

M15 adds one static 12 KiB `BlynkTask` on core 0 at priority one, below
`ControlTask`, and leaves it outside TWDT. ESP-MQTT also runs on core 0. The
Blynk task observes immutable snapshots once per second and owns MQTT,
serialization, NVS provisioning, and remote event/result delivery. None of
those operations is on the heater-control dependency chain.

## M12 local HTTP and credential boundary

Every HTTP request is classified before Host validation, authentication, and
routing from the accepted socket's local IPv4 address. `192.168.4.1` is
`Commissioning`; the current STA IPv4 is `Operational` only while SoftAP is
inactive; every other combination is rejected fail-closed. ESP-IDF's lwIP can
accept on one netif a packet addressed to another local netif, so a STA-local
destination address alone is not ingress proof during APSTA overlap. This keeps
a valid or stolen cookie from unlocking operational routes through the open AP,
including while a failed STA-only transition is being retried.

The `Commissioning` surface is public but exposes only the embedded Wi-Fi setup
page, provisioning status, bounded scan, and credential save. It has no login,
dashboard, snapshot, cooking state, or command access. The open AP remains the
explicit D046 usability tradeoff, so clients in radio range can observe or
modify its plaintext Wi-Fi setup traffic; no smoker runtime data is served.

The `Operational` STA surface publishes a public password-only `/login` page
and `POST /api/v1/auth/session`. A successful device-password check replaces
the single random 256-bit `HttpOnly`, `SameSite=Lax`, `Path=/` cookie with a
30-minute server-side idle timeout. There are no users, roles, Basic, or Bearer
credentials. Dashboard/data routes require the cookie. Unauthenticated API
requests return JSON `401` without `WWW-Authenticate`; page navigation redirects
to `/login`. Logout invalidates the token and clears the cookie.

The fixed initial device password remains `smoker257500`. An authenticated,
separate `PUT /api/v1/auth/password` requires current and new passwords,
persists the new value, invalidates the session, and forces relogin. Network
configuration never accepts `device_password`. Login failures retain the fixed
per-IPv4-peer exponential limiter. The dual-stack ESP-IDF HTTP listener
normalizes IPv4-mapped peers before indexing that limiter. Secrets are never
returned. Wi-Fi credentials and device-authentication state are separate,
versioned NVS blobs, so each concern updates atomically; boot migrates the
legacy per-field M12 keys without erasing NVS. M12 has no TLS
or NVS encryption, so HTTP credentials/cookies and stored secrets are not
end-to-end/at-rest encrypted. NVS errors never trigger automatic erase.
Legacy authentication migration uses the initial password only for a genuinely
missing/unclaimed state. Read errors, corrupt values, and a claimed marker
without a valid password reject connectivity initialization instead of
persisting the public initial password over a claimed credential.

For configured STA credentials, the 30-second SoftAP fallback deadline is
anchored to the beginning of a disconnected period. Repeated authentication or
association failures reconnect STA without restarting that deadline. `GOT_IP`
cancels it; expiry while still disconnected enables APSTA so credentials can be
corrected without erasing NVS. A dedicated Wi-Fi-mode mutex serializes fallback
activation with `GOT_IP`, so the final state converges to STA-only once an IP is
available. Immediate `esp_wifi_connect()` failures use a bounded timer retry,
and an unrecoverable STA startup error exposes AP-only recovery instead of
leaving the device unreachable. A validated provisioning PUT marks a
configuration transition before NVS work and holds the Wi-Fi-mode boundary
through persistence and driver application. `GOT_IP` waits on that boundary
and validates the currently associated SSID against the active configuration;
a stale event cannot disable the recovery AP, while rollback can still accept
the prior valid association.

Only one STA connect attempt may be in flight. An accepted connect cancels any
older retry timer; a disconnect clears that admission and retries, while the
intentional disconnect used to install new credentials defers its one connect
to the resulting event (with a timer as a missing-event fallback).

Requests have a 512-byte maximum body and accept exact media types and strict
schemas without duplicate or unknown fields. Every state-changing authenticated
or provisioning request requires an explicit Origin equal to the accepted
device authority. Commissioning Host is restricted to the AP address;
operational Host is restricted to the current STA address or `.local` hostname,
so reflecting an attacker-selected rebound Host cannot satisfy the boundary.
All HTTP error envelopes use fixed bounded storage. In particular, a failed
cJSON allocation can return `503` without attempting another C++ heap allocation
on the exception-disabled target.
Embedded Romanian HTML/CSS/JS has no CDN or npm
runtime dependency and self-schedules one snapshot request at a time instead of
using overlapping intervals, WebSocket, or SSE. Probe rows are updated by key,
so live readings continue while their target input retains focus.
Defensive headers and a same-origin CSP cover assets and APIs.

The operational UI adopts only the visual tokens and language of the read-only
Fumuri prototype. It remains embedded vanilla HTML/CSS/JavaScript with system
fonts and implements live snapshots/safe commands, alarms/faults, device/network
status, logout, and password change. The commissioning UI is a separate,
data-free Wi-Fi-only page. Theme preference is the only browser-local value and
is stored per origin.

`POST /api/v1/network/scan` starts or coalesces a non-blocking scan and returns
admission `202`; `GET /api/v1/network/scan` exposes its state and sanitized
public results. Hidden networks are omitted, duplicate SSIDs retain their
strongest record, and at most 20 entries expose only SSID, RSSI, channel, and a
security category. Firmware-owned scan buffers are preallocated. STA reconnect
is suppressed during scan and resumed afterward. A 15-second one-shot timeout
stops a wedged driver scan and runs the same reconnect/provisioning recovery.
Only WPA2 and WPA3 Personal results are selectable; OPEN, WEP, WPA1, Enterprise,
and unknown results are unsupported. `PUT /api/v1/setup/network` and the
authenticated network update accept exactly `ssid` plus an 8..63-character
`wifi_password`; the driver minimum threshold is WPA2. STA disconnect reasons
are exposed as bounded status codes. SoftAP stops only after a STA IP is obtained. Restoring AP-only after a scan rechecks configuration inside
the serialized Wi-Fi-mode transition so it cannot overwrite a concurrent
provisioning request.

SoftAP DHCP advertises option 114 with `http://192.168.4.1/`. A bounded parser
adapted from the ESP-IDF 6.0.2 captive-portal example answers wildcard A queries
only on the SoftAP address. Unknown captive-probe HTTP paths receive a non-empty
redirect to the absolute `http://192.168.4.1/` setup URL so captive clients do
not retain probe hosts such as `captive.apple.com`. Authentication is never
offered through AP. DNS failure is a logged degradation to manual IP access. On AP stop,
the static DNS worker closes its socket, signals exit, and suspends; only the
core-0 owner deletes it and releases its static stack/control block before a
later AP start may reuse that storage.

`espressif/cjson` supplies JSON parsing/serialization and `espressif/mdns`
supplies discovery without a display. Registry versions and hashes are locked;
generated components stay ignored. M14 retains the preserved 24 KiB NVS
partition, `otadata`, `phy_init`, and two 3 MiB OTA slots, then assigns a 4 MiB
raw `history` partition. The remaining `0x5e0000` bytes of the confirmed 16 MiB
flash stay intentionally unallocated. Build
verification limits the application to 75% of either slot. Moving from M12's
single-app layout requires a complete serial flash. The serial helper rejects
stale generated configuration and unsigned or build-mismatched applications,
then substitutes the verified signed application into ESP-IDF's generated
flash map. It writes the generated bootloader, partition table, initial OTA
metadata, and application without erasing or writing the preserved NVS region.

## M13 firmware update boundary

The only release source is the fixed HTTPS GitHub Releases asset. A manual
check has a 30-second total monotonic deadline, including bounded wall-time
synchronization. It opens an ESP-IDF HTTP client stream with certificate-bundle
validation, disables automatic redirects, follows at most five redirects, and
requires HTTPS transport on every hop. It reads the ESP image prefix without
installing and validates image/descriptor magic, the real image chip ID,
project name, and canonical semantic version. Only a strictly newer ESP32-S3
image is offered. Installation repeats that validation through a fresh stream
with a five-minute total deadline, requires complete image receipt and ESP-IDF
image verification, including RSA-3072 publisher-signature verification at
`esp_ota_end()`, selects the inactive OTA slot, and reboots. Signed-app
verification without hardware Secure Boot derives the trusted public-key
digest from the currently running signed application. The first serial M13
image must therefore be signed with the same key as every OTA release.
Application permission itself expires after ten seconds so a missing
correlated response cannot leave Start interlocked indefinitely.

The canonical source/release repository is public so the controller can fetch
that fixed asset without a GitHub credential. No repository token or signing
secret is stored on the device. Public availability is only transport access;
publisher authenticity still comes exclusively from the RSA signature checked
by ESP-IDF.

Ordinary and pull-request target builds remain deliberately unsigned and never
receive private signing material. The tag-restricted release environment supplies
the base64-encoded private key only to the signing step, which writes it to a
permission-restricted temporary file, signs the prevalidated padded image,
verifies the result against the versioned public key, and deletes the temporary
copy. GitHub receives only the verified signed image and its SHA-256. Hardware
Secure Boot and flash encryption remain separate physical/flash-write threat
controls and are not enabled by M13.

Because ESP-IDF generates serial flash targets even for those unsigned builds,
the project attaches a fail-closed prerequisite to `flash`, `app-flash`,
`bootloader-flash`, `partition-table-flash`, and `otadata-flash`. The only M13
USB installation path is the explicit signed-image helper, which validates the
signature, build provenance, effective configuration, generated layout, and
complete write map before invoking `esptool`.

The release environment currently has no independent required reviewer because
the project has only one maintainer. D051 accepts that as a conditional P3
operational boundary only while that maintainer alone controls repository and
tag writes. Any additional human or automation with either capability reopens
the release-signing design before another release.

`PrepareFirmwareUpdateCommand` is accepted only in `IDLE`, `STOPPED`, or
`FAULT`; it owns `firmware_update_active` and blocks Start. Correlated immutable
command results tell `OtaTask` whether permission was granted. The platform
service publishes Prepare through a dedicated one-slot atomic signal rather
than the saturable HTTP command mailbox. `ControlTask` retries Prepare before
draining that mailbox and does not consume the atomic Finish signal until the
pending Prepare has entered the application FIFO. Thus a timeout cannot apply
Finish before its reservation and leave a delayed reservation latched. Availability
checks do not reserve application state and therefore remain allowed during
`RUNNING`.

On a `PENDING_VERIFY` boot, the application interlock is established before
validation. The image becomes valid only after five consecutive cycles report
`IDLE`, a valid authoritative chamber measurement, no fault, heater OFF, and a
successful TWDT reset. Fault, a ten-second timeout, or mark-valid failure calls
the rollback-and-reboot API. Network connectivity is not an input to this
decision. Failure to allocate the runtime context or create `ControlTask` also
checks the running image and rolls back immediately instead of returning from
`app_main` with a pending image. Failure to create `OtaTask` rolls back a
pending image; on an ordinary boot it exposes bounded `FAILED` status and
rejects check/install requests while autonomous control continues.

`OtaTask` calls SPI-flash APIs, so its 16 KiB static stack and task control block
are explicitly placed in internal DRAM outside the heap-owned service object.
They remain accessible while flash operations disable cache access to PSRAM.

Firmware API routes are authenticated and operational-STA-only. POST check and
install require the exact device Origin; install accepts exactly one canonical
`version` member. Safe GET status accepts the normal browser case with no
`Origin`, but rejects a supplied non-exact Origin. Commissioning SoftAP rejects
all firmware routes. Check admission uses a prebuilt fixed `202` body, so a
local JSON allocation failure cannot report `503` after the check has started;
network delivery can still fail after admission as with any request.

## M14 durable history boundary

History is a platform-owned auxiliary projection of immutable post-control
snapshots. It is not application recovery state and adds no command or mutable
runtime owner:

```text
ControlTask (core 1)
    -> post-tick SmokerSnapshotView
    -> bounded SPSC observation mailbox (drop, never wait)
    -> HistoryTask (core 0)
    -> 4 MiB raw circular flash log

authenticated operational HTTP
    -> bounded read-only query
    -> HistoryTask-owned log state
```

A session writes a committed START, immediate complete CHANGE records, periodic
SAMPLE records every 60 seconds while RUNNING, and one complete END. Idle
snapshots are not stored. Records contain monotonic
session elapsed time; credible synchronized Unix UTC is attached by
`HistoryTask` when available and remains optional.

If Start is followed by Stop or a safety FAULT inside one control cycle, the
post-control projection first sees a terminal session. The mailbox admits its
START+END pair only when two slots are available, or retries the unchanged
terminal snapshot without publishing a partial lifecycle. `HistoryTask` also
retains a failed flash START or END ahead of later mailbox records until that
lifecycle write succeeds; a transient START failure therefore cannot turn its
END into an orphan or starve future sessions.

The log uses 4 KiB erase-sector/pages. Page and record headers are versioned,
CRC checked, and committed last, and records never cross a page. Startup scans
the partition and ignores unclaimed media. Torn or corrupt tails degrade health
without inventing records. Before erasing a multi-page victim, the log commits
an eviction tombstone in reserved page-header space. That tombstone hides the
whole victim and survives until every other victim page is erased; startup
conservatively treats partial NOR program/erase states of that word as an
eviction in progress and finishes it before serving queries. Whole completed
sessions are therefore evicted atomically and oldest first; an interrupted
session may be evicted next, and only a single partition-filling active session
loses its oldest page and becomes explicitly truncated. No fixed retention
duration is promised.

History and OTA use a platform-only flash-operation coordinator. OTA first
defers new history work, waits only within its total install deadline for the
current bounded operation, and then owns flash until completion/reboot. Neither
side calls the application or changes heater state. NVS remains a separate
ESP-IDF persistence concern.

History APIs are authenticated operational-STA GETs with strict bounded query
schemas, 64-bit IDs encoded as decimal JSON strings, pagination/stride, and
chunked bounded response formatting. Commissioning rejects them before
authentication. There is no history write/delete/export/cloud endpoint in M14.

## M15 personal Blynk remote-access boundary

M15 adds one non-critical `smoker_platform` adapter around the official
ESP-MQTT component and Blynk Device MQTT API. It is a transport adapter, not a
new state owner or control service:

```text
Blynk app
    <-> Blynk Cloud Device MQTT/TLS
    <-> MQTT callback -> bounded raw allowlist mailbox
         -> BlynkTask on core 0 -> bounded application command mailbox
         -> ControlTask on core 1 -> SmokerApplication -> safety -> heater

ControlTask
    -> immutable snapshot exchange
    -> platform Blynk adapter
    -> normalized batch status / correlated result / configured event
```

MQTT callback context never calls `SmokerApplication::submit()`. It validates
only the datastream name/size allowlist and copies the complete bounded message
into a raw SPSC mailbox, whose final slot is reserved for Stop. `BlynkTask`
performs deterministic fixed-buffer parsing and translates accepted messages
into its distinct application-level SPSC mailbox. Only `ControlTask` drains
that mailbox and submits the command. A transport-level publish or delivery
acknowledgment is never presented as semantic command acceptance.

HTTP and Blynk share atomic nonzero session/correlation ID generators which
skip the internal OTA reservation, including at wraparound. `ControlTask`
alternates the HTTP/Blynk mailboxes round-robin with one global 13-command
budget, leaving two regular application-queue slots for internal OTA intents;
it stops the external drain after the first Stop barrier.

The outbound status adapter retains one fixed-size normalized projection, one
dirty flag, the last successful status-publish monotonic point, and bounded
serialization storage. On connect/reconnect it publishes one current complete
projection. Later snapshot observations mark the projection dirty only when a
normalized user-visible value differs. Status publication is suppressed until
five seconds have elapsed since the previous status publication, and all
intervening changes collapse into the newest complete `batch_ds`. With no
change, no status publish is scheduled. MQTT keepalive/presence owns connection
liveness; it is not emulated with duplicate telemetry.

Correlated command results and configured Blynk events use separate bounded
messages and may publish immediately. They do not mutate the cached projection
or force an unchanged batch. Publish failure leaves the newest projection dirty
for a later connected attempt but never blocks or feeds back into control.

Control datastreams are edge-triggered transport inputs. The adapter does not
request, replay, or synchronize saved Start or OTA-install values after a
reconnect. A new live Blynk user action is required. State/configuration
datastreams may expose current application values, but Blynk never owns the
authoritative runtime configuration.

The MQTT callback records disconnect/error occurrence independently from the
final connected state and assigns each successful connection a nonzero bounded
generation. Therefore a disconnect followed by reconnect between two
`BlynkTask` polls still forces old-connection cleanup before the new connection
is activated. Raw inputs and translated Blynk mailbox commands retain their
origin generation; `BlynkTask` and the ControlTask-side round-robin drain drop
stale generations. Disconnect also clears the one-shot Start parameter,
unpublished result/feedback/event state, and acknowledges the callback-ordered
inbound-drop watermark. A command received on the new live generation remains
processable and a new-generation mailbox drop remains observable.

The firmware-update control invokes the existing M13 check/install service.
The Blynk payload contains no URL or binary; the platform continues to download
the fixed public GitHub `releases/latest` asset, obtain application permission,
verify the signed ESP32-S3 image, install only outside `RUNNING`, and use the
existing rollback policy. M14 raw history remains local; Blynk datastream
retention is only an auxiliary visualization cache and is not backfilled.

The device token and regional endpoint are platform configuration. The token
is provisioned through bounded UART0 frames and stored in an unencrypted,
versioned CRC-protected blob in a separate NVS namespace. It never enters
snapshots, logs, browser assets, events, command results, or repository
evidence. Missing or invalid configuration disables only Blynk. The accepted
physical-extraction risk is explicit because flash/NVS encryption is outside
M15. Blynk loss, credential
failure, broker throttling, or quota exhaustion cannot become a dependency of
the heater-control path.

## Safety in the control cycle

Conceptually:

```text
requestedDemand = control(...)
safetyResult = safety(...)

if heating is not allowed:
    finalDemand = 0
else:
    finalDemand = requestedDemand

heater.write(finalDemand)
```

Do not implement a separate competing safety task in V0.

## Time

Use monotonic time for:

- durations;
- timer elapsed time;
- control-loop intervals.

Wall-clock time is for:

- UI timestamps;
- event/log timestamps when available.

NTP/timezone corrections must not alter timer duration.

`Duration` and `MonotonicTimePoint` are distinct C++ types. Subtracting two time
points produces a duration; a duration cannot be passed accidentally where an
absolute monotonic point is required.

## Testing architecture

Two levels:

### Native host tests

Primary for `smoker_core`.

Examples:

- Start -> RUNNING
- Stop -> heater off
- target > current -> positive demand
- invalid chamber reading -> FAULT + zero demand
- over-temperature -> FAULT + zero demand
- probe target -> alarm but no heater-control change
- timer threshold logic

Allocation-observation tests replace the ordinary C++ `new`/`delete` family on
the host and measure selected control-cycle paths after initialization. They are
regression evidence for those paths, not proof that no libc, aligned/custom
allocator, ESP-IDF, or target-runtime allocation can ever occur.

### ESP32 integration tests

Used for:

- ESP-IDF adapters;
- task integration;
- hardware drivers;
- persistence;
- OTA;
- real board behavior.

The M5 ESP-IDF build proves target compilation. M6A additionally exercises task
scheduling, the stack high-water mark, and task-watchdog panic/reset on the
connected ESP32-S3 N16R8 target. Exact carrier identity and exposed carrier pins
are recorded for the final SuooTci `KFB003` controller, completing M6A.
External sensing/output and electrical-safety gates remain M6B work.

### Executable architecture guardrails

`tools/check_architecture.py` enforces the M0-M15 layer imports, component graph,
the sole critical `ControlTask` plus the captive-DNS helper, `OtaTask`,
`HistoryTask`, and non-critical `BlynkTask`,
heater-write owner, single production command-submit site, M12
transport/core-placement contracts, M13 effective generated configuration,
signed serial-flash/release contracts, partitions/update placement, exact V0
session states/command family, single-stage recipe shape, and thin composition
root. M14 checks additionally cover 32-bit/non-blocking history publication,
absence of wall-clock/flash work from `ControlTask`, raw-log ownership,
authenticated read-only routes, and OTA/history serialization.
M15 checks additionally cover the exact MQTT pin, platform confinement,
callback isolation, two bounded mailboxes, fair ControlTask draining, static
core/priority placement, TLS/session/topic settings, provisioning boundaries,
status/result separation, and bounded payloads.
The active M7 and inactive M8/M9 checks additionally keep hardware/component
types target-only, preserve the documented mixed production composition,
enforce exact registry pins and explicit configuration/call ordering, and reject project-owned
waits/tasks/steady-state allocation in their adapter paths. The M8 check can
optionally validate the reviewed upstream source when managed components are
present, but never fetches and passes a tracked-files-only checkout when they
are absent.
`tools/check_traceability.py` requires one explicit matrix row
for every approved rule and validates concrete host-test references for rules
marked implemented.

These source-level checks are intentionally narrow. They complement host
behavior tests and the ESP-IDF cross-build; they are not target-runtime or
hardware-safety proof.

## OTA placement

OTA is not a core-domain concern. M13 realizes this placement without adding an
application updater port: the application owns only permission/interlock
commands, while the platform service owns update mechanics.

```text
smoker_app
    -> PrepareFirmwareUpdateCommand / FinishFirmwareUpdateCommand
    -> firmware_update_active snapshot

smoker_platform
    -> ESP-IDF OTA implementation
```

OTA must remain outside the critical temperature-control dependency chain.
