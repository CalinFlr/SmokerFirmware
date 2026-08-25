# Architecture Decision Log

Only accepted decisions are listed here.

## D001 — Native ESP-IDF

Use native ESP-IDF rather than PlatformIO.

Status: Accepted.

## D002 — Pin ESP-IDF baseline

Start from ESP-IDF `v6.0.2`.

Do not silently move to another IDF release.

Both direct CMake configuration and the verification entrypoint reject any
version other than exactly `6.0.2`.

Status: Accepted.

## D003 — Target ESP32-S3 family

The firmware targets ESP32-S3.

M6A records the final controller as the SuooTci `KFB003` ESP32-S3 board with
the user-reported N16R8 module variant. Carrier evidence comes from the user's
procurement identity and listing photographs; storage facts are independently
confirmed from target readback.

Status: Accepted.

## D004 — C++ application/domain code

Use strict C++20, without GNU language extensions, for project-owned host and
ESP-IDF C++ sources. Use ESP-IDF APIs for platform/system integration.

Status: Accepted.

## D005 — Thin composition root

`main/app_main.cpp` is a thin bootstrap/composition layer.

Status: Accepted.

## D006 — Three logical components

Use:

- `smoker_core`
- `smoker_app`
- `smoker_platform`

Status: Accepted.

## D007 — Pure core

`smoker_core` is platform-independent and has no ESP-IDF/hardware dependency.

Status: Accepted.

## D008 — Application owns hardware ports

Hardware/application ports belong to `smoker_app`.

`smoker_platform` provides concrete ESP-IDF/hardware implementations.

Status: Accepted.

## D009 — Single runtime-state writer

`SmokerApplication` is the only writer of mutable runtime state.

External actors use commands.

Status: Accepted.

## D010 — Snapshot-based reads

UI/network consumers use snapshots/events rather than mutable state references.

Status: Accepted.

## D011 — One critical V0 ControlTask

V0 uses one critical control task rather than independent PID/sensor/timer/safety tasks.

Status: Accepted.

## D012 — Synchronous safety gate

Safety is evaluated in the same critical control cycle before final heater output is written.

Status: Accepted.

## D013 — Host-first core tests

Primary domain/business tests run natively on the development machine.

ESP32 target tests cover integration/platform behavior.

Status: Accepted.

## D014 — No premature scaffolding

Future roadmap capabilities must not be implemented/scaffolded without an explicit milestone/task.

Status: Accepted.

## D015 — Food probes are monitoring/alarm inputs

Food probes never participate in chamber heater control.

They may trigger alarms/events/timer-start conditions.

A probe-threshold timer waits while the selected probe is disabled or has no
valid reading. A later enabled/reconnected sample may trigger it.

Status: Accepted.

## D016 — Single-stage V0

Recipe architecture is stage-oriented, but V0 implements exactly one stage.

Status: Accepted.

## D017 — Preheat/Hold are not domain states

Preheat, cook, keep-warm, etc. are stage configurations/names, not special V0 session states.

Status: Accepted.

## D018 — No separate food-safety engine

Recipes contain user-selected process parameters.

The controller does not certify food safety.

Status: Accepted.

## D019 — Configurable power-loss recovery

`resumeAfterPowerFailure` is a user-configurable device setting.

Status: Accepted.

## D020 — No fan/smoke-generator control yet

These are future/undecided capabilities and must not be implemented in V0.

Status: Accepted.

## D021 — OTA outside core

OTA belongs to application/platform layers and must not become a control-loop dependency.

Status: Accepted.

## D022 — No OTA install while running

Update installation/reboot is forbidden while a cooking session is running.

Status: Accepted.

## D023 — OTA rollback required

The eventual partition/update strategy must support rollback and new-image validation.

Status: Accepted.

## D024 — Hardware inventory deferred

Hardware facts are recorded only from available identified hardware. The
available controller board, including flash/PSRAM and pin restrictions, is M6A
work. External sensor/probe interfaces, SSR/power design, final external pin
assignments, and independent safety hardware remain deferred to M6B until those
components/design facts are available.

Device-specific probe capacity is decided at M9. Persisted-input bounds for
probe collections, names/recipes, and timer values are decided and enforced at
M10. No such values are invented in M5 or hard-coded in `smoker_core`.

Status: Accepted.

## D025 — Distinguish simulated slice from product V0

M0-M5 are the completed V0 simulated application/control slice. They do not
constitute a real-device or product-V0 completion claim.

The controller product baseline cannot be complete before the M6A/M6B and
M7-M10 hardware, real I/O, persistence, and recovery work.
Connectivity/display/OTA remain their separate later milestones.

Status: Accepted.

## D026 — M5 command queue has one owner and reserved Stop admission

`SmokerApplication::submit()` is single-owner/ControlTask-only in M5 and is not
thread-safe. The bounded queue reserves one slot for Stop, coalesces a
consecutive trailing Stop, and makes regular-command overflow observable.

Its Boolean return value reports queue admission/coalescing only. Semantic
validation and `CommandRejected` happen later in the owning control cycle.

Cross-task command transport is introduced only when an external producer is
implemented.

Status: Accepted.

## D027 — Probe defaults and session settings are separate

Device/default probe configuration remains immutable during a session. Live
probe commands mutate only active-session settings, and a new Start restores
the configured defaults.

Persistence of defaults and recovery of session settings remain M10 work.

Status: Accepted.

## D028 — Alarm acknowledgement and resolution are independent

Acknowledgement records user attention. Resolution records that the alarm is no
longer active because its condition/session lifecycle ended. Active snapshots
contain unresolved alarms, whether acknowledged or not.

Probe alarms are derived only while `RUNNING`; reconnect resolves disconnect,
and session termination resolves session alarms.

Status: Accepted.

## D029 — Raw input acquisition precedes derived side effects

The M5 cycle acquires raw inputs without emitting probe events/alarms, processes
commands, and only then derives probe state. This makes Stop, target changes,
and alarm/probe disables deterministic in the same cycle while retaining a
current chamber sample for safety/fault-clear decisions.

Status: Accepted.

## D030 — Duration and monotonic time point are distinct

Use `std::chrono::milliseconds` for `Duration` and a tagged
`std::chrono::time_point` for `MonotonicTimePoint`. They must not be aliases of
the same type.

Status: Accepted.

## D031 — Manual Stop is an observable control-cycle barrier

When a valid `StopSession` is processed, command draining ends for that tick.
Later queued commands retain FIFO order and become eligible on the next tick.
This guarantees a final safety-gated OFF write before any explicit subsequent
Start can return the controller to RUNNING.

Status: Accepted.

## D032 — P0 architecture constraints are executable

The M0-M5 layer graph, critical control ownership, V0 scope, and requirements
traceability are checked by dependency-free repository scripts and CI. These
structural checks complement behavior tests and cross-builds; they do not claim
runtime or electrical proof.

Status: Accepted.

## D033 — Stop coalescing preserves FIFO command intent

Only a Stop that is already the newest pending command may absorb another Stop.
Any intervening command ends the coalescing run, so a later Stop is admitted as
a distinct FIFO intent using the reserved slot.

This preserves both consecutive-Stop deduplication and the meaning of sequences
such as `Stop -> Start -> Stop`, where discarding the final Stop could otherwise
leave the controller RUNNING despite reporting that Stop as accepted.

Status: Accepted.

## D034 — Split controller-board and external-hardware identification

The original combined hardware-identification milestone is split into:

