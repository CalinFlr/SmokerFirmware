# Roadmap

The roadmap controls implementation scope.

A future item is **not permission to implement it early**.

## Scope and current status

- **M0-M5 — V0 simulated application/control slice:** complete. This means the
  application workflow and safety gate are host-tested and the ESP32-S3 image
  builds; it is not a thermal-plant simulation or real-controller validation.
- **M6A — controller-board identification:** complete. The final SuooTci
  `KFB003` N16R8 controller, integrated carrier capabilities/restrictions,
  storage readback, native USB flashing, sustained runtime, stack watermark,
  and TWDT panic/reset are recorded.
- **M6B — external-hardware identification:** started for chamber and probe
  acquisition. MAX31865 plus a three-wire PT100 and two ADS1115 converters are
  selected, with exact registry drivers imported. The final soldered MAX31865
  assignment is SPI2/GPIO12 SCK/GPIO11 MOSI/GPIO13 MISO/GPIO10 CS, and the
  supplier-documented probe assembly and maintainer-reported three-wire
  placement are recorded. The first connected diagnostic failed because MISO
  followed its pulls; a later corrected run passed pull-independent SPI,
  exact configuration/shutdown readbacks, and ten stable raw samples. The
  module/revision, fitted Rref, continuity, and shield remain open. The first
  ADS1115 is now wired at 3.3 V on GPIO17 SDA/GPIO18 SCL with ADDR=GND
  (`0x48`) and passed a connected register/single-shot digital check. Four
  `NTC100` channels with nominal 100 kOhm 0.1%/100 nF networks were reported
  assembled. Only A3 was analog-exercised: a failed near-ground run was
  preserved, followed by corrected room-condition and uncontrolled-heating
  response evidence consistent with NTC behavior. A0-A2, exact probe curves,
  calibration/accuracy, actual rail/resistor values, connectors, external
  pull-ups, and the second ADS1115 remain open. SSR, power,
  and independent-protection hardware are still blocked on exact parts.
- **M7 — real authoritative chamber integration:** complete for its defined
  software integration and connected ordinary-runtime functional activation.
  MAX31865 is active as the ordinary chamber source; the short target run is
  not chamber-hardware or physical-regulation qualification.
- **M6B and M8-M10 — remaining controller product baseline:** incomplete. M8
  and M9 have host-tested/cross-buildable PID and staged one-or-two-ADS1115 software
  boundaries. Food probes and heater remain simulated, production PID remains
  inactive, and outstanding physical qualification stays gated by M6B. Product
  V0 cannot be called complete before real output, food probes, persistence,
  recovery, and their hardware qualifications are implemented and validated at
  their appropriate levels.
- **M11 — local display:** postponed because no display has been purchased.
- **M12 — Wi-Fi + local API/UI:** implemented for the simulated controller and
  host/cross-build validated; physical radio/provisioning/runtime validation on
  the final KFB003 board remains required before M12 is complete.
- **M13 — OTA + rollback:** complete for its defined scope. Signed USB
  migration, public GitHub OTA into the second slot, forced pending-image
  rollback, clean reinstall, five-cycle validation, and persistent reboot
  passed on KFB003.
- **M14 — durable local history:** implemented for simulated I/O with host,
  browser, guardrail, cross-build, signed connected-board migration, reboot,
  and stack/runtime validation; deliberate Wi-Fi loss remains pending.
- **M15 — personal Blynk remote access:** implemented for host and ESP-IDF
  cross-build validation. One owner controls one home smoker through Blynk's
  existing app and MQTT/TLS service. KFB003 provisioning, live home-STA/TLS,
  status, simulated Start/Stop, reboot no-replay, remote-error e-mail, and
  firmware-check scenarios passed; native mobile UI, phone push, exact broker
  timing, and deliberate transport-loss scenarios remain target-pending.

Rule-level implementation and validation evidence is in
`docs/TRACEABILITY.md`.

## M0 — Repository and ESP-IDF bootstrap

Status: **Complete — ESP-IDF build validated, no hardware runtime claim.**

Goal:

- create clean ESP-IDF project;
- target ESP32-S3;
- pin/document IDF baseline;
- keep `app_main` thin;
- establish the three logical components;
- build successfully.

Do not require real external hardware.

Do not invent pin mappings.

Definition of done:

- `idf.py build` succeeds;
- project structure matches architecture;
- no placeholder forest for future features;
- README contains build commands.

## M1 — Core domain model + host test harness

Status: **Complete — native host validated.**

Implement only the core types/rules needed for V0.

Initial tests should cover approved invariants.

Definition of done:

- core is buildable/testable without ESP-IDF runtime/hardware;
- host tests run from a documented command;
- no hardware dependencies in `smoker_core`.

## M2 — Simulated chamber + simulated heater

Status: **Complete — deterministic application simulation validated on host.**

Add application ports and simulated platform adapters required to execute a control loop.

Goal:

```text
simulated chamber
    -> controller
    -> normalized heater demand
    -> simulated heater
```

Definition of done:

- application can run a deterministic simulated control cycle;
- heater output is observable in logs/tests;
- no real GPIO required.

## M3 — Session + one Stage + Timer

Status: **Complete — native host validated.**

Implement:

- Start;
- Stop;
- recipe snapshot;
- exactly one stage;
- optional timer;
- timer start conditions approved in business rules.

Definition of done:

- complete simulated session can start/stop;
- timer behavior has host tests.

