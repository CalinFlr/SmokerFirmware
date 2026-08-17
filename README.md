# Smoker Controller

Native ESP-IDF firmware for a DIY smart smoker controller based on ESP32-S3.

## Baseline

- MCU family: ESP32-S3
- Framework: native ESP-IDF
- ESP-IDF baseline: `v6.0.2`
- Project C++ baseline: strict C++20 on host and ESP-IDF target
- Local-first operation: cooking/control must not depend on internet, phone, browser, or cloud
- V0 priority: get a small, testable controller working before adding hardware integrations or advanced features

ESP-IDF stable documentation currently identifies `v6.0.2` as the stable line.

Official references:

- ESP-IDF stable docs: https://docs.espressif.com/projects/esp-idf/en/stable/
- ESP32-S3 docs: https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/
- Codex best practices: https://developers.openai.com/codex/learn/best-practices
- Codex `AGENTS.md`: https://developers.openai.com/codex/agent-configuration/agents-md
- Codex execution plans: https://developers.openai.com/cookbook/articles/codex_exec_plans

## Current implementation status

M0-M5 complete the **V0 simulated application/control slice**. M12 adds local
Wi-Fi/API/UI and M13 adds manual HTTPS OTA with rollback-capable partitions.
Radio and OTA target scenarios remain pending, so these are scoped software
milestones, not a claim that product V0 is complete.
M6A is complete for the final SuooTci `KFB003` ESP32-S3 N16R8 controller:
carrier inventory, 16 MiB Quad SPI flash, 8 MiB Octal SPI PSRAM, native USB,
application runtime, stack use, and task-watchdog reset behavior are recorded
with product, photograph, and target evidence.
[`docs/HARDWARE.md`](docs/HARDWARE.md) is the canonical, evidence-classified
component inventory; every future sensor, probe frontend, output, power, and
independent-safety component must be recorded there before integration.
The firmware now provides:

- a deterministic simulated chamber-to-heater control cycle;
- Start/Stop with a recipe snapshot and exactly one stage;
- optional monotonic timers with immediate, chamber-threshold, or selected
  food-probe threshold start conditions;
- synchronous safety override, configured maximum-temperature enforcement,
  and latched chamber faults;
- simulated 1..N food probes, alarms, events, commands, and immutable snapshots;
- a single critical ESP-IDF `ControlTask`, with a statically provisioned 12 KiB
  stack, stack high-water-mark instrumentation, and task-watchdog integration;
- deterministic same-cycle command/alarm behavior, reserved Stop admission,
  consecutive-only Stop coalescing, and observable regular-command overflow;
- an observable safety-gated OFF cycle after every accepted manual Stop, even
  when a Start is already queued behind it;
- separate immutable probe defaults and mutable active-session settings;
- explicit-only Start after every boot, initially `IDLE` with simulated heater
  OFF;
- STA provisioning with delayed `Smoker-<MAC6>` SoftAP fallback, mDNS, an
  authenticated JSON API, and responsive Romanian dashboard;
- a core-separated SPSC command mailbox and preallocated triple snapshot
  exchange that keep HTTP/Wi-Fi out of the critical loop;
- manual authenticated firmware check/install from one fixed GitHub Release,
  an application-owned Start interlock, and bounded post-update validation;
- one static low-priority core-0 `OtaTask`, dual 3 MiB OTA slots, app rollback,
  and a `0.13.0` tag-gated release workflow.

The initial controller is deliberately simple: it requests `100%` below the
active chamber target and `0%` at or above it. A missing target means monitoring
only and always requests `0%`. PID/hysteresis tuning remains an implementation
choice for a later, evidence-driven change.

The dependency direction is:

```text
main -> smoker_app -> smoker_core
main -> smoker_platform -> smoker_app
```