- M6A for the available ESP32-S3 controller board, its integrated capabilities,
  storage facts, and target-runtime validation;
- M6B for external chamber/probe frontends, SSR/heater interface, power design,
  and independent safety hardware that are not yet available.

M12 controller-board connectivity work and M13 partition work may depend on
M6A without claiming M6B completion. M7-M9 each require the corresponding M6B
interface facts before real hardware integration.

This split changes scheduling and dependency gates only. It does not identify a
board, invent hardware facts, or constitute target/electrical validation.

Status: Accepted.

## D035 — SuooTci KFB003 N16R8 is the final controller board

The final smoker controller uses the SuooTci ESP32-S3 board sold as code
`KFB003` / eMAG product `D1T7M22BM`, with the N16R8 storage variant. The
carrier's product photographs and silkscreen provide the header/USB/component
inventory, while Espressif module/SoC documentation governs electrical pin
restrictions.

The seller title's “34 pins” conflicts with photographs showing 44 header
positions, and one generic slide's 8 MB flash claim conflicts with the 16 MB
title and target readback. The project therefore records the photographed
silkscreen explicitly, relies on target readback for storage, and does not treat
inconsistent marketing artwork as a pinout. GPIO35-37 remain unavailable because
the target-confirmed R8 Octal PSRAM takes precedence over exposed carrier labels.

Status: Accepted.

## D036 — M12 uses bounded one-way cross-task transports

The single ESP HTTP task produces commands into a 16-entry SPSC mailbox; the
single `ControlTask` consumes them and remains the only production caller of
`SmokerApplication::submit()`. The transport preserves a Stop slot and FIFO
intent. Cross-core Stops are never coalesced because determining whether a
trailing Stop is still pending races with the consumer; each accepted Stop owns
an entry. Snapshot reads use a preallocated triple exchange with atomic read
leases; the critical publisher drops rather than waiting when no non-current
slot is free.

`ControlTask` remains priority 2 with a static 12 KiB stack and moves to core 1.
HTTP, Wi-Fi, TCP/IP, default events, and fallback timer work run on core 0.

Status: Accepted.

## D037 — M12 local credentials are a transitional tradeoff

This decision records the initial M12 implementation: an open SoftAP and the
shared device password `smoker257500`. D045 reinstates the fixed initial HTTP
password and D046 reinstates the open SoftAP. The remaining browser session shape is retained: a public, data-free form at
`/login` asks only for the device password and includes an accessible show/hide control. It creates
a random 256-bit `HttpOnly`, `SameSite=Lax` cookie whose server-side idle
timeout is 30 minutes; `Lax` permits the top-level CNA login redirect while
exact-Origin checks remain mandatory for protected mutations. Dashboard/data
routes require this session. HTTP Basic
with user `admin` remains supported only as the API-client credential shape. An
optional replacement is persisted with STA credentials, invalidates the web
session, and the UI warns while the initial password is active.

M12 has no TLS and NVS encryption is not enabled, so form/Basic credentials and
the bearer cookie remain visible to another client on the same LAN, while stored
credentials are plaintext at rest. No route returns secrets, no API bypasses
safety, and NVS errors never trigger an automatic partition erase.

Status: Credential behavior reinstated by D045 and D046; HTTP hardening remains accepted.

## D038 — M12 UI is embedded and polling-based

Use embedded vanilla Romanian HTML/CSS/JavaScript with Celsius values, no CDN,
npm runtime, WebSocket, or SSE. The browser self-schedules immutable snapshot
requests after the previous request completes, without replacing a focused or
dirty target input; keyed probe updates keep live readings moving while an
input is focused. Only the data-free login
page and its stylesheet and show/hide script are public; dashboard/data routes
are authenticated. Protected routes are same-origin, non-cacheable, and carry a
restrictive CSP and defensive headers. The login POST accepts captive-assistant
Origin quirks but still needs the correct device password. JSON bodies are
limited to 512 bytes and reject duplicate or unknown fields. Socket receive
timeouts have a finite retry budget and every body has a ten-second wall-clock
deadline, so a stalled or byte-dribbling client cannot occupy
the single HTTP task indefinitely.

All routes accept only the active AP address, current STA address, or device
`.local` hostname in `Host`. Exact-Origin checks therefore cannot be satisfied
by reflecting an attacker-controlled rebound Host.

Status: Accepted.

## D039 — M12 registry dependencies are limited and locked

Use `espressif/cjson` 1.7.19 for JSON and `espressif/mdns` 1.8.2 for
display-independent local discovery. Their hashes and the ESP-IDF 6.0.2
dependency are committed in `dependencies.lock`; generated
`managed_components/` remains ignored. M12 does not add OTA partitions or an
installer; it uses ESP-IDF's built-in 1500 KiB single-app layout until M13.

Status: Accepted.

## D040 — M12 adopts Fumuri visually without a web runtime

Use `/Users/floreacalin/Developer/Fumuri` as a read-only visual reference. The
embedded page transfers the canonical Jar, Jar profund, Salvie, Auriu, Hârtie,
and Cărbune tokens and the established light/dark surfaces, but implements only
real M12 capabilities in vanilla HTML/CSS/JavaScript with system fonts.

No React, Next.js, Tailwind, npm dependency, remote font, CDN, or fictional
cloud/Bluetooth/OTA/history/multi-stage/manual-heater feature enters firmware.

Status: Accepted.

## D041 — Wi-Fi discovery is asynchronous, bounded, and public-data-only

Authenticated same-origin `POST`/`GET /api/v1/network/scan` routes control an
asynchronous scan. Repeated starts coalesce. Hidden networks are omitted;
visible SSIDs are UTF-8/control-byte sanitized, deduplicated by strongest RSSI,
sorted strongest first, and capped at 20. Responses expose only SSID, RSSI,
channel, and security category.

Firmware-owned scan/result storage is fixed and preallocated. Scan suppresses
STA reconnect and resumes it after completion or a 15-second recovery timeout;
it never enters the application command transport or the control cycle. Scan
results explicitly mark only OPEN, WPA2, and WPA3 Personal as selectable.

Status: Accepted.

## D042 — Captive discovery is a fail-soft SoftAP-only service

SoftAP advertises `http://192.168.4.1/` through DHCP option 114. A responder
adapted from the ESP-IDF 6.0.2 captive-portal DNS example uses one static 4 KiB
task pinned to core 0 and binds only the SoftAP address. It is the sole auxiliary
network task allowed by the V0 guardrails and stops with the AP. Its static
task storage is not reused until the old worker has signalled exit, suspended,
and been deleted by the core-0 owner.

Unknown paths and registered unauthenticated page routes receive a non-empty
redirect to the absolute, data-free `http://192.168.4.1/login` form while AP is
active instead of retaining a probe host such as `captive.apple.com` or
returning an API-style JSON `401`. The login form itself is never served for a
foreign Host. STA-only redirects remain local instead of targeting the inactive
AP address. Because iOS CNA may
retain the probe Origin after that redirect, only the public password-verified
login POST accepts it; protected writes retain exact same-origin validation.
DNS/DHCP failure leaves manual `192.168.4.1` access available and cannot alter
control state, heater demand, timers, or safety.