## M4 — Safety V0

Status: **Complete for the M4 definition below — native host validated.**

This status does not claim that later reset-recovery or independent hardware
protection requirements in `docs/SAFETY.md` are implemented.

Implement:

- safety override;
- invalid authoritative chamber fault;
- max chamber-temperature fault;
- heater OFF outside RUNNING;
- latched-fault behavior.

Definition of done:

- safety rules have automated tests;
- simulated heater cannot be driven by a bypass path.

## M5 — V0 simulated application/control slice

Status: **Complete — host-tested application/control slice and ESP-IDF build.**

Add simulated 1..N food probes, alarms, snapshots, and event flow.

Definition of done:

- simulated application/control workflow can run end-to-end;
- probe targets alarm without affecting heater demand;
- snapshots expose current state;
- core tests pass.

## M6A — Identify and validate the controller board

Status: **Complete — final SuooTci KFB003 N16R8 controller identified and
target-runtime validated.**

Confirm and document:

- exact development-board model and ESP32-S3 module marking;
- whether this is the intended product controller or a development-only board;
- flash capacity;
- PSRAM presence/capacity;
- USB/JTAG/UART capabilities;
- integrated Wi-Fi/Bluetooth LE capability required by later connectivity work;
- exposed/usable GPIO and boot/strapping restrictions;
- onboard display/touch controller, if present;
- `ControlTask` scheduling, watchdog timeout/reset, stack high-water mark, and
  reset behavior on this exact target.

Do not assign external sensor, probe, or SSR pins until their M6B interfaces are
known.

Flash capacity and product-board status are now confirmed. M13 may choose and
validate the OTA/rollback-capable partition layout for this final board; M6A
does not scaffold that future feature.

Definition of done:

- the M6A checklist in `docs/HARDWARE.md` contains evidence rather than assumed
  values;
- target-runtime results are distinguished from cross-build results;
- no external sensor, SSR, electrical, thermal, or independent-safety claim is
  made.

## M6B — Identify external sensing, output, and safety hardware

Status: **In progress — MAX31865/three-wire PT100 functional communication,
raw sampling, and checked shutdown passed after the preserved first
floating-MISO failure; the installed ADS1115 passed connected digital
communication and corrected A3-path response on its assigned bus after a
preserved failed near-ground run. A0-A2, calibration/accuracy, the second
ADS1115, physical module identity, complete electrical qualification, and
remaining external hardware are still incomplete.**

External-adapter activation requires documented facts, explicit evidence-class
boundaries, and a recorded decision. MAX31865 functional evidence permits its
ordinary activation with provisional choices labeled; it does not complete
the remaining M6B electrical, calibration, heater, or independent-safety work.

Confirm and document:

- authoritative chamber sensor and electrical frontend;
- food-probe frontend/protocol;
- SSR model and input requirements;
- heater power;
- power-supply rails;
- independent thermal/electrical cutout design;
- optional current-sensing hardware, if actually present;
- final external-interface pin assignments checked against M6A restrictions.

The relevant interface portion of M6B must be confirmed before its M7, M8, or
M9 integration begins. M6B completes only when the complete external-hardware
checklist is resolved.

## M7 — Real authoritative chamber sensor

Status: **Complete for the defined software integration and connected ordinary-
runtime functional activation. Diagnostic SPI/configuration/raw/shutdown and a
signed 179-second ordinary run are T-pass after the preserved first floating-
MISO failure. Remaining chamber-hardware qualification stays under M6B/pre-
real-heater and release gates.**

The ordinary composition uses MAX31865 as its only authoritative chamber
source. Simulated chamber infrastructure remains available only to host tests;
food probes and heater output remain simulated in production.

Use the exact-pinned ESP Component Registry dependency
`esp-idf-lib/max31865` 1.0.8 rather than rewriting the register protocol. Its
70 ms `max31865_measure()` convenience call is forbidden inside the critical
cycle. The active backend configures continuous conversion and
distinguishes descriptor/configuration success from sample readiness. Its
host-tested monotonic policy returns absence and performs no fault/temperature
register read before the official maximum first-conversion interval: 55 ms for
60 Hz or 66 ms for 50 Hz. Every successful configuration, including recovery
after fault clear, resets this boundary.

Project-owned read code contains no explicit delay, task creation, heap
allocation, or `max31865_measure()` call. This source/host evidence does not
prove ESP-IDF/driver/SPI allocation behavior or bound real SPI worst-case
blocking. Module/input-network settling and sustained target timing remain
connected-hardware gates.

The confirmed RTD configuration is PT100 with three leads, corresponding to
driver nominal resistance `100.0F` and `MAX31865_3WIRE`. The fitted module
reference resistor remains an independent required physical fact. Production
uses provisional Rref 430.0 ohm, 50 Hz, ITS-90, and 100 kHz; these operational
choices must not be confused with a measured fitted resistor, its tolerance,
or calibrated accuracy.

The project adapter maps every SPI, conversion, non-finite, and MAX31865 fault
to an absent authoritative measurement; existing safety then latches
`ChamberSensorInvalid` and commands heater OFF. No last-known-value fallback is
allowed.

The sensor-specific validity policy requires finite, strictly ordered explicit
bounds and accepts the inclusive supplier-documented assembled-probe range
-50.0..+200.0 C. This is documentation-backed operational policy, not measured
calibration. Raw-zero-like -242.02 C and every finite value outside the range
are absent without changing the global finite `Temperature` domain.