`main/app_main.cpp` is the thin composition root for a simulated-I/O runtime
with local connectivity.
FreeRTOS task/watchdog mechanics and the owned simulation context live in
`smoker_platform`. The context is allocated once before the critical task starts
and ownership is transferred to that task, keeping the large application state
off both the ESP-IDF main-task stack and the control-task stack.
`SmokerApplication` is the sole mutable-state owner and the sole path to the
final heater write. Its cycle acquires raw inputs, handles commands, derives
probe events/alarms using those commands, updates timer state, evaluates safety
synchronously, applies the safety gate, writes the simulated heater, and then
publishes events. The simulated event history is bounded to the newest 64
entries and reports how many older entries were overwritten.

## Local ESP-IDF setup

The project is pinned to ESP-IDF `v6.0.2`. The commands below keep the framework,
Python environment, and cross-toolchain under the ignored `.tools/` directory.
Run them once from the repository root after installing the official ESP-IDF
system prerequisites:

```sh
git clone --branch v6.0.2 --depth 1 --recurse-submodules \
  https://github.com/espressif/esp-idf.git .tools/esp-idf-v6.0.2

IDF_TOOLS_PATH="$PWD/.tools/espressif" \
  .tools/esp-idf-v6.0.2/install.sh esp32s3
```

Activate that pinned environment in every new shell:

```sh
export IDF_TOOLS_PATH="$PWD/.tools/espressif"
. "$PWD/.tools/esp-idf-v6.0.2/export.sh"
```

Verify the baseline:

```sh
idf.py --version
```

The expected result is `ESP-IDF v6.0.2`.

Project configuration fails immediately for every other ESP-IDF version. The
target build also replaces ESP-IDF's default GNU++26 mode with strict C++20 for
one reproducible project baseline.

## Build

From an activated ESP-IDF shell:

```sh
idf.py set-target esp32s3
idf.py build
```

`set-target` creates the generated, ignored `sdkconfig`. Versioned configuration
belongs in `sdkconfig.defaults`; do not edit generated `sdkconfig` manually.
M13 ordinary builds are deliberately unsigned. ESP-IDF's signed-update
bootloader rejects their `smoker_controller.bin`, so the project makes
`idf.py flash`, `app-flash`, `bootloader-flash`, `partition-table-flash`, and
`otadata-flash` fail closed before any serial write. Use the signed serial
workflow documented below.

Firmware build validation does not require a physical board. M12 final-board
validation includes flashing and radio tests, but a cross-build alone does not
close that gate.

## Local UI and API (M12)

With no saved STA credentials the controller starts the intentionally open
SoftAP `Smoker-<MAC6>` at `192.168.4.1`; joining it requires no Wi-Fi password.
With saved credentials it tries STA first and
enables fallback after 30 seconds disconnected. After STA receives an IP, the
AP stops and the controller is published as `smoker-<mac6>.local`.

The fallback delay is measured from the start of the outage, so repeated failed
STA authentication attempts cannot postpone provisioning access. Once STA gets
an IP, serialized mode switching disables AP/DNS and retries that transition if
ESP-IDF reports a transient mode-change error.

While SoftAP is active, DHCP option 114 advertises `http://192.168.4.1/` and a
SoftAP-only wildcard DNS responder redirects common captive-network probes to
the public Wi-Fi setup page at `http://192.168.4.1/`. Failure of this helper
only removes automatic opening; manual `192.168.4.1` access remains available.

The AP page and its status/scan/save APIs are commissioning-only. They never
serve login, dashboard assets, snapshots, cooking state, or commands, even when
a client supplies a valid cookie. HTTP scope is classified from the socket's
local AP/STA address before authentication. Operational scope is additionally
denied while SoftAP is active, because lwIP local-address acceptance alone does
not prove the ingress interface during APSTA overlap.

After STA connects to WPA2/WPA3 Personal, the LAN login asks only for the device
password and includes an `Arată`/`Ascunde` control. It creates one random
256-bit `HttpOnly`, `SameSite=Lax` session cookie with a 30-minute idle timeout.
Every login replaces the prior token; logout and password change invalidate it.
There is no username, HTTP Basic, Bearer access, or JavaScript token storage. By
explicit product decision, every new or still-unclaimed device uses:

```text
password: smoker257500
```

The commissioning AP is deliberately open by explicit product decision D046.
This permits anyone in radio range to observe or modify plaintext Wi-Fi setup,
but D047 prevents AP access to smoker runtime/control data. Device-password
replacement is a separate authenticated LAN operation requiring current and new
passwords; it is not part of the network schema. Login failures are rate-limited
per IPv4 peer, including IPv4 clients accepted through ESP-IDF's dual-stack
listener. Wi-Fi credentials and device-authentication state use separate atomic,
versioned NVS blobs with legacy-key migration. M12 still uses HTTP without TLS
and unencrypted NVS, so LAN credentials/cookies and flash secrets are not
end-to-end/at-rest encrypted.
NVS errors are logged and never trigger automatic erase.
Legacy authentication read/corruption errors fail migration instead of
replacing a claimed password with the fixed public initial password.

Protected routes include `/`, `/api/v1/snapshot`, `/api/v1/network`,
`GET`/`POST /api/v1/network/scan`, `/api/v1/commands`, and the firmware routes
described below, plus embedded assets.
Scan start is asynchronous, coalesced, and has a 15-second recovery timeout;
results contain at most 20 visible 2.4 GHz SSIDs, deduplicated by strongest
signal. The UI permits only WPA2 and WPA3 Personal scan results and
keeps manual SSID entry for hidden networks. STA disconnect causes are exposed
as actionable status without returning secrets.

A command `202` means transport admission and includes a correlation ID;
semantic acceptance or rejection appears in a later immutable snapshot. The UI
waits for that result before claiming that the command was applied. Protected
cookie writes require an explicit exact Origin, JSON media types and schemas are
strict, request bodies are limited to 512 bytes, and CORS is not enabled. The
responsive embedded Fumuri UI uses no remote resources, self-schedules one
snapshot poll at a time, supports system/light/dark themes, and labels all
temperature/heater I/O as simulated.

## Firmware update (M13)

The authenticated STA dashboard has an „Actualizare firmware” panel. Check is
manual and always reads the fixed public asset:

```text
https://github.com/CalinFlr/SmokerFirmware/releases/latest/download/smoker_controller.bin
```

`CalinFlr/SmokerFirmware` is the public canonical source and release repository.
The controller carries no GitHub credential: public reachability supplies the
bytes, while the RSA signature—not repository access control—authenticates the
publisher.

`GET /api/v1/firmware` returns bounded status, current/available version,
progress, install permission, and error. `POST /api/v1/firmware/check` accepts no
body. `POST /api/v1/firmware/install` accepts exactly
`{"version":"X.Y.Z"}`. Both POST routes require the exact Origin; the safe GET
accepts a missing browser Origin but rejects a supplied foreign Origin. Every
route requires authentication and operational STA scope, so SoftAP
commissioning cannot access firmware operations.

Checking is permitted during `RUNNING`, but installation is not. After Stop,
installation obtains application permission, blocks Start, downloads into the
inactive slot, verifies and selects the image, then reboots. On the first
`PENDING_VERIFY` boot, five consecutive safe ControlTask/TWDT cycles are needed
to mark the image valid; a fault or ten-second timeout rolls back and reboots.
The UI explains the required Stop and post-reboot reconnection.

OTA permission uses a dedicated bounded signal into `ControlTask`; it is not
queued behind HTTP commands, and a timeout Finish cannot overtake its Prepare.
The static OTA worker stack/TCB are kept in internal DRAM so they remain
accessible while ESP-IDF flash operations suspend cache access to PSRAM.

The download client disables automatic redirects, follows no more than five
redirects, and verifies that every connection remains HTTPS. Availability
parses the actual ESP image chip ID, project, and version before it can offer an
update. Total monotonic deadlines are 30 seconds for check, five minutes for
install, and ten seconds for application permission; failure releases the Start
interlock.

