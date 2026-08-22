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
  assignment is SPI2/GPIO12 SCK/GPIO11 MOSI/GPIO13 MISO/GPIO10 CS. Physical
  modules/revisions, remaining PT100 and probe-frontend facts, address straps,
  connectors, probe GPIOs, and connected validation remain open. SSR, power,
  and independent-protection hardware are still blocked on exact parts.
- **M6B and M7-M10 — remaining controller product baseline:** incomplete. M7
  and M9 now have inactive host-tested/cross-buildable MAX31865 and dual-ADS1115
  software boundaries, but production remains simulated and physical activation
  stays gated by M6B. Product V0 cannot be called complete before real sensing/
  output, food probes, persistence, and recovery are implemented and validated
  at their appropriate levels.
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

Status: **In progress — MAX31865/three-wire PT100 and two ADS1115 converters
selected; physical modules, complete electrical frontends, and the remaining
external hardware are still incomplete.**

Inactive adapter software may be implemented when unknown physical values stay
mandatory configuration and production composition remains simulated. Do not
activate a real external adapter, initialize its concrete bus, or assign pins
until the actual component/interface is available and documented.

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

Status: **Software adapter and opt-in board diagnostic implemented but inactive
— host behavior and ESP-IDF 6.0.2 build/API compatibility are validated;
runtime activation and all connected-sensor evidence remain blocked on the
remaining M6B chamber facts.**

Eventually replace the simulated chamber source with the real hardware adapter.

Keep simulated adapter for development/testing.

Use the exact-pinned ESP Component Registry dependency
`esp-idf-lib/max31865` 1.0.8 rather than rewriting the register protocol. Its
70 ms `max31865_measure()` convenience call is forbidden inside the critical
cycle. The inactive backend provisionally configures continuous conversion and
distinguishes descriptor/configuration success from sample readiness. Its
host-tested monotonic policy returns absence and performs no fault/temperature
register read before the official maximum first-conversion interval: 55 ms for
60 Hz or 66 ms for 50 Hz. Every successful configuration, including recovery
after fault clear, resets this boundary.

Project-owned read code contains no explicit delay, task creation, heap
allocation, or `max31865_measure()` call. This source/host evidence does not
prove ESP-IDF/driver/SPI allocation behavior or bound real SPI worst-case
blocking. Bus ownership/timing and any additional module/input-network or bias
settling remain connected-hardware gates.

The confirmed RTD configuration is PT100 with three leads, corresponding to
driver nominal resistance `100.0F` and `MAX31865_3WIRE`. The fitted module
reference resistor remains an independent required value and must not be
inferred from the RTD type.

The project adapter maps every SPI, conversion, non-finite, and MAX31865 fault
to an absent authoritative measurement; existing safety then latches
`ChamberSensorInvalid` and commands heater OFF. No last-known-value fallback is
allowed.

The adapter requires explicit SPI host, CS GPIO, SPI clock, fitted reference
resistance, filter, and RTD standard. Production still composes
`SimulatedChamberSensor`; no bus or GPIO value is present in the production
composition. The final target assignment is nevertheless recorded once in
platform production code as SPI2/GPIO12 SCK/GPIO11 MOSI/GPIO13 MISO/GPIO10 CS,
matching the maintainer-reported soldered board. This is configuration intent,
not connected behavior.

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
overlay builds validate the intended ordering and isolation only; the
diagnostic has not been flashed or monitored, so physical shutdown is not
proved.

M7 remains incomplete until the chamber portion of M6B is recorded and the
physical sensor, wiring, conversion timing, accuracy, noise, open/short faults,
recovery, and sustained operation are validated on the target.

Requires the chamber-sensor/frontend portion of M6B.

## M8 — Real SSR heater output + PID control

Implement real platform heater driver.

Replace the M2 simulation's deterministic 100/0 control choice with an adapter
over Espressif's official `espressif/pid_ctrl` component. Add that Component
Registry dependency only when M8 implementation begins, pin its exact reviewed
version and lockfile hash, and keep it outside `smoker_core`.

The PID adapter runs synchronously inside the existing `ControlTask`; it must
not create a PID task, perform I/O, block, or allocate during computation. It
returns normalized `0..100%` heater demand, after which the existing synchronous
safety gate remains authoritative. SSR switching/window timing remains a
separate platform heater-output concern.

Select and validate the numeric backend, PID form, sample period, gains,
integral/output bounds, reset behavior, and SSR window using the identified
sensor, heater, smoker, and protection hardware. Do not invent tuning values
from the M2 simulation.

Do not bypass the approved safety gate.

Electrical work must respect independent hardware safety design.

Requires M7 plus the SSR/heater and independent-protection portions of M6B.

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

Status: **Software adapter implemented but inactive — host behavior and ESP-IDF
6.0.2 API compatibility are validated; modules, addresses, channel map, probe
frontend, runtime service placement, wiring, and connected evidence remain
blocked on M6B.**

Integrate actual probe frontend/protocol.

Food probes remain monitoring/alarm inputs only.

Use the exact-pinned ESP Component Registry dependency
`esp-idf-lib/ads111x` 1.1.14 rather than rewriting its I2C register protocol.
The upstream example demonstrates two devices on one bus, but its GND/VCC ADDR
straps are example wiring only. The project must document two distinct physical
addresses from `0x48..0x4b` before activation.

The inactive adapter stays in `smoker_platform` behind `IFoodProbeSource`,
without a separate sensor task. One acquisition owner stages explicit
mux/gain/rate configuration plus single-shot start, returns, and only on a
later service step checks deadline/readiness and obtains the same raw result.
`read(probe_id)` is I2C-free and returns an independently timestamped cached
temperature only before the required configured maximum age expires.

Raw codes require injected calibration/validity with no physical defaults.
All buses, pins, pull-ups, addresses, mappings, mux/gain/rate values, conversion
timeout, and sample age remain explicit configuration. Same-bus devices require
compatible settings and distinct addresses; genuinely separate buses may reuse
an address. The target backend overrides `ads111x_init_desc()`'s hard-coded
1 MHz descriptor clock before the first transaction.

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

M9 remains incomplete until the physical modules, bus/address/channel/front-end
facts and device-specific capacity are recorded, a service placement is proven
against the real ControlTask budget, and known-voltage, calibration, accuracy,
noise, disconnect/short, sustained-run, and heater-interference behavior are
validated on the target.

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
checks validate the board, radio, and simulated I/O only—not sensors, SSR,
electrical protection, or thermal safety.

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