Production centralizes SPI2/GPIO12 SCK/GPIO11 MOSI/GPIO13 MISO/GPIO10 CS,
100 kHz, PT100/three-wire, 50 Hz, ITS-90, Rref 430.0 ohm, active `0xD1`,
terminal `0x11`, and a 66 ms first-conversion boundary. Runtime owns the bus
before the descriptor, applies a checked GPIO13 internal MISO pull-up after bus
initialization and before descriptor access, destroys the descriptor before the
bus, and restores MISO to floating after successful bus release. It uses the
real monotonic clock and completes bus/descriptor/configuration plus the first
sample wait before creating the sole `ControlTask` when sensor startup succeeds.
Bus/pull, descriptor/configuration, or boundary failure instead creates the
ordinary runtime with a permanently unavailable chamber source: its first IDLE
tick publishes `ChamberSensorInvalid`, FAULT, no chamber value, and heater OFF.
Normal services still start, and a pending OTA image rolls back through the
published fault; only critical runtime allocation or task creation retains the
immediate bootstrap rollback path.

The software-only board diagnostic remains default-OFF and compiles as an
exclusive target composition that cannot construct the normal application,
ControlTask, or heater output. A corrected bounded mode-1 software-SPI path and
the pinned driver both perform pull-discriminated configuration readback using
only persistent defined bits. It then reports ten raw RTD codes, raw/reference
ratios, and fault bits without calculating temperature. Both stages use an
exact command-zero terminal configuration (`0x11`: AUTO/VBIAS off,
three-wire/50 Hz) before ownership release. The normal driver path requires
write/readback success, while bounded RAII fallbacks attempt the same cleanup
on early returns. Software-SPI first restores idle clock and a CS-high frame
boundary; descriptor removal still precedes bus release. Sensor
fault samples remain distinct from SPI/shutdown failure. Separate ordinary and
overlay builds validate the intended ordering and isolation. On 2026-08-24 the
first run failed at pull-following MISO and remains recorded. A later corrected
setup observed pull-independent `0x11`, exact software `0x00`/`0x91`/`0xD1`,
exact software terminal `0x11`, driver initial `0x11` and active `0xD1`, ten
raw 8548/8549 samples with zero fault/transaction/sensor-fault counts, and
driver terminal `0x11`. A separate raw-zero/`0x40` observation is not treated
as controlled open/short evidence.

M7 completion is split by evidence class: SPI/configuration/raw/shutdown
functional bring-up is T-pass; adapter behavior and ordinary activation are
H-pass/B-pass/Guardrail. The signed 1,445,888-byte serial target, SHA-256
`4b1541202eaa7388b79c48f9f615fd7443959b5ad943f12652e3cfa4ea95ffcc`,
then reported 25.7, 25.7, and 25.8 C at cycles 1, 60, and 180 over approximately
179 seconds while IDLE with no target and simulated heater 0.0%. These readings
satisfy the intended three-reading, at-least-120-second functional observation;
exact cycle 120 was not required. No chamber/control, watchdog, rollback,
unexpected-reset, or diagnostic failure appeared.

The serial image used blank initial OTA metadata, which ESP-IDF 6.0.2 selected
directly as `ESP_OTA_IMG_VALID` in the no-factory layout. Its
`PENDING_VERIFY`/five-cycle criterion is inapplicable and waived; no pending
state is created or forced. OTA-005 remains unchanged for actual OTA-installed
pending images, and a future sensor-faulting pending-image test remains separate
from M7 completion.

M7 is therefore complete for its defined scope. Physical module identity,
fitted Rref/tolerance, continuity and shield termination, calibrated accuracy,
controlled open/short behavior and recovery, longer-duration behavior,
response/noise, heater interference, and independent electrical/thermal safety
remain M6B/pre-real-heater and release gates. The 179-second observation is not
sustained-duration qualification or physical temperature regulation: heater/
SSR and production PID remain inactive. These gates do not block beginning
ADS1115 integration.

The chamber-sensor/frontend facts sufficient for M7 activation were established
within the still-incomplete M6B record.

## M8 — Real SSR heater output + PID control

Status: **First software-integration slice implemented but inactive — the
application boundary and exact-pinned float PID adapter are host-tested and
ESP-IDF 6.0.2 cross-buildable; production retains deterministic control and a
simulated heater, and all cadence, tuning, SSR, thermal-plant, and hardware-safety evidence remains
pending.**

Implement real platform heater driver.

The first inactive slice adds `espressif/pid_ctrl` exactly 0.3.1 at component
hash `974be0666bb4d95f49677327dd8305781d04d8bae284fdde2fbadf06ca9d4979`.
Its mandatory `espressif/iqmath` 1.11.0~1 dependency is locked at
`39448db759b410373e543798167ca4670bbff3019cb290a2fe8e627221e71b9d`.
The PID component and ESP-IDF types remain target-only in `smoker_platform`.

`SmokerApplication` now obtains requested demand through injected
`IChamberController`. Ordinary production composition explicitly uses
`DeterministicChamberController`, preserving the M2 100/0 behavior with
`Max31865ChamberSensor`, `SimulatedFoodProbeSource`, and
`SimulatedHeaterOutput`. The real float PID adapter is compiled but not composed.
No SSR output, GPIO, or switching window exists.