M13 does not support arbitrary URLs, LAN upload, automatic checks, downgrade,
or reinstall. ESP-IDF verifies an RSA-3072 signature at `esp_ota_end()` and
trusts only the public-key digest carried by the currently running signed app.
This authenticates remote OTA releases even if the GitHub asset or transport is
replaced. Hardware Secure Boot, eFuse provisioning, and flash encryption are
not enabled, so an attacker with flash-write/physical access can still replace
the running trust anchor and bypass this software-only verification.

Pushing the exact tag matching `version.txt` (initially `v0.13.0`) triggers the
release workflow. It reruns host/sanitizer/guardrail and ESP-IDF 6.0.2 checks,
then signs the padded application inside the tag-restricted `firmware-release`
environment, verifies it against the repository public key, and publishes only
that signed `smoker_controller.bin` plus `smoker_controller.bin.sha256`. The
hash is useful for download/corruption checks but is not the publisher trust
mechanism. The workflow never creates or pushes a tag; tag creation remains an
explicit maintainer action.

### OTA signing-key setup

The private key is never committed, published as an asset, or embedded in the
firmware. The locally generated key is:

```text
local-secrets/smoker_ota_signing_key.pem
```

That directory and common private-key suffixes are ignored by Git. Keep an
encrypted offline backup; losing the only copy prevents future OTA updates to
installed boards, while disclosure lets the holder sign malicious updates.
The matching versioned public key is
`keys/smoker_ota_signing_public.pem` (fingerprint documented in
`keys/README.md`).

Create a GitHub Environment named `firmware-release`, restrict its deployment
tags to `v*.*.*`, and add required reviewers when the repository plan supports
them. Then upload a base64 encoding as an environment secret without creating
another key file:

```sh
base64 < local-secrets/smoker_ota_signing_key.pem \
  | tr -d '\n' \
  | gh secret set SMOKER_OTA_SIGNING_KEY_B64 --env firmware-release
```

Base64 is transport encoding, not encryption; GitHub encrypts the submitted
secret. Ordinary CI and local `idf.py build` intentionally produce a padded,
unsigned image and never receive the key. To exercise the final signing gate
locally after `tools/verify.sh --idf-only`:

```sh
SMOKER_OTA_SIGNING_KEY_FILE="$PWD/local-secrets/smoker_ota_signing_key.pem" \
  tools/sign_release_firmware.sh
```

The resulting repository-root `smoker_controller.bin` is the only application
image suitable for the first full serial M13 flash or a GitHub OTA release.
Validate the signed image, its matching bootloader/partition artifacts, and the
generated M13 configuration without touching a board:

```sh
tools/flash_signed_firmware.py --check-only
```

For the required first M12-to-M13 serial migration, provide the exact port and
explicit write confirmation:

```sh
tools/flash_signed_firmware.py \
  --port /dev/cu.usbmodemNNN \
  --yes
```

The helper verifies the RSA-3072 signature against the versioned public key,
checks that the signed image is derived from the selected build, checks the
effective M13 Kconfig and generated partition table, then uses ESP-IDF's own
generated flash map. It writes bootloader, partition table, initial OTA data,
and the signed application; it does not erase or write the NVS partition. Back
up NVS before migration. The 2026-08-17 KFB003 migration retained the logical
STA and authentication records with matching value CRCs and reconnected using
the saved STA configuration; this is evidence for that board, not a substitute
for making a backup on every device.

The ordinary ESP-IDF flash targets cannot be used as a shortcut: they are
attached to an unsigned application and abort with a pointer to this helper.
`erase-flash` remains available only as an explicit destructive recovery tool;
it is not part of the M13 migration.

Key rotation is not an ordinary OTA operation in this software-only ESP-IDF
mode: all installed boards must be serial-flashed with an initial image signed
by the new key (or migrated under a separately designed hardware Secure Boot
policy).