The 30-second SoftAP fallback is anchored to the start of a disconnected
period; repeated STA authentication failures cannot postpone it. Wi-Fi mode
changes are serialized, fallback rechecks connectivity after acquiring that
serialization boundary, and a failed STA-only transition is retried until
`GOT_IP` converges to AP disabled. Runtime configuration errors remain logged
and preserve AP recovery instead of falsely claiming reconnection. `AP_STOP`
arms recovery when STA is still disconnected, transient fallback-enable errors
schedule a new bounded attempt, and scan completion rechecks configuration
inside the Wi-Fi-mode lock before restoring AP-only. Immediate STA connect
errors use an ESP timer retry; a STA startup error falls back to AP recovery and
fails connectivity startup only when that recovery also fails. Provisioning
marks its configuration transition before persistence, keeps the mode lock
through commit/application, and makes `GOT_IP` validate the currently
associated SSID. An event from a previous attempt therefore cannot close the AP
before the new attempt is installed.

STA reconnect retries track one in-flight driver admission. Accepted connects
cancel stale timers, ordinary disconnects reopen admission, and a credential
change lets its intentional disconnect event initiate the new connect, avoiding
overlapping driver calls.

Status: Accepted.

## D043 — M12 commissioning credentials are unique and rate-limited

Replace the shared HTTP default and open provisioning link with two generated,
persisted credentials: a WPA2 SoftAP password and an HTTP device password.
ESP-IDF's hardware random source generates 16-character credentials. First-boot
and legacy-default migration values are exposed through the physical serial
commissioning channel; the HTTP value is no longer logged after the device is
claimed. Changing the HTTP password does not silently change the SoftAP key.
Manufacturing may preload both values with the claimed marker to suppress
serial disclosure; label/injection operations remain outside this firmware
decision and are required before consumer shipment.

Login failures use a fixed four-peer IPv4 table. The fifth failure starts a
30-second block with bounded exponential backoff, a successful login clears the
peer, and stale failures reset. Cookie-authenticated state changes require an
explicit exact Origin. HTTP Basic clients may omit Origin, but cannot supply a
foreign one. HTTP remains plaintext and NVS remains unencrypted in M12, so this
does not claim protection against a hostile same-LAN client or flash access.

Status: Accepted in part; D045 supersedes generated HTTP credentials and D046
supersedes WPA2 SoftAP. Rate limiting and Origin policy remain accepted.

## D044 — M12 browser admission is correlated and resource growth is bounded

HTTP `202` is transport admission, not semantic success. A 32-bit correlation
ID crosses the SPSC mailbox and `SmokerApplication`, then a bounded immutable
snapshot result reports semantic acceptance or rejection. The UI waits for
that result. Mailbox sequencing uses 32-bit atomics so ESP32-S3 does not invoke
global-lock-backed 64-bit atomic load/store helpers in the ControlTask path.

Browser polling is completion-scheduled with a request timeout, probe DOM rows
are updated by identity, cJSON construction fails with `503` on any allocation
failure, and Wi-Fi scans have a 15-second recovery timeout. The build uses
ESP-IDF's built-in 1500 KiB single-app layout and rejects images above 75% of
that slot. This is a pre-OTA growth guard, not the M13 rollback layout.

Status: Accepted.

## D045 — The initial HTTP password is the fixed product default

Every new device and every device whose HTTP credential is still marked
initial uses `smoker257500` for the login form and HTTP Basic user `admin`.
At the time of this decision the WPA2 SoftAP key remained separate and unique;
D046 later supersedes that link-layer behavior. D045 does not weaken rate limiting, session cookies,
Host/Origin validation, strict request parsing, or protected-route access.

An owner-supplied HTTP password stored during provisioning marks the device
claimed and is preserved. Firmware migration replaces a generated credential
that is still marked initial with `smoker257500`, but never overwrites a claimed
custom password.

This shared, publicly documented default prioritizes simple onboarding. The
accepted consequence is that a nearby AP client or same-LAN client can
authenticate if the owner has not replaced it; HTTP and NVS also remain
unencrypted in M12. Reviewers must retain and report this product decision.
Changing the fixed initial value or returning to per-device HTTP generation
requires a new explicit product decision rather than an incidental security
remediation.

Status: Accepted; supersedes the generated-HTTP-password portion of D043.

## D046 — The commissioning SoftAP is intentionally open

`Smoker-<MAC6>` uses ESP-IDF `WIFI_AUTH_OPEN` and has no Wi-Fi password.
Firmware does not generate, persist, or disclose an AP key. The fixed HTTP
password `smoker257500` remains required before dashboard or API access; login
rate limiting, random session cookies, Host/Origin validation, strict parsing,
and control-safety isolation remain unchanged. STA provisioning still supports
the approved security types of the selected home network.

This decision prioritizes familiar low-friction IoT onboarding. The accepted
consequence is that anyone in radio range can associate with the AP, observe or
modify plaintext HTTP traffic, trigger captive endpoints, and attempt the
publicly documented shared login. HTTP authentication is not link encryption
and does not protect an associated client from traffic interception.

Reviewers must retain and report this product decision. Adding WPA2, generating
an AP key, or otherwise requiring a Wi-Fi password requires a new explicit
product decision rather than an incidental security remediation.

Status: Accepted; supersedes the WPA2 SoftAP portion of D043 and the remaining
link-layer supersession in D037.

## D047 — SoftAP is commissioning-only and LAN auth is password-to-session

The intentionally open `Smoker-<MAC6>` SoftAP is a commissioning surface only.
Requests accepted on local address `192.168.4.1` may load the public Wi-Fi setup
page, read provisioning status, scan visible networks, and save STA credentials.
They may never load login/dashboard assets, snapshots, cooking state, or
commands, even when a valid session cookie is supplied. The request's local
socket address determines this scope before Host validation and authentication;
an unknown local address is rejected fail-closed.

Operational access is available only through the current STA IPv4 or device
`.local` authority on a socket whose local address is the current STA address.
There are no users or roles. `POST /api/v1/auth/session` accepts only the device
password and replaces the single random 256-bit session token in an `HttpOnly`,
`SameSite=Lax`, `Path=/` cookie. The idle timeout remains 30 minutes. Logout and
device-password replacement invalidate the token; password replacement is a
separate authenticated operation requiring current and new passwords.

HTTP Basic and the `admin` identity are removed completely. Authorization
Basic/Bearer headers never grant access, unauthenticated APIs return JSON `401`
without `WWW-Authenticate`, and all authenticated/provisioning writes require
the exact device Origin. No token is exposed to JavaScript or stored in browser
storage.

STA configuration accepts exactly `ssid` and `wifi_password`, requires an
8..63-character password, marks only WPA2/WPA3 Personal scan results supported,
and configures the driver with WPA2 as the minimum authentication threshold.
STA OPEN, WEP, WPA1, and Enterprise are not supported. The SoftAP itself remains
open under D046; D047 changes its application scope, not its link security.

Captive DHCP/DNS and unknown AP paths canonicalize to
`http://192.168.4.1/`. On successful STA association the AP closes and the
commissioning page directs the owner to the `.local` hostname. Wi-Fi-loss
fallback may reopen the same setup-only AP while `ControlTask` continues
autonomously.

HTTP and NVS remain unencrypted in M12. WPA protects the supported STA radio
link but does not provide end-to-end HTTP encryption. This decision does not
claim physical radio, SSR, thermal, electrical, or independent-safety testing.

Status: Accepted; supersedes AP dashboard/login access in D037/D042/D046,
HTTP Basic/admin in D037/D043/D045, and STA OPEN support in D041.

## D048 — M12 credential groups use atomic versioned NVS blobs