The PID adapter is synchronous and creates no PID task, I/O, delay, wait, lock,
or steady-state project allocation. It fixes error direction as target minus
measured and returns only a valid normalized 0..100% request. Compute/reset
failure fails closed as latched `ControlLoopFailure`. Safety is evaluated after
the request and remains authoritative before the only heater write. Boot and
every transition out of eligible RUNNING control reset/disable latent state;
fault clear never restarts heating and a clean explicit Start remains required.

The selected numeric backend is float: the project domain boundary already
uses float, and ESP-IDF 6.0.2 records single-precision FPU support for ESP32-S3.
The target RAII owner uses the exact 0.3.1 `_f` create/compute/reset/delete APIs.
Creation allocates the upstream control block during initialization; reviewed
valid compute/reset paths intentionally do not allocate.

The component provides positional and incremental forms but no autotuning,
plant identification, sample-period, or `dt` input. Positional form accumulates
and clamps raw per-call error before applying Ki; incremental form ignores those
bounds and instead retains/clamps output. Project configuration exposes the
accumulated-error bounds only for positional form and maps the ignored upstream
incremental fields to `0/0`. Both forms differentiate error with no derivative
filtering or derivative-on-measurement, so target-minus-measured input permits
setpoint kick. Calculation form, real call period, gains, positional
accumulated-error bounds, common output bounds, derivative treatment, and SSR
window must be selected and validated using the identified sensor, heater,
smoker, and protection hardware. Neither form is production-approved.
Automatic tuning is a separate future decision. Do not infer tuning values from
M2 simulation or present any simulated result as a production recommendation.

Do not bypass the approved safety gate.

Electrical work must respect independent hardware safety design.

Requires M7 plus the SSR/heater and independent-protection portions of M6B.

The inactive software slice does not satisfy those activation prerequisites;
M6B, M8, and M9 remain incomplete. M7's sensor-integration prerequisite is
satisfied, but real-heater activation remains gated by the outstanding M6B
hardware qualifications above.

Definition of done:

- the pinned official component passes host-boundary and ESP32-S3 integration
  tests through the project adapter;
- boot, missing target, Stop, invalid measurement, and fault reset/disable PID
  state and command heater OFF according to the existing rules;
- deterministic sample timing and allocation-free `compute` behavior are
  verified;
- tuning and closed-loop behavior are validated on the real thermal plant;
- safety-gated OFF and independent hardware protection are validated
  separately from PID performance.

## M9 — Real food probes

Status: **Software adapter and explicit i2cdev ownership implemented but
inactive — host behavior and ESP-IDF 6.0.2 API compatibility are validated;
one installed ADS1115 now has
GPIO17/18, address `0x48`, connected digital evidence, and corrected
A3-divider/jack/probe response evidence after a preserved wiring failure. A0-A2,
module identity, `NTC100` curve/calibration/accuracy, second-device address,
runtime service placement, and qualification remain blocked on M6B.**

Integrate actual probe frontend/protocol.

Food probes remain monitoring/alarm inputs only.

Use the exact-pinned ESP Component Registry dependency
`esp-idf-lib/ads111x` 1.1.14 rather than rewriting its I2C register protocol.
The upstream example demonstrates two devices on one bus, but its GND/VCC ADDR
straps are example wiring only. The installed project device uses ADDR=GND
and responded at `0x48`; the project must still document the second distinct
physical address from `0x49..0x4b` before dual-device activation.

The later A3 evidence was produced by a temporary signed ESP-IDF 6.0.2
diagnostic using direct `i2c_master` APIs, internal SDA/SCL pull-ups, and
100 kHz. It did not exercise `Ads1115TargetBackend`, pinned `i2cdev`, this M9
sequencer, production composition, ControlTask timing, or sustained acquisition.
The initial 20-sample run started from `0xF383` and stayed near 0 V, consistent
with A3 being grounded or missing the high-side branch. After correction, a
room-condition run moved from raw 18329 to 18040 (2.2911 V to 2.2550 V;
nominally 227.10 kOhm to 215.79 kOhm). Uncontrolled soldering-tool heating
moved overall from raw 8300 to 1174 (1.0375 V to 0.1468 V; nominally
45.86 kOhm to 4.65 kOhm), with a small intermediate reversal. These nominal
resistances used unmeasured 3.3 V and 100 kOhm values and prove only connected
A3 path response consistent with NTC behavior, not temperature, R25, Beta,
curve identity, calibration, or accuracy.

The A3 diagnostic wrote `0x8583` at exit and immediately observed `0x0583`,
consistent with a just-started one-shot conversion rather than proved terminal
idle. Idle polling and terminal `0x8583` verification are required before the
procedure is reused. The separate 2026-08-25 digital test did verify terminal
`0x8583` and remains a distinct result. The last known board image after the A3
session was the temporary diagnostic; repository production composition still
uses `SimulatedFoodProbeSource`.

The inactive adapter stays in `smoker_platform` behind `IFoodProbeSource`,
without a separate sensor task. It accepts one or two explicitly configured
devices, which supports the one installed module without fabricating a second
device/address. Zero or more than two devices, an unconfigured-device channel,
and any configured device without a channel are rejected; probe IDs and muxes
per device remain unique. One acquisition owner retains independent per-device
synchronization/quarantine state. Every configured ADC begins unsynchronized;
first use and recovery require a successful idle observation which discards
any stale result and never configures/restarts in that service step. Busy or
unknown devices are skipped so another configured healthy ADC progresses even
when consecutive logical channels use the quarantined device.

