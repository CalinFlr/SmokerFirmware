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
- **M6B — external-hardware identification:** not started and blocked until the
  exact sensor, probe, SSR, power, and independent-protection hardware is
  available.
- **M6B and M7-M10 — remaining controller product baseline:** future. Product V0
  cannot be called complete before real sensing/output, food probes,
  persistence, and recovery are implemented and validated at their appropriate
  levels.
- **M11 — local display:** postponed because no display has been purchased.
- **M12 — Wi-Fi + local API/UI:** implemented for the simulated controller and
  host/cross-build validated; physical radio/provisioning/runtime validation on
  the final KFB003 board remains required before M12 is complete.
- **M13 — OTA + rollback:** complete for its defined scope. Signed USB
  migration, public GitHub OTA into the second slot, forced pending-image
  rollback, clean reinstall, five-cycle validation, and persistent reboot
  passed on KFB003.
- **M14-M15 — history/cloud capabilities:** future independent milestones;
  they are not implied by M12/M13 implementation.

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

Status: **Not started — blocked on exact external hardware availability.**

Do not implement a real external adapter until its actual component/interface
is available and documented.

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

Replace simulated chamber source with real hardware adapter.

Keep simulated adapter for development/testing.

Requires the chamber-sensor/frontend portion of M6B.

## M8 — Real SSR heater output

Implement real platform heater driver.

Do not bypass the approved safety gate.

Electrical work must respect independent hardware safety design.

Requires the SSR/heater and independent-protection portions of M6B.

## M9 — Real food probes

Integrate actual probe frontend/protocol.

Food probes remain monitoring/alarm inputs only.

Confirm the device-specific maximum configured probe count from the actual
frontend/hardware. Do not turn that capacity into a universal `smoker_core`
constant.

Requires the food-probe frontend/protocol portion of M6B.

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

Add history only after storage strategy is deliberately chosen.

Avoid high-frequency flash writes.

## M15 — Optional cloud/mobile integration

Cloud/mobile are auxiliary clients, not control dependencies.

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
- cloud synchronization.