Wi-Fi configuration (`ssid` plus `wifi_password`) and device authentication
(`device_password` plus the initial/claimed marker) remain separate persistence
concerns. Each concern is stored in one fixed-size, versioned NVS blob so an
individual update cannot expose a new field paired with a stale companion
field. Existing M12 per-field keys are read only for a one-time, non-erasing
boot migration; once present, the blob is authoritative.

The migration preserves existing STA and claimed-password state. An invalid or
unreadable authoritative blob fails connectivity initialization without erasing
NVS or falling back to potentially stale legacy fields. Local control continues
independently with heating initialized OFF.

The dual-stack HTTP listener also normalizes IPv4-mapped peers before applying
the accepted per-IPv4-peer login limiter. HTTP error envelopes are built in
fixed bounded storage so cJSON allocation failure does not immediately invoke a
second throwing allocation on the exception-disabled target.

Status: Accepted.

## D049 — M12 review boundaries fail closed before external effects

Operational HTTP is unavailable while the open commissioning SoftAP is marked
active, even when STA already has an IPv4 address. ESP-IDF's lwIP receive path
may accept a packet on one netif when its destination matches another configured
local netif; therefore the accepted socket's STA-local address is not sufficient
proof of protected-LAN ingress during APSTA overlap. Once `AP_STOP` confirms the
commissioning surface is inactive, normal STA operational access resumes.

Legacy authentication migration uses `smoker257500` only for a genuinely
missing or explicitly unclaimed state. Any NVS read error, corrupt legacy value,
invalid claim marker, or claimed marker without a valid password fails
connectivity initialization without writing an authoritative replacement blob.

The allocation-free command-admission JSON is completed before publishing a
command to the cross-task mailbox. No local JSON allocation/serialization
failure may return `503` after admission. Network delivery can still fail after
publication and remains reconciled through bounded command results where the
client has received the ID. Direct application Stop IDs that coalesce are
resolved only after the queued Stop is processed and all inherit its actual
semantic result.

These changes retain D045's fixed initial password, D046's open SoftAP, D047's
commissioning-only intent, and the existing safety gate.

Status: Accepted.

## D050 — M13 uses manual fixed-source HTTPS OTA with application-owned permission

M13 publishes version `0.13.0` and accepts firmware only from the fixed public
GitHub Releases asset
`https://github.com/CalinFlr/SmokerFirmware/releases/latest/download/smoker_controller.bin`.
The device performs no automatic checks, arbitrary-URL downloads, LAN uploads,
downgrades, or reinstalls. It uses ESP-IDF's HTTPS HTTP client and native OTA
write/verification APIs, certificate bundle, SNTP, application rollback, and
the M6A-confirmed 16 MiB flash. Automatic redirects are disabled: at most five
manually followed hops are accepted and every hop must retain TLS. The real ESP
image header supplies the chip ID used for pre-install admission. Check and
install have total monotonic deadlines of 30 seconds and five minutes;
application permission expires after ten seconds.

All M13 application images use ESP-IDF Secure Boot v2 RSA-3072 signature blocks
with `CONFIG_SECURE_SIGNED_APPS_NO_SECURE_BOOT` and update-time verification.
The currently running signed application supplies the trusted public-key
digest; therefore the first full-serial M13 image and every later OTA image must
use the same key. Ordinary builds are padded but unsigned and receive no
private material. The tag-restricted `firmware-release` environment supplies the
base64-encoded private key only to a dedicated signing step; that step verifies
the output against the repository public key before GitHub publication. The
private key is ignored locally and must have an encrypted backup. SHA-256 is a
corruption/transparency aid, not publisher authentication. Hardware Secure
Boot, eFuse provisioning, and flash encryption remain explicitly outside M13,
so physical/flash-write attackers can replace the software trust anchor.

Availability checks are read-only and may run during `RUNNING`. Installation
first obtains correlated permission through `PrepareFirmwareUpdateCommand` and
is allowed only in `IDLE`, `STOPPED`, or `FAULT`. While permission or first-boot
validation is active, `SmokerApplication` rejects Start. Completion or failure
uses the internal `FinishFirmwareUpdateCommand`; HTTP and `OtaTask` never call
`SmokerApplication::submit()` directly.

Prepare and Finish use dedicated bounded atomic signals to `ControlTask`, not
the HTTP command mailbox. Prepare is submitted first and retried if the
application FIFO is full; Finish is held until that submission succeeds. This
keeps a permission timeout from overtaking its reservation under queued Stop
traffic. Once admitted, the application FIFO preserves their order.

One static low-priority `OtaTask` on core 0 owns time synchronization, HTTPS,
flash writing, and rollback APIs and is not subscribed to TWDT. Its 16 KiB
stack and task control block reside explicitly in internal DRAM because PSRAM
is unavailable while the flash cache is disabled. A newly booted
`PENDING_VERIFY` image is marked valid only after ControlTask is subscribed to
TWDT and reports five consecutive safe cycles: `IDLE`, valid authoritative
chamber input, no fault, heater OFF, and successful TWDT reset. A fault,
ten-second timeout, mark-valid failure, runtime-context allocation failure, or
ControlTask/OtaTask creation failure rolls back and reboots. Network state does
not participate in that validation. Outside pending validation, failure to
create `OtaTask` leaves autonomous control running but makes firmware status
explicitly `FAILED` and rejects new check/install requests.

Firmware routes exist only on the authenticated STA surface. Browser writes
require the exact Origin. Safe authenticated GET accepts the browser's normal
absence of an `Origin` header, but rejects a supplied foreign Origin; Host and
socket-scope checks still apply before authentication. This preserves D047's
HTTP security boundary while making the firmware status GET usable by a
same-origin browser. Firmware-check admission returns a prebuilt fixed response
instead of allocating JSON after the operation has started.

The first M12-to-M13 deployment requires a complete serial flash because the
M12 single-app image cannot migrate its own partition table. Host tests and
cross-builds do not prove live GitHub download, both-slot boot, rollback,
sensors, SSR behavior, or electrical safety; those target scenarios remain
explicit evidence gates.

ESP-IDF still generates ordinary serial targets for the intentionally unsigned
developer build. M13 makes `flash`, `app-flash`, `bootloader-flash`,
`partition-table-flash`, and `otadata-flash` depend on an unconditional
fail-closed guard. This prevents a standard build from installing an unsigned
application or a partial new boot layout. USB installation uses only the
signed-image helper; `erase-flash` remains a separate, explicit recovery
operation.

Status: Accepted.

## D051 — Missing independent release review is conditional on single-maintainer access

The project currently has only one maintainer, so the `firmware-release`
environment has no independent reviewer. Public repository visibility or an
available reviewer feature does not manufacture independent approval.
Repository and tag-write access are currently limited to that maintainer, so
the absence of a second reviewer is accepted as a P3 operational hardening risk
rather than an M13 release blocker. This access fact is maintainer-reported and
must not be inferred or claimed from source-tree checks alone.

The residual risk is explicit: compromise of that maintainer's account, token,
or workstation could change tag-referenced code and invoke the signing job with
the environment secret. Tag filtering does not provide independent approval.
The maintainer must retain strong account MFA/passkeys, least-privilege and
short-lived credentials where available, deliberate tag creation, and the
encrypted offline signing-key backup required by D050.

This decision is valid only while no additional human or automation can write
repository content or create/move matching release tags. Before granting either
capability, or before the next release if that condition has changed, the
release boundary must be reopened and moved to an independent reviewer,
offline signer, or separately controlled KMS/workflow. Agents must report this
condition during future OTA/release security reviews; they must not silently
treat the current single-maintainer exception as a general design approval.