Only a synchronized idle device receives explicit mux/gain/rate configuration
and single-shot start. The deadline is established after successful start. A
later service step observes readiness before applying the deadline: ready is
accepted even at/after it, while still busy at/after it, failed busy
observation, or failed start quarantines the ADC and discards the abandoned
result. Configuration failure while idle, ready-then-value failure, and
calibration/validity failure invalidate only the affected probe. This
classification is grounded in pinned 1.1.14, whose non-OS configuration writes
clear OS and cannot start a single-shot conversion. `read(probe_id)` is
I2C-free and returns an independently timestamped cached temperature only
before the required configured maximum age expires.

Raw codes require injected calibration/validity with no physical defaults.
All buses, pins, pull-ups, addresses, mappings, mux/gain/rate values, conversion
timeout, and sample age remain explicit configuration. Same-bus devices require
compatible settings and distinct addresses; genuinely separate buses may reuse
an address. The target backend overrides `ads111x_init_desc()`'s hard-coded
1 MHz descriptor clock before the first transaction and never touches its
unused second storage slot for a one-device configuration.

A target-only non-copyable owner now calls real locked `i2cdev_init()` and must
be active before descriptor initialization. Checked backend shutdown attempts
all acquired descriptors and releases its subsystem lease only when every
`ads111x_free_desc()` call returns `ESP_OK`; checked subsystem shutdown then
reports whether real `i2cdev_done()` returned `ESP_OK`. Locked 2.1.2 can swallow
nested device/bus deletion errors during descriptor cleanup, so neither result
proves every nested teardown succeeded or physical/driver quiescence. Because
2.1.2 also leaves its function-local initialized flag true, one owner instance
rejects its own restart but cannot exclude a simultaneous or later instance.
This ownership is compiled but remains uncomposed in ordinary production.
Activation must provide exactly one owner/initialization per boot; restart after
any real release remains unsupported until a future pinned dependency proves a
restartable lifecycle. No project-global mutable state is added for this
inactive path.

Project-owned service/read code has no explicit delay, polling loop, new task,
or steady-state allocation. Locked `i2cdev` still uses timeout-capable mutex/
I2C operations, lazy bus setup, internal retry delays, and possible allocation,
so target ControlTask suitability remains unproven and production stays on
`SimulatedFoodProbeSource`. An ADC/read/calibration/validity failure is absent
only for the affected monitoring probe and cannot change chamber control,
chamber faults, or heater demand.

Confirm the device-specific maximum configured probe count from the actual
frontend/hardware. Do not turn that capacity into a universal `smoker_core`
constant.

Requires the food-probe frontend/protocol portion of M6B.

M9 remains incomplete. The eventual product may still use the selected two
converters and six probes, but the second converter/address remains deferred
and unconfirmed and neither number becomes a universal `smoker_core` rule. M9
cannot complete until module manufacturer/revision and external pull-up
rail/value are recorded; actual rail and individual reference resistors are
measured; R25 and a documented/fitted curve are established from stable,
co-located points against a separately validated reference; A0-A2 and the
second module/address are exercised; and device-specific capacity is confirmed.
Known-resistance/known-voltage, disconnect/open/short, noise, settling,
accuracy, repeatability, sustained-operation, and heater-interference tests
also remain required. PT100/MAX31865 is only a possible future reference after
its own accuracy/reference checks; no ice-bath or simultaneous calibration run
occurred in the A3 session. Project backend/sequencer integration, production
activation, and service placement proven against the real ControlTask budget
remain separate gates.

## M10 — Persistence + power recovery

Persist:

- device configuration;
- saved recipes;
- sufficient active-session state.

Implement `resumeAfterPowerFailure`.

Define and enforce bounds for persisted/untrusted configuration before runtime
allocation, including probe collection size, probe/recipe text sizes, recipe
payload size, and timer values. Numeric limits must follow identified hardware
and storage constraints rather than M5 simulation assumptions.

Completing M10 closes BR-011 and the recovery portions of SF-004/SF-006; M5
does not claim those behaviors.

## M11 — Local display

Status: **Postponed — display not purchased.**

Add display/UI only after core control path is stable.

Display failure must not affect control.

## M12 — Wi-Fi + local API/UI

Status: **Implemented — host/build/browser pass and target boot verified;
iPhone/radio scenarios pending.**

Add local connectivity.

Local cooking/control remains fully autonomous if connectivity is absent.

Controller-board integration and target validation depend on M6A, not on M6B
sensor/SSR availability. Real sensing and heater output remain governed by
M7-M9.

The implementation boots `IDLE` with simulated heater OFF and requires explicit
Start. It tries persisted STA credentials, exposes the open `Smoker-<MAC6>` at
`192.168.4.1` immediately when no STA is configured and after 30 seconds when a
configured STA remains disconnected, and publishes `smoker-<mac6>.local`.
The SoftAP is commissioning-only: its public Romanian page, status, scan, and
credential-save endpoints expose no snapshot, cooking state, login, dashboard,
or command route, even when a valid cookie is supplied. Request scope is derived
from the socket's local AP/STA address before authentication; unknown addresses
fail closed.

After WPA2/WPA3 Personal STA connection, the LAN surface offers password-only
login using the product-fixed initial password `smoker257500`. Login creates one
random 256-bit `HttpOnly`, `SameSite=Lax` cookie with a 30-minute idle timeout;
new login replaces the old token. Logout and a separate current/new password
operation invalidate it. HTTP Basic/admin and STA OPEN are absent. APIs return
JSON `401` without a Basic challenge, and all authenticated/provisioning writes
require exact Origin. Failed logins remain rate-limited.