The absent release reviewer is a conditionally accepted P3 hardening risk only
while repository and tag-write access remain limited to the single maintainer.
If another human or bot receives either capability, D051 requires the signing
boundary to be reviewed before the next release; tag restriction alone is not
independent approval.

## Native host tests

The host tests use CMake, CTest, and the native C++ compiler. They do not link
ESP-IDF or require an ESP32. After activating the local tools above (or providing
CMake 3.22+ and Ninja independently), run:

```sh
cmake -S tests -B build-host -G Ninja
cmake --build build-host
ctest --test-dir build-host --output-on-failure
```

The host build compiles `smoker_core`, `smoker_app`, simulated platform adapters,
and tests with the same strict C++20 baseline as the target, warnings as errors,
exceptions disabled, and RTTI disabled.
CTest reports checkpoints for M2, M3, M4, M5, M12 concurrent
transport/snapshot exchange, and M13 OTA policy/version state in addition to
the M1 domain-value tests.

Optional host sanitizer validation:

```sh
cmake -S tests -B build-host-sanitize -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
  -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined'
cmake --build build-host-sanitize
ctest --test-dir build-host-sanitize --output-on-failure
```

## M0-M13 verification and architecture guardrails

The complete local verification entrypoint runs architecture/traceability
guardrails, native host tests, ASan/UBSan, and the pinned ESP-IDF cross-build:

```sh
tools/verify.sh
```

For split CI-style runs:

```sh
tools/verify.sh --host-only
tools/verify.sh --idf-only
```

The host path includes separate AP/STA fixture origins, commissioning-only
scope, password-to-cookie login, logout/password change, rejection of Basic and
STA OPEN, and exact-Origin writes. A real-browser check for both AP setup and STA
login/dashboard rendering, password visibility, and
non-overlapping polling, live focused-probe readings, unsupported Wi-Fi choices,
semantic command feedback, firmware progress/interlock/error recovery, and a
390-pixel responsive viewport is also versioned and uses an
external Playwright CLI without adding npm dependencies to firmware:

```sh
PWCLI=/path/to/playwright_cli.sh tools/check_m12_browser.sh
```

`--idf-only` requires ESP-IDF `v6.0.2`; the script activates the repository's
local pinned installation when available and checks the version exactly. Host
verification uses clean-first builds and also proves that an unknown test group
is rejected. The dependency-free Python checks can also be run directly:

```sh
python3 tools/check_architecture.py
python3 tools/check_traceability.py
python3 tools/check_partitions.py partitions.csv
```

After an ESP-IDF build, run:

```sh
python3 tools/check_target_compile_commands.py build/compile_commands.json
```

This verifies that every project-owned target C++ source was effectively
compiled as strict C++20. The full/IDF verification modes run it automatically.

The architecture check guards layer imports, component dependencies, single
ControlTask ownership, heater writes, command submission, M12 transport/core
placement, M13 OTA task/API/release/partition contracts, and the deliberately
small V0 state/command shape. The traceability check requires exactly one row
for every approved rule and verifies concrete host-test references. GitHub CI
runs these checks with host tests/sanitizers and a separate ESP-IDF v6.0.2
ESP32-S3 cross-build. None of these checks constitutes physical-hardware proof.

The M5 heap-observation tests instrument ordinary replaceable C++ allocation
functions around selected initialized control-cycle paths. They do not observe
every possible libc/custom/target allocation mechanism and are not a substitute
for target heap instrumentation.

## Simulation boundary