Status: Accepted with single-maintainer condition.

## D052 — Public canonical repository begins from one sanitized root commit

The canonical source and release repository is public so an uncredentialed
controller can download the fixed GitHub Release asset. A private repository
would return `404` to the device unless a GitHub access credential were stored
on it; M13 rejects that credential lifecycle and extraction risk. Repository
visibility controls access to bytes, not firmware authenticity. ESP-IDF's
RSA-3072 signed-update verification remains the publisher trust boundary.

The former repository is renamed and retained as a private evidence archive.
The public repository receives a sanitized working-tree snapshot through a new
Git repository containing exactly one root commit; historical objects, pull
request refs, real STA identifiers, unique device identifiers, and local port
identifiers are not pushed. Future public commits form the canonical history.

Ordinary commits run unsigned CI. Only an explicit `v*.*.*` tag matching
`version.txt` may enter the `firmware-release` environment, use its signing
secret, verify the result against the versioned public key, and publish the
signed binary and SHA-256. Public visibility must never turn every commit into
an automatically signed release.

Status: Accepted.

## D053 — M14 uses a bounded raw-flash circular session history

M14 assigns a 4 MiB `data, 0x40` partition at `0x620000` after the two existing
3 MiB OTA slots. It uses a project-owned versioned circular page/record format rather than
NVS, a filesystem, or an external/cloud service. NVS remains reserved for small
configuration values; a filesystem would add mount/recovery and dependency
surface without a current file requirement; cloud storage would violate local
availability and M15 scope.

Pages equal the confirmed 4 KiB erase sector. Page and record headers carry
magic, format version, CRC, generation/identity metadata, and a commit-last
marker; records do not cross pages. Startup reconstructs committed prefixes,
reports torn/corrupt tails as degraded, and lazily erases unclaimed media.
Completed sessions are evicted whole and oldest first, then interrupted
sessions. A commit-only tombstone in reserved page-header space makes the whole
victim logically absent before its first sector erase and is erased last;
startup treats intermediate NOR states of that marker as fail-closed and
completes the eviction before exposing history. A reset or erase failure
therefore cannot reconstruct only the surviving pages of an old session. If
one active session owns the full partition, only its oldest page is
reused and the retained summary is marked truncated. Capacity and usage are
reported; no retention duration is promised.

`ControlTask` publishes a complete post-safety snapshot through a preallocated
16-entry SPSC mailbox with 32-bit atomic sequences. Ordinary samples/changes
reserve lifecycle admission, drop rather than wait, and expose their drop
count. START and END are lifecycle records; periodic samples occur every 60
seconds while RUNNING; semantic changes publish on the first observed control
cycle. `ControlTask` performs no clock, mutex,
flash, logging, allocation, or HTTP work for history.

A session that both starts and reaches STOPPED/FAULT within one control cycle
is represented by an atomically admitted START+END mailbox pair. The auxiliary
consumer retries failed flash START and END records in FIFO order before
consuming later observations, so a transient missing START cannot orphan END or
block all subsequent histories.

One static 12 KiB low-priority `HistoryTask` on core 0, outside TWDT, owns flash
append/reconstruction and attaches credible Unix UTC when already synchronized.
Monotonic session elapsed remains authoritative. A platform flash coordinator
prevents raw-history and OTA writes from overlapping; OTA defers new history
work and its wait remains inside the existing total installation deadline.
History failure changes only history health and never submits a command, raises
an application fault, or changes heater/timer/safety behavior.

Two strict, authenticated, operational-STA-only GET APIs provide newest-first
session summaries and paged/strided records. History IDs are decimal JSON
strings. The embedded bounded chart has no external runtime. Commissioning
rejects all history access, and M14 adds no delete, erase, CSV, upload, or cloud
operation.

Changing the M13 partition table requires a complete signed serial installation;
an application-only OTA cannot introduce the partition table. This migration
preserves the existing NVS range and leaves `0x5e0000` bytes unallocated.

Status: Accepted.

## D054 — M15 uses Blynk as a personal MQTT relay and application

M15 targets one maintainer/owner controlling one home smoker. It uses Blynk's
existing mobile application and Device MQTT API over TLS rather than building a
custom mobile app, backend, domain, database, or multi-user product service.
The firmware will use the official ESP-MQTT component against the
Blynk-provided regional endpoint. The Blynk account remains the phone-side
authentication boundary; the per-device token is a non-versioned platform
secret and is never committed or exposed through firmware diagnostics.

This chooses Blynk over Telegram because the required daily experience is a
live smoker dashboard with controls, datastreams, graphs, and push events, not
a chat-only command surface. It chooses Blynk over a generic free MQTT broker
or Cloudflare relay because those alternatives still require a custom phone UI
and/or push/backend code. The choice is intentionally replaceable: Blynk exists
only in `smoker_platform` and consumes the same immutable snapshots and bounded
command mailbox as other network adapters.

Remote status uses one bounded `batch_ds` projection. It publishes once after
connect/reconnect and thereafter only when the normalized user-visible
projection changes. Successful status publications are separated by at least
five seconds; changes inside that interval coalesce to the newest complete
projection. There is no periodic duplicate status or telemetry heartbeat.
MQTT keepalive and Blynk presence provide connectivity indication. Correlated
command results and configured fault/alarm/session/OTA events are separate,
rare messages and may publish immediately.

Blynk controls are live gestures, not cloud-owned desired state. MQTT callbacks
may translate an allowlisted control into the existing bounded transport, but
only `ControlTask` calls `SmokerApplication::submit()`. The adapter must not
sync or replay Start or OTA-install datastream values after reconnect, and
Blynk delivery is never reported as semantic acceptance. No Blynk operation
can write heater output or bypass safety.

The firmware-update control carries only a check/install request. M13 remains
the sole OTA implementation and downloads the fixed public GitHub
`releases/latest/download/smoker_controller.bin` asset directly, verifies the
ESP32-S3 signed image, obtains application permission, rejects installation
while `RUNNING`, and retains rollback/first-boot validation. No Blynk firmware
storage or Blynk.Air packaging is required.

Blynk datastream retention may support auxiliary graphs, but M14 raw history
remains the authoritative local session log and is not uploaded or backfilled.
Free-plan quotas are handled by bounded change-driven publication, not by
weakening local control or safety when quota or service is unavailable.

Status: Accepted.

## D055 — M8 uses Espressif's official PID component behind a platform adapter

M8 uses the official ESP Component Registry component `espressif/pid_ctrl`
exactly at reviewed version 0.3.1, component hash
`974be0666bb4d95f49677327dd8305781d04d8bae284fdde2fbadf06ca9d4979`.
It declares ESP-IDF `>=4.4`, covering the pinned ESP-IDF 6.0.2 baseline, and is
maintained by Espressif rather than bundled in that IDF source tree. Its
mandatory transitive `espressif/iqmath` 1.11.0~1 component is locked at
`39448db759b410373e543798167ca4670bbff3019cb290a2fe8e627221e71b9d`.
An unreviewed floating upgrade is forbidden.

Reviewed 0.3.1 provides float and IQmath numeric backends and both positional
and incremental calculation forms. The first inactive slice selects the float
API because the existing `Temperature` and `HeaterDemand` boundary is float and
ESP-IDF 6.0.2 identifies ESP32-S3 as having a single-precision hardware FPU.
This is a numeric-interface selection, not approval of a calculation form or
physical tuning. Form, gains, common output limits, positional accumulated-error
bounds, call cadence, and SSR window remain explicit and require real
M6B/M7/M8 evidence.