DHCP option 114 and wildcard DNS redirect captive probes to the absolute
`http://192.168.4.1/` Wi-Fi setup page, never to login. The AP closes after a
STA IP is obtained; Wi-Fi-loss fallback reopens the same setup-only AP without
entering the autonomous control path.

The UI starts an asynchronous 2.4 GHz scan, offers up to 20 sanitized,
deduplicated visible SSIDs, marks only WPA2/WPA3 Personal selectable, and
permanently retains manual entry for hidden networks. Scan and STA reconnect are
serialized, with a 15-second timeout restoring recovery if the driver wedges.
The AP and STA pages are separate embedded experiences.

HTTP commands cross a 16-entry SPSC mailbox with reserved Stop admission and a
32-bit correlation ID; immutable snapshots carry bounded semantic command
results across a preallocated non-blocking triple exchange.
`ControlTask` remains priority 2 with a static 12 KiB stack and is pinned to
core 1; connectivity services run on core 0.

D049 additionally fails closed during APSTA overlap: operational HTTP remains
unavailable until `AP_STOP` confirms that the open commissioning surface is
inactive. Legacy authentication migration rejects unreadable/corrupt/claimed-
without-password state, command-admission JSON is completed before mailbox
publication, and coalesced application Stop IDs inherit the processed Stop's
semantic result. Host, sanitizer, browser, and ESP-IDF 6.0.2 gates pass; the
985,408-byte image uses 64.2% of the 1500 KiB application partition. Physical
AP-disable and NVS-failure injection remain pending.

M12 completes only after final-board checks cover AP/STA provisioning and
persistence, mDNS, authentication, explicit Start/Stop including saturated Stop
admission, automatic iPhone captive opening, real scan/refresh/hidden-SSID
fallback, wrong-password retry, Wi-Fi loss during RUNNING, a stable ten-minute
run, ControlTask/DNS core affinity and stack watermark, and TWDT behavior. These
checks were completed before M7 activation and validate the board, radio, and
then-simulated I/O only—not the now-active chamber sensor, SSR, electrical
protection, or thermal safety.

The 2026-08-17 target flash/boot check verified image SHA, ESP-IDF 6.0.2,
`ControlTask` on core 1 in `IDLE` with simulated heater `0%`, Wi-Fi on core 0,
the then-current open SoftAP `Smoker-[REDACTED_MAC6]` at `192.168.4.1`, authenticated HTTP startup, and
captive DNS startup on core 0. It did not execute the remaining
iPhone/provisioning or RUNNING Wi-Fi-loss scenarios and is not hardware-safety
evidence.

The subsequent consumer-hardening build historically introduced generated
WPA2 and HTTP credentials. D045 restored `smoker257500` for the initial HTTP
login, and D046 later restored the open commissioning AP. D047 subsequently
restricted that AP to Wi-Fi setup, removed Basic/admin and STA OPEN, and
retained sessions, rate limiting, Host/Origin policy, and other request
hardening. The build also
selects ESP-IDF's built-in 1500 KiB single-app partition; the current D048
983,440-byte build uses 64.0% and leaves 552,560 bytes. These changes are
build/browser/host-test
validated but still require the pending physical AP/STA/iPhone scenarios.

The 2026-08-17 D045 target check SHA-verified that build, erased and read back
only the 24 KiB NVS partition as empty, and observed a clean boot with simulated
heater `0%`, generated WPA2 `Smoker-[REDACTED_MAC6]`, DHCP/captive DNS, and the fixed
initial HTTP password `smoker257500`. A preceding preserved-NVS boot also
confirmed migration of an unclaimed generated HTTP password to the fixed
default. This is target evidence for factory commissioning and migration, not
for iPhone captive opening, a completed HTTP login, STA provisioning, or
hardware safety. D046 supersedes only that check's WPA2 AP behavior; its target
validation is recorded separately after deployment.

The 2026-08-17 D046 target check flashed and SHA-verified the 973,088-byte image
(`a61ab8bf09ced2ae70f8e46ad44c3b6d0d6eafdffa702d4c650dcfd4a76ea5af`),
erased only the 24 KiB NVS partition, and read it back with zero bytes differing
from `0xFF`. The clean boot exposed the explicitly open `Smoker-[REDACTED_MAC6]`
SoftAP at `192.168.4.1`, retained HTTP user `admin` and initial password
`smoker257500`, started DHCP and captive DNS, and kept the simulated heater at
`0%`. This is target evidence for the D046 factory boot configuration, not for
iPhone captive opening, a completed browser login/STA provisioning flow, or
hardware safety.

A later preserved-NVS boot and authenticated API read on 2026-08-17 observed
the saved `[REDACTED_STA_SSID]` at `[REDACTED_STA_IP]` with SoftAP inactive. This proves
saved-STA boot on that network, but not the still-pending wrong-password,
hidden-SSID, iPhone CNA, or RUNNING Wi-Fi-loss scenarios.