The ESP-IDF image runs only simulated temperature sources and a simulated heater;
it does not access GPIO or energize an SSR. The `150 C` maximum used by the demo
and host fixtures is explicitly test input, not a confirmed physical-smoker
safety limit. The N16R8 storage characteristics and module-level pin
restrictions are documented from Espressif primary sources, and storage plus
native USB Serial/JTAG flashing have been confirmed on the target. The final
SuooTci `KFB003` carrier, exposed headers/restrictions, application runtime,
ControlTask scheduling/stack use, and TWDT panic/reset behavior are documented
and target-validated at M6A. Sensor/probe frontends, SSR/power interfaces, final external pin
assignments, and independent electrical protection remain M6B work and must be
confirmed before their real hardware integration.

The versioned task-watchdog defaults configure ESP-IDF startup initialization, a
five-second timeout, and panic/reset for a stalled subscribed loop. M6A exercised
the subscribed `ControlTask` on the physical ESP32-S3: a deliberate seven-second
stall triggered TWDT panic/reset and the following boot reported the watchdog
reset reason. This is not a substitute for the independent hardware
over-temperature/power interruption required at M6B.

The simulated image uses fixed input adapters; it is an end-to-end application
workflow, not a closed-loop thermal-plant model.

## Read order

1. `AGENTS.md`
2. `docs/BUSINESS_RULES.md`
3. `docs/ARCHITECTURE.md`
4. `docs/SAFETY.md`
5. `docs/DATA_MODEL.md`
6. `docs/DECISIONS.md`
7. `docs/ROADMAP.md`
8. `docs/TRACEABILITY.md`
9. `START_CODEX.md`

## Repository shape

Source files are added only as milestones require them.

```text
smoker-controller/
├── CMakeLists.txt
├── sdkconfig.defaults
├── partitions.csv
├── version.txt
├── main/
│   ├── CMakeLists.txt
│   └── app_main.cpp
├── components/
│   ├── smoker_core/
│   ├── smoker_app/
│   └── smoker_platform/
├── tests/
│   ├── CMakeLists.txt
│   └── host/
├── docs/
│   └── TRACEABILITY.md
├── tools/
│   ├── check_architecture.py
│   ├── check_partitions.py
│   ├── check_traceability.py
│   └── verify.sh
├── .github/workflows/ci.yml
├── AGENTS.md
├── README.md
└── .agent/
    ├── PLANS.md
    ├── M0_M5_REMEDIATION_PLAN.md
    ├── P0_ARCHITECTURE_GUARDRAILS_PLAN.md
    └── M13_PLAN.md
```

M13's exact custom layout preserves the M12 24 KiB NVS partition and adds
`otadata`, `phy_init`, and two 3 MiB OTA application slots. The rest of the
target-confirmed 16 MiB flash remains unallocated. Verification enforces the
table and a 75% image-usage ceiling per slot. An M12 device needs one complete
serial flash to adopt this layout because its single-app image cannot migrate
the partition table itself.

## M0-M5 outcome and next gate

The V0 simulated application/control slice includes:

- ESP32-S3 project builds;
- one active session;
- one recipe stage;
- simulated authoritative chamber temperature;
- simulated heater;
- chamber target control;
- optional timer;
- 1..N simulated food probes;
- alarms;
- faults;
- safety override;
- host tests for core business rules.

Wi-Fi provisioning, local HTTP/UI, and M13 OTA software are present. No display,
session/power-recovery persistence, real SSR, real temperature frontend, fan,
smoke generator, or cloud has been added. No physical heater or hardware-safety
behavior has been tested by the simulation/build/unit tests. The signed M13 USB
migration, credential-free public-release download, both-slot boot, forced
pending-image rollback, clean reinstall, five-cycle mark-valid, and persistent
reboot passed on KFB003. The observed simulated heater command remained `0.0%`;
this is OTA-path evidence, not a sensor, SSR, thermal, or independent
hardware-safety test.

The controller product baseline cannot be considered complete before M6B and
M7-M10 identify and integrate the remaining real hardware and implement
persistence/power recovery. M6A is complete for the final SuooTci `KFB003`
N16R8 board. M6B is blocked until the external components/design are available.
Rule-by-rule evidence and deferred work are recorded in
`docs/TRACEABILITY.md`.