Reviewed positional form accumulates raw per-call error, clamps that accumulator
to `min_integral`/`max_integral`, multiplies it by Ki, and differentiates error.
Reviewed incremental form never reads those accumulator/bound fields; it adds
per-call error changes to `last_output` and clamps the retained output instead.
Project configuration therefore requires finite ordered accumulated-error
bounds containing zero only for positional form and rejects them for incremental
form. The target adapter maps the upstream incremental-only ignored fields to
`0/0` deterministically.

The API takes an error per call and has no sample-period/delta-time parameter,
so both forms use implicit per-call gains. Both differentiate error, and the
component provides neither derivative filtering nor derivative-on-measurement.
Because the project passes target minus measured, a target step can create
derivative kick; form selection and mitigation require real activation/tuning
evidence. It also provides neither autotuning nor plant identification.
`pid_ctrl` is the runtime engine; automatic tuning is a separate future decision
which cannot be selected or tested safely before the real chamber sensor,
SSR/heater, smoker thermal plant, and independent cutoff are available and
validated. No form, coefficient, or simulated tuning result is production
approved.

Because the component includes ESP-IDF types, its real backend remains
target-only in `smoker_platform` behind application-owned `IChamberController`.
The host-testable adapter fixes error direction as target minus measured and
rejects backend failure, non-finite output, and output outside configured
normalized 0..100% bounds. `smoker_core` stays platform-independent, and
`SmokerApplication`/`ControlTask` remains the only caller and runtime-state
writer. Production explicitly composes a deterministic adapter around the
existing M2 100/0 controller; the PID adapter is compiled but not composed.

The target float backend is non-copyable RAII and exercises the exact
`pid_new_control_block_f()`, `pid_compute_f()`,
`pid_reset_ctrl_block_f()`, and `pid_del_control_block_f()` APIs. Reviewed
creation uses `calloc()` and is restricted to initialization before the
critical task starts. Valid upstream compute/reset and project request/reset
paths intentionally allocate nothing and perform no I/O, delay, wait, logging,
locking, or task creation.

The synchronous safety evaluation and gate occur after requested-demand
computation and may always replace it with OFF before the sole final heater
write. Application construction issues observable heater OFF before its first
controller reset, without claiming to replace safe initialization in a future
real output driver. Boot, IDLE, STOPPED, manual Stop, an effectively missing
target, invalid authoritative measurement, active fault, and firmware-update
interlocks leave heating OFF and reset/disable latent controller state. Reset
is a bounded critical-path operation with the same no-I/O/wait/task/allocation
contract as request. Application-owned reset failures are reported;
`PidChamberController` has no ignored destructor reset, while its target backend
still releases the owned control block through RAII. Compute/reset failure fails
closed and latches `ControlLoopFailure`. A later successful reset only makes
explicit fault clear possible; clear leaves the session stopped, so a new
explicit Start is required. Electrical SSR output/windowing stays separate and
is absent from this slice.

The ordinary command batch uses final-state semantics except where a rule
defines a barrier. Target removal followed by restoration in one tick therefore
does not create an intermediate OFF/reset cycle: RR-003 governs an absent target
at control evaluation, while SR-003/D031 explicitly reserve that boundary for
an accepted manual Stop.

Status: Accepted.

## D056 — M7 imports the registry MAX31865 driver before physical activation

The authoritative chamber frontend will use MAX31865. ESP-IDF 6.0.2 provides
the required SPI master but does not bundle a MAX31865 device driver. The
project therefore uses ESP Component Registry component
`esp-idf-lib/max31865` exactly at version 1.0.8, with registry component hash
`c7a027843a3f9cf4b06e7e216b25b2089115568f288c8682defd84c018a5b80f`.
The BSD-3 component explicitly targets ESP32-S3, and that release contains the
upstream ESP-IDF 6 driver-component compatibility change. This is preferred to
copying register logic into the project or depending directly on an unversioned
Git repository.

Importing, wrapping, and cross-building the driver is M7 software integration,
not a completed M6B physical record or a real-sensor implementation claim. The
selected sensor is a three-wire PT100, fixing `rtd_nominal = 100.0F` and
`MAX31865_3WIRE` for the then-inactive adapter. At this decision point,
production continued to compose `SimulatedChamberSensor` pending connected
evidence. The
maintainer confirmed on 2026-08-22 that the final soldered production assignment
is SPI2 with GPIO12 SCK, GPIO11 MOSI, GPIO13 MISO, and GPIO10 CS. Target code
records that mapping once and the opt-in diagnostic consumes it, but neither
the mapping nor a build activates the real chamber source or proves continuity,
power, response, or electrical validity.

The implementation remains a `smoker_platform` adapter behind the existing
application-owned `IChamberSensor` port. A platform-neutral seam is host tested;
the target-only RAII backend acquires/configures/releases the real descriptor
with `max31865_init_desc()`, `max31865_set_config()`, and
`max31865_free_desc()`, and reads through `max31865_get_fault_status()` plus
`max31865_read_temperature()`. A detected fault uses
`max31865_clear_fault_status()` and reconfiguration only to permit a fresh
later-cycle read; it remains absent for the current cycle. It creates no task
and exposes no ESP-IDF or driver types to `smoker_app` or `smoker_core`. The
component's `max31865_measure()` convenience function waits 70 ms and is
forbidden from the critical cycle.

Descriptor/configuration success is not sample readiness. The RTD data-register
POR value is zero, and driver 1.0.8 converts raw zero to a finite value near
-242.02 C. A host-tested monotonic policy therefore blocks fault/temperature
register reads until the official maximum first-conversion interval has elapsed
after every successful automatic configuration: 55 ms with the 60 Hz notch or
66 ms with the 50 Hz notch. It returns explicit `NotReady`, which the chamber
adapter maps to absence. Fault clear/reconfiguration invalidates freshness and
restarts the same boundary; no previous reading is reused.

Continuous conversion with bias was the provisional inactive strategy.
Project-owned read code contains no explicit delay, task creation, heap
allocation, or `max31865_measure()` call. Host allocation observation and source
inspection do not prove allocation behavior inside ESP-IDF/driver/SPI code or a
real worst-case SPI blocking time. Filter, RTD standard, reference resistance,
and SPI clock have no fabricated defaults. The target board host/pins are fixed
by the confirmed soldered assignment, while bus ownership and timing,
module/input-network and bias settling, fault recovery, accuracy, continuity,
and physical validity remain hardware-pending and may require a later decision.

The dormant connected-board diagnostic is an explicit Kconfig opt-in which is
OFF by default and compile-time exclusive from the ordinary application/runtime
composition. It uses a bounded datasheet-mode-1 register-response check, rejects
MISO which follows internal pulls, compares only persistent defined
configuration fields, calls no temperature API without fitted Rref, takes ten
raw/fault samples, and distinguishes RTD fault observations from SPI and
shutdown failures.

The MAX31865 configuration register's D7/D6/D4/D0 fields are persistent while
D5 (1-shot), D3:D2 (fault-cycle control), and D1 (fault clear) are commands or
self-clearing state. Driver 1.0.8's `max31865_set_config()` read-modify-write
clears only the persistent fields before setting their requested values, so it
can carry command bits read from the device into a later write. The diagnostic
does not call the driver's unbounded `max31865_detect_fault_auto()`. Its cleanup
instead writes the exact command-zero terminal byte `0x11` and requires exact
readback: AUTO off, VBIAS off, no 1-shot/fault-cycle/fault-clear command,
three-wire, and 50 Hz. It first exits AUTO without changing the current filter,
then changes to 50 Hz only while normally off when necessary.