The 2026-08-17 D048 remediation redeployed and SHA-verified the 983,440-byte
image (`4f8d6e1bff566b09542ab5ce3e32f60e94683d72212478e0519282f9231f2fc0`)
without erasing NVS. Boot retained `[REDACTED_STA_SSID]`, reached `[REDACTED_STA_IP]`, kept
`ControlTask` on core 1 in `IDLE` with simulated heater `0%`, and served the LAN
login while unauthenticated snapshot access returned `401`. A read-only NVS
audit found both authoritative `sta_config_v1` and `auth_config_v1` blobs. This
validates preserved-state deployment and blob presence, not injected flash
failure, iPhone/radio edge scenarios, or hardware safety.

## M13 — OTA + rollback

Status: **Complete for the defined M13 scope — host/build/browser, signed USB
migration, public GitHub OTA, rollback, and final target validation pass.**

Implemented in software:

- manual authenticated STA-only check against the fixed public GitHub Release;
- download/install only while not `RUNNING`, with an application-owned Start
  interlock and correlated permission;
- version `0.13.0`, a rollback-capable dual-3-MiB partition table, certificate
  bundle, SNTP, HTTPS OTA, RSA-3072 publisher-signature verification without
  hardware Secure Boot, and app rollback;
- critical-startup validation across five safe ControlTask/TWDT cycles before
  marking a pending image valid, with immediate rollback when runtime-context,
  ControlTask, or OtaTask bootstrap fails;
- embedded Romanian dashboard controls and a tag-gated, tag-restricted release
  workflow that signs outside ordinary CI, verifies against the versioned
  public key, and publishes the signed binary and SHA-256 after host and
  ESP-IDF 6.0.2 validation;
- a public canonical source/release repository initialized from one sanitized
  root snapshot, allowing credential-free device download while RSA signatures
  remain the publisher-authentication boundary;
- effective generated-Kconfig validation that rejects stale build directories,
  plus an explicit serial helper which accepts only the signed application and
  its matching generated bootloader, partition table, and OTA metadata;
- fail-closed prerequisites on every ordinary ESP-IDF target that could write
  the deliberately unsigned application or a partial M13 boot layout.

An unavailable OTA worker fails the firmware API closed with bounded `FAILED`
status while autonomous local control continues; firmware check admission uses
a fixed response and performs no post-admission JSON allocation.

Host, sanitizer, HTTP/browser, guardrail, and ESP-IDF cross-build evidence cover
the software boundary. Separate connected-target evidence now covers the
documented KFB003 two-slot installation and rollback scenario; neither class of
evidence is a hardware-safety test.
The first M12 migration requires a complete serial flash through the signed
serial helper; ordinary unsigned `idf.py flash` output is not bootable under
the M13 signed-update bootloader. D051 records the single-maintainer condition
under which the missing independent release reviewer is accepted as P3
hardening. OTA does not depend on M6B sensor/SSR availability, and M13 makes no
hardware-safety claim.

On 2026-08-17 the signed helper migrated the connected KFB003 through its native
USB Serial/JTAG port. `esptool` identified ESP32-S3 revision 0.2, 16 MiB flash,
and 8 MiB PSRAM, verified every written hash, and boot logs confirmed version
`0.13.0` from `ota_0`, the expected dual-slot table, OtaTask on core 0,
ControlTask on core 1, and simulated heater `0%`. Pre/post read-only NVS audits
had valid page CRCs and identical logical key/value CRCs; the saved STA then
reconnected at `[REDACTED_STA_IP]`. An authenticated firmware check validated the
GitHub TLS certificate and failed closed on HTTP `404`, as expected before a
release existed. This closed the first serial migration and preserved-state
evidence; the later release scenario below closes the remaining M13 gates.

On 2026-08-18 public protected-main CI passed and the tag-restricted workflow
published the RSA-verified `v0.13.0` application. Anonymous download produced a
1,249,280-byte ESP32-S3/ESP-IDF 6.0.2 image with SHA-256
`b511934ec354392ba6ee20e4b687d6e3e765e9722a0e2c3cdf5fafc7f559e91b`.
The initial target attempt revealed that GitHub's real redirect expanded to a
923-character URL and an 893-byte request line, exceeding ESP-IDF's default
512-byte TX buffer. After a reviewed, guarded 4,096-byte request-buffer fix, a
signed `0.12.99` bootstrap installed through the USB helper successfully found
`0.13.0` and OTA wrote it to `ota_1`.

A forced reset during the first `PENDING_VERIFY` boot returned the board to
`0.12.99` in `ota_0`. A second installation booted `0.13.0`, marked it valid
after five safe ControlTask cycles, and remained on `ota_1` after another
controlled reboot. The simulated heater command was `0.0%` at the observed
rollback and persistent boots, and the final firmware API was `IDLE` with no
error. Repository evidence omits SSID, IP, MAC, and native-USB identifiers.
This completes M13's OTA/rollback scope without making any sensor, SSR,
thermal, Secure Boot, flash-encryption, or independent electrical-safety claim.

## M14 — Telemetry/history

Status: **Implemented for simulated I/O — host/browser/build and connected-target
persistence validated; deliberate Wi-Fi-loss-during-RUNNING remains pending.**

M14 deliberately chooses a local 4 MiB raw-flash circular log rather than NVS,
a filesystem, or cloud storage. It records only active sessions: immediate
START, END, and semantic changes, plus periodic complete snapshots every 60
seconds while RUNNING. CRC-checked commit-last pages/records reconstruct
after reboot, evict completed sessions oldest first, and expose degraded,
interrupted, and truncated state rather than hiding data loss.