The software-SPI stage verifies that quiescent terminal state before releasing
its pins. Its fallback restores idle SCLK and a CS-high frame boundary before
attempting shutdown after a partial transaction. The descriptor owner provides
checked/idempotent normal shutdown and a bounded destructor fallback on every
early return; descriptor removal remains
attempted after quiescence failure and precedes bus release. Failure of checked
normal shutdown fails the diagnostic. These source/build properties are a
buildable procedure, not evidence that a connected converter actually accepted
the shutdown.

Evidence chronology, without rewriting this historical import decision: the first separately
authorized connected run on 2026-08-24 read configuration `0xff` with the
software-SPI MISO pull-up and `0x00` with its pull-down. It therefore failed at
floating-MISO discrimination before complementary configuration writes, driver
initialization, raw/fault sampling, or either exact terminal `0x11` readback.
The software fallback requested `0x10` but observed `0x00`, so physical
quiescence was not verified in that run. The ordinary signed simulated firmware
was restored and verified immediately afterward. A later corrected connected
setup produced pull-independent `0x11` reads, exact software patterns
`0x00`/`0x91`/`0xD1`, ten stable raw samples with no transaction or sensor
fault, and exact software and driver terminal `0x11` readbacks. D059 records
the subsequent ordinary-runtime activation decision.

Every SPI/conversion error, MAX31865 fault, non-finite value, or value rejected
by the documented M7 validity policy becomes an absent authoritative
measurement. The application must not reuse the last valid value: existing
synchronous safety latches `ChamberSensorInvalid` and commands heater OFF.

Status: Accepted.

## D057 — M9 imports the registry ADS1115 driver before physical activation

The external analog/probe acquisition path will use two ADS1115 converters.
ESP-IDF 6.0.2 provides the I2C master but does not bundle an ADS1115 device
driver. The project therefore uses ESP Component Registry component
`esp-idf-lib/ads111x` exactly at version 1.1.14, with registry component hash
`fd18497adfb7210d750188986bc7cebc048db36abb64fdbe7216d4536083c4a2` and
upstream release commit `9eb6f607662518f1bdd3a3b88629db720b765b8e`. The
BSD-3 component explicitly targets ESP32-S3 and its versioned example
demonstrates two ADS1115 descriptors on one bus. This is preferred to copying
register logic into the project or depending directly on an unversioned Git
repository.

The component's wildcard support dependencies are made reproducible by the
versioned lockfile: `esp-idf-lib/i2cdev` 2.1.2 at hash
`ad8981cc64533dcaced5107d72e42bcebe79345e194e82795792af531b300ce3` and
`esp-idf-lib/esp_idf_lib_helpers` 1.4.0 at hash
`689853bb8993434f9556af0f2816e808bf77b5d22100144b21f3519993daf237`.
The selected `i2cdev` release detects ESP-IDF 6.0 and compiles against its new
`i2c_master` driver.

Importing, wrapping, and cross-building these components is M9 software
integration, not a completed probe frontend or connected-hardware claim. Two
devices on a shared bus require two distinct physical ADDR selections from
`0x48..0x4b`; the upstream example's GND/VCC straps are not project
assignments. Production continues to compose `SimulatedFoodProbeSource` until
the exact module revisions, supplies, address straps, pull-ups, channel
purposes, analog conditioning, probe curves, calibration, I2C port, and GPIOs
are documented at M6B.

The inactive implementation is one `smoker_platform` acquisition owner behind
the existing `IFoodProbeSource` port. It owns both device/channel state and
uses one round robin plus explicit synchronization/quarantine state per ADC.
Both devices begin unsynchronized even after successful backend initialization,
because `ads111x_set_mode()` cannot prove that an externally powered ADC is
idle after an MCU-only reset. First use and quarantine recovery require one
successful `busy=false` observation which discards any old result and performs
no configure/start in that service step. Busy/error devices are skipped so a
healthy ADC continues to progress, including across consecutive logical
channels mapped to the quarantined device.

After explicit single-shot mux/gain/rate configuration and a successful start,
the deadline is calculated and a later service step observes busy before
evaluating that deadline. Ready is accepted at or after the boundary because
polling time cannot reveal completion time. Still busy at/after the boundary,
a busy-read error, or a failed start quarantines the physical device and the
abandoned result is never read. A successful ready observation followed by a
value-read failure leaves the known-idle device reusable; calibration/validity
and configuration failures remain probe-local. Pinned 1.1.14
`write_conf_bits()` clears OS for mux/gain/rate writes, so a failed
configuration transaction on a synchronized idle ADC cannot itself start a
conversion, even if it partially changes configuration. The adapter creates no
task, contains no project-owned wait/poll loop, and does not expose ESP-IDF/
driver types to `smoker_app` or `smoker_core`. `read(probe_id)` performs no I2C
work; it returns only an independently timestamped cache entry before a
required configured maximum age expires.

Raw ADC codes have no physical interpretation in the adapter. A mandatory
injected calibration/validity policy must produce `Temperature`; there is no
probe curve, divider, voltage range, temperature range, or calibration default.
I2C port, SDA/SCL, clock, internal/external pull-up policy, address, device/
channel/probe map, mux, gain, data rate, conversion timeout, and sample age are
all explicit required configuration. Same-port devices require compatible bus
configuration and distinct addresses; separate non-overlapping buses may reuse
an address.

The target-only RAII backend calls the real init/free, mode, mux, gain, rate,
start, busy, and value APIs. `ads111x_init_desc()` writes 1 MHz into its public
descriptor and creates a mutex; project configuration replaces that clock and
sets the explicit pull-up policy before the first I2C transaction. Production
does not call `i2cdev_init()` or instantiate the backend.

TI specifies conversion time as `1 / DR` with +/-10% data-rate variation and
approximately 25 us single-shot power-up. The monotonic stuck deadline remains
mandatory configuration and must exceed the documented worst conversion
period because the approximate power-up, service cadence, and physical
frontend margin have no established maximum. Locked `i2cdev` may wait up to
`CONFIG_I2CDEV_TIMEOUT`, lazily create bus/device state, retry with internal
task delays, and allocate during initialization or error recovery. Host/API
cross-build evidence therefore does not establish bounded real target latency,
allocation freedom, or suitability for ControlTask.

Failures invalidate only the attempted or active food-probe cache; ambiguous
start/busy failures additionally quarantine only their physical ADC. Per
BR-005 and SF-008, food probes remain monitoring/alarm inputs: no ADS1115 value or failure directly changes the authoritative chamber control, fault policy,
or heater demand.

Status: Accepted.

## D058 — M15 pins ESP-MQTT and provisions Blynk through UART0/NVS

M15 uses the official ESP Component Registry dependency `espressif/mqtt`
exactly at version `1.0.0`, locked at component hash
`ffdad5659706b4dc14bc63f8eb73ef765efa015bf7e9adf71c813d52a2dc9342`.
ESP-IDF does not bundle this external component at the project-selected API
version, and the built-in TLS/CA-bundle facilities remain sufficient; no Blynk
Library, Edgent, Blynk.Air, Arduino layer, backend, or additional MQTT library
is introduced.

The client connects directly to a validated regional `*.blynk.cloud` endpoint
on port 8883 using MQTT 3.1.1, the ESP certificate bundle, username `device`,
token-as-password, clean session, 45-second keepalive, ten-second reconnect,
QoS 0, and no retain. Its only subscription is `downlink/ds/#`. It never uses
`get/ds`, saved-value sync/replay, or `downlink/ota`.

The token is not a build or release secret. A bounded `FUMURI-BLYNK/1` protocol
over the confirmed KFB003 USB-to-UART0 link performs `set`, redacted `status`,
and `clear`. Firmware stores endpoint, Template ID, and token in a versioned,
CRC-protected fixed blob under a dedicated NVS namespace. Missing or invalid
data disables only Blynk and a successful update restarts its MQTT client.
M15 explicitly accepts that unencrypted NVS can be extracted with physical
access; flash/NVS encryption and eFuse provisioning remain future work.

The MQTT callback only copies allowlisted bounded input into a raw SPSC
mailbox. A static low-priority core-0 `BlynkTask` parses it and produces into a
second SPSC mailbox. `ControlTask` alternates that mailbox with HTTP under a
global budget which preserves two regular application slots for OTA intents
and stops at the first Stop. One atomic generator supplies nonzero HTTP/Blynk
session and correlation IDs while skipping the internal OTA reservation.

Status, command results, and events remain separate bounded streams. The
15-field complete status projection is change-driven and five-second
throttled; correlated results are emitted only for IDs tracked as Blynk
commands, and the five event types coalesce per type over the same minimum
interval. Disconnect drops unpublished result/event state and never replays a
Start or Install gesture.

Disconnect/error occurrence is tracked separately from the final MQTT
connected state, and every successful connection receives a nonzero generation.
Both bounded Blynk command stages carry that generation. `BlynkTask` must apply
disconnect cleanup before activating a newer generation, and `ControlTask`
must discard a translated Blynk command whose generation is no longer current.
Cleanup resets the pending Start parameter, results, already-selected feedback,
events, and the callback-ordered inbound-drop watermark; it does not reject a
new command received for the live reconnect generation.

Status: Accepted.

## D059 — M7 activates MAX31865 as the ordinary authoritative chamber source

The ordinary firmware composes `Max31865ChamberSensor` as its sole
authoritative chamber input after the corrected connected diagnostic produced
pull-independent SPI configuration reads, exact software pattern readbacks,
exact active `0xD1`, ten stable raw observations without transaction or sensor
faults, and exact software and pinned-driver terminal `0x11` shutdown
readbacks. The earlier floating-MISO failure remains recorded; it is not
silently reclassified as success.

Production centralizes SPI2/GPIO12 SCK/GPIO11 MOSI/GPIO13 MISO/GPIO10 CS,
100 kHz, PT100 nominal 100 ohm, three-wire, 50 Hz, ITS-90, provisional
430.0-ohm Rref, active `0xD1`, terminal `0x11`, and the 66 ms first-conversion
boundary. It explicitly applies the supplier-documented inclusive -50.0..+200.0
C assembled-probe range as sensor-specific operational validity; both finite
bounds are mandatory and strictly ordered. The pins, PT100/three-wire selection,
and range come from maintainer/probe documentation; SPI/configuration/raw/
shutdown facts are connected T-pass; Rref and ITS-90 are provisional operational
choices. The range is not measured calibration, and conversion corroboration is
not a fitted-Rref measurement, calibration, or accuracy claim.

Target startup owns and initializes the SPI bus, then establishes a checked
GPIO13 internal pull-up before acquiring the driver descriptor. The backend
rejects descriptor initialization unless that owner proves it owns the
initialized SPI2 bus. It writes and verifies the full exact active byte, waits past the
first-conversion boundary using the same real ESP monotonic clock supplied to
`SmokerApplication`, and creates the sole `ControlTask` only afterward. Exact
register writes replace driver 1.0.8's configuration/fault-clear
read-modify-write helpers. Fault recovery invalidates freshness, issues an
exact clear command, verifies a fresh active configuration, and never returns
the faulting or cached sample. Shutdown verifies exact terminal `0x11`, removes
the descriptor, then releases the SPI bus and restores GPIO13 to floating.
Disconnected/high-impedance MISO consequently resolves toward `0xff`, producing
configuration mismatch or nonzero fault data. Exact configuration readback and
the validity policy reject stuck-low/raw-zero paths, including the driver's
finite approximately -242.02 C raw-zero conversion.

Food probes remain simulated, heater output remains simulated, the
deterministic chamber controller remains active, and the real PID adapter stays
uncomposed. Safety still evaluates synchronously before the sole simulated
heater write. Driver error, converter fault, non-finite temperature, or policy
rejection produces an absent authoritative measurement, latches
`ChamberSensorInvalid`, and commands OFF; no later sample
automatically resumes a latched session. No SSR/GPIO heater path or additional
control/sensor/safety task is introduced.

SPI-bus/pull, descriptor/configuration, and first-boundary failures are chamber-
hardware failures rather than critical application-construction failures. They
leave the sensor unavailable until reboot while ordinary `ControlTask` and the
normal observation/connectivity services start. The first IDLE tick publishes
no chamber value, latches `ChamberSensorInvalid`, exposes `FAULT`, and retains
OFF. A pending image then rolls back through the normal published-fault policy;
five safe cycles are still required to mark an actual OTA-installed
`PENDING_VERIFY` image valid. Runtime-context allocation and `ControlTask`
creation failures remain immediate pending-image rollback conditions because
the application runtime cannot exist.

The default-OFF connected diagnostic remains isolated and behaviorally
unchanged. A 2026-08-25 full-serial installation of the 1,445,888-byte signed
ordinary application, SHA-256
`4b1541202eaa7388b79c48f9f615fd7443959b5ad943f12652e3cfa4ea95ffcc`,
then observed this exact composition at cycles 1, 60, and 180. Chamber readings
were 25.7, 25.7, and 25.8 C over approximately 179 seconds, with no target and
simulated heater 0.0%. Those three readings satisfy the intended at-least-120-
second functional observation without requiring exact cycle 120. No
MAX31865/SPI/configuration/chamber-sensor fault, watchdog, rollback,
unexpected reset, or diagnostic failure appeared. The operator initiated no
provisioning, OTA, or network command; automatic saved-Wi-Fi connection and
configured Blynk attempts remained auxiliary to `ControlTask` and MAX31865
validation.

The serial helper installed blank all-`0xff` OTA metadata in the no-factory
layout. Reviewed ESP-IDF 6.0.2 therefore selected `ota_0` directly as
`ESP_OTA_IMG_VALID`; only an already selected `ESP_OTA_IMG_NEW` entry transitions
to `ESP_OTA_IMG_PENDING_VERIFY`. The pending/five-cycle acceptance criterion is
waived only for this serial activation, and no pending state is created or
forced. OTA-005 remains intact for actual OTA-installed pending images; a
future sensor-faulting pending-image target test is separate from M7 completion.

M7 is complete for its defined software integration and connected ordinary-
runtime functional activation. The 179-second observation is not sustained-
duration qualification and does not demonstrate physical temperature
regulation because heater/SSR and production PID remain inactive. Physical
module identity, fitted Rref/tolerance, continuity and shield termination,
calibrated accuracy, controlled open/short behavior and recovery, longer-
duration behavior, response/noise, heater interference, and independent
electrical/thermal safety remain M6B/pre-real-heater and release gates. They do
not block beginning ADS1115 integration.

Status: Accepted.