One static low-priority core-0 `HistoryTask` owns storage. `ControlTask` only
publishes through a preallocated SPSC mailbox after its normal safety-gated
cycle; overflow drops and remains observable. History and OTA serialize flash
work with OTA priority and no control dependency. Monotonic session elapsed is
authoritative; synchronized UTC is optional.

Authenticated operational-STA GET APIs provide bounded newest-first summaries
and paged/strided samples. The embedded Romanian dashboard renders a responsive
bounded chart. Commissioning exposes no history and M14 adds no delete, export,
upload, cloud, recovery, or control command.

Definition of done:

- history format/reboot/torn-write/eviction behavior passes native and sanitizer
  tests;
- architecture guardrails prove bounded non-blocking publication and storage/
  OTA isolation;
- HTTP and real-browser AP/STA contracts pass;
- ESP-IDF 6.0.2 build, exact partition check, strict C++20 audit, and size gate
  pass;
- a signed full-serial installation on KFB003 confirms the new table, preserved
  NVS, START/sample/END persistence across reboot, HistoryTask core/stack, and
  local control continuity during history unavailability/Wi-Fi loss.

The connected-target gate validates internal flash and simulated data only. It
does not validate a sensor, SSR, thermal behavior, or independent electrical
safety.

## M15 — Personal Blynk remote access

Status: **Implemented in software — host/cross-build and Blynk Console
validated; KFB003 provisioning, live TLS/status/commands, reboot no-replay, and
firmware check plus remote-error e-mail delivery passed; native mobile/phone
push and remaining broker/outage scenarios pending.**

M15 provides private remote operation for one owner and one home smoker by
using the existing Blynk mobile application and Blynk Cloud's standard Device
MQTT API over TLS. It does not build a product-scale, multi-user cloud service.

The ESP32 connects outbound to the Blynk-provided regional endpoint. No inbound
home-network port, custom domain, custom backend, custom mobile application, or
cloud database is required. The Blynk device token is a non-versioned secret;
it must not appear in source, logs, snapshots, browser assets, release
artifacts, or documentation evidence.

M15 scope:

- use the official ESP-MQTT component and Blynk Device MQTT API over TLS;
- map Blynk controls to the existing external command set: Start, Stop,
  chamber/probe settings, alarm acknowledgement, resolved-fault clear, and a
  user-requested firmware check/install;
- return correlated semantic command acceptance/rejection instead of treating
  MQTT delivery as application success;
- publish one bounded `batch_ds` remote-status projection from immutable
  application/platform snapshots;
- configure Blynk events for faults, alarms, session completion, and OTA result;
- let the Blynk application provide the single user's dashboard, graphs, and
  notifications;
- reuse M13's fixed public GitHub
  `releases/latest/download/smoker_controller.bin` source, signed-image
  verification, permission interlock, rollback, and result reporting. Blynk
  carries only the check/install request and status; it never carries an
  arbitrary URL or firmware binary.

Remote-status publication is change-driven, not periodic:

- publish the current projection once after MQTT connect/reconnect;
- after that, publish only when at least one normalized, user-visible projected
  value changes;
- never publish status more often than once per five seconds;
- coalesce changes inside that window and publish only the newest complete
  projection when the window opens;
- if nothing changes for 30 seconds, 30 minutes, or longer, publish no duplicate
  status;
- use MQTT keepalive/Blynk connection state for online/offline rather than a
  telemetry heartbeat;
- command results and critical Blynk events are separate, rare messages and may
  be emitted immediately; they do not force an unchanged status publication.

Remote commands cross the existing bounded transport and are submitted only by
`ControlTask`. MQTT callbacks never call `SmokerApplication::submit()`, mutate
runtime state, write heater output, or wait on the critical loop. A reconnect
must not replay or synchronize a retained Start/OTA control value; a new user
gesture is required. Every command retains the same validation, safety gate,
and application semantics as its local equivalent.

M15 does not upload or backfill M14 raw history. Blynk datastream retention is
an auxiliary visualization cache, not the authoritative durable session log.
Loss of Blynk, Internet, MQTT, credentials, notifications, or quota cannot
change local session, timer, safety, or heater behavior.

Definition of done:

- host tests cover normalized-projection equality, first-connect publication,
  five-second throttling, coalescing, silence without change, and immediate
  event/command-result separation;
- host/concurrency tests cover command mapping, correlated results, no retained
  Start/OTA replay, bounded saturation, and control independence from a stalled
  or disconnected Blynk transport;
- architecture guardrails keep Blynk/MQTT out of `smoker_core` and prevent
  network callbacks from submitting directly or writing heater output;
- ESP-IDF 6.0.2 strict-C++20 build, dependency, stack, image-size, and task/core
  checks pass;
- the connected KFB003 publishes one initial snapshot, stays silent while the
  projection is unchanged, coalesces changed snapshots to at most one per five
  seconds, receives Start/Stop with semantic results, emits a phone
  notification, reconnects after Wi-Fi/Blynk loss without replaying Start, and
  requests a signed GitHub OTA through the existing M13 path while not running;
- all connected-target work uses simulated I/O until M6B/M7-M9 hardware exists,
  and therefore makes no sensor, SSR, thermal, or independent-safety claim.

## Future / undecided

Do not scaffold until explicitly requested:

- multi-stage recipes;
- Pause;
- fan/airflow control;
- integrated/external smoke-generator control;
- power monitoring;
- SSR stuck-ON detection;
- advanced outage-duration recovery policy;
- richer recipe automation;
- product-scale accounts, sharing, fleet management, and cloud synchronization.
