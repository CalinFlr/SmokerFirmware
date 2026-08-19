# M15 Personal Blynk Remote Access Plan

Status: software implementation complete — host/sanitizer and ESP-IDF 6.0.2
cross-build gates pass; Blynk Console plus connected KFB003 TLS/status/command,
reboot no-replay, remote-error e-mail, and firmware-check validation pass.
Native mobile/phone push, exact broker timing, and deliberate outage remain.

## Goal

Give one owner private remote Start/Stop/configuration, live status, Blynk push
events, and user-requested M13 OTA for one home smoker, with no custom backend,
domain, mobile application, or cloud-history synchronization.

## Scope

- Blynk's existing mobile application and Device MQTT API over TLS.
- Official ESP-MQTT component isolated in `smoker_platform`.
- Existing immutable snapshots, bounded command mailbox, semantic command
  results, and M13 OTA service.
- One complete change-driven `batch_ds` status projection: initial on connect,
  then only on normalized change, never more often than once per five seconds.
- Immediate bounded command results and configured critical Blynk events.
- One versioned NVS credential blob provisioned over bounded USB/UART0 frames;
  the per-device token remains non-versioned and absent from source/releases.

## Non-goals

- A custom backend, domain, database, mobile application, or MQTT broker.
- Multi-user accounts, sharing, fleet management, subscriptions, or product
  provisioning.
- Arbitrary remote heater commands, arbitrary OTA URLs, Blynk.Air packaging, or
  automatic OTA installation.
- Upload/backfill of M14 raw history or cloud-owned runtime state.
- Any real-sensor, SSR, thermal, or independent electrical-safety claim.

## Current repository observations

- M12 already supplies the cross-task command mailbox, immutable snapshot
  exchange, correlated results, Wi-Fi, and local authenticated API/UI.
- M13 already installs the fixed latest public GitHub Release asset with
  application permission, ESP32-S3 signed-image verification, rollback, and
  pending-image validation.
- M14 already owns durable local history and shares flash with OTA through a
  platform coordinator; M15 must not turn that history into a cloud dependency.
- The worktree contains a partial M15 projection/mapper implementation. It must
  be corrected for the final plan rather than treated as completed evidence.
- The current HTTP adapter owns private ID counters and one command mailbox;
  M15 requires a common concurrent ID source and fair ControlTask draining of a
  second application-level mailbox.
- No Blynk credential is currently part of the project.

## Assumptions

- The owner accepts Blynk account authentication in the existing Blynk app.
- Exactly one Blynk device/template is required initially.
- The Blynk-provided regional endpoint and device token will be available during
  implementation; they are configuration/secrets, not source constants.
- Blynk Free-plan availability and quotas are auxiliary and may change without
  changing local controller behavior.

## Steps

1. Correct and freeze the 26-datastream/five-event wire contract, including
   separate command results, explicit `timer_configured`, and a proven
   sub-1,024-byte serializer.
2. Pin `espressif/mqtt == 1.0.0`; implement MQTT 3.1.1/TLS on the direct regional
   endpoint, certificate bundle, clean session, keepalive 45 s, QoS 0, no
   retain/sync/replay, `downlink/ds/#` only, and bounded reconnect behavior.
3. Implement fixed storage for normalized projection/throttling, raw MQTT
   command ingress, a reserved-Stop translated mailbox, correlated pending
   results, and five coalesced event types. MQTT callbacks remain copy-only.
4. Add a shared atomic session/correlation ID generator and fair ControlTask
   draining across HTTP/Blynk with a global budget preserving two internal OTA
   admissions and a Stop barrier.
5. Implement a versioned NVS credential blob plus bounded `FUMURI-BLYNK/1`
   UART0 parser and a no-echo local `set|status|clear` provisioning tool.
6. Integrate one static low-priority core-0 `BlynkTask`, isolated from TWDT and
   local control, and route OTA check/install through the existing M13 service.
7. Add host/concurrency/parser/serializer/guardrail tests, run sanitizer and
   ESP-IDF 6.0.2 build/lock/size/affinity gates, then update docs/traceability
   to the exact validation level achieved. Blynk Console/mobile and connected
   KFB003 evidence remain pending without temporary credentials/board access.

## Validation commands

```text
python3 tools/check_traceability.py
python3 tools/check_architecture.py
tools/verify.sh --host-only
tools/verify.sh --idf-only
```

Connected-target validation additionally observes Blynk message timing and
silence, command semantics, notification delivery, reconnect without command
replay, and Blynk-triggered signed GitHub OTA while not `RUNNING`.

## Risks / unresolved items

- The Blynk Console/mobile configuration and live KFB003 scenarios require the
  owner's temporary Blynk credentials and connected USB device; source/build
  work cannot claim those target results.
- Blynk Device MQTT currently uses clean sessions and public-service limits;
  implementation must fail remote access closed and never compensate by
  replaying energizing commands.
- Notification delivery and cloud graphs are best-effort auxiliary behavior,
  not safety evidence or authoritative M14 history.

## Execution log

- 2026-08-18: Re-read all M15 source-of-truth contracts, architecture/safety
  boundaries, existing M12 mailbox/snapshot transport, M13 update path, and
  M14 history isolation. Fixed the template/wire contract before implementation.
  No firmware source, dependency, generated configuration, token, or hardware
  claim was added in this checkpoint.
- 2026-08-18: Added the initial allocation-free value projection/throttle policy
  and MQTT dependency declaration. The focused M15 host
  executable passes via direct strict-C++20 compilation; full CMake/CTest is
  unavailable in this environment because `cmake` is not installed.
- 2026-08-18: Reviewed Blynk's current official options. The C++ library is
  Arduino/ESP32-oriented; Blynk Edgent has an ESP-IDF component but its current
  registry release supports IDF only through 6.0.1 and bundles provisioning/
  Blynk.Air behavior outside M15. The official Device MQTT API plus ESP-IDF's
  native MQTT client remains the compatible choice. Added the bounded
  allowlisted command mapper and strict host coverage; no third-party Blynk
  firmware component is added.
- 2026-08-18: The final implementation brief superseded the partial local-header
  design. Re-read all source-of-truth files and official Blynk/ESP-MQTT docs;
  switched the living plan to UART0/NVS provisioning, two-stage bounded command
  transport, shared IDs, fair ControlTask draining, and separate feedback.
- 2026-08-18: Completed the platform-only service, exact MQTT 1.0.0 pin/lock,
  static core-0 task, two mailboxes, common IDs, status/result/event paths,
  UART0/NVS provisioning tool, 26-datastream contract, tests, documentation,
  and M15 architecture/traceability guardrails. `tools/verify.sh --host-only`
  passes all 9 tests in normal and ASan/UBSan builds. A fresh ESP-IDF 6.0.2
  `--idf-only` verification passes strict C++20 for 23 project sources,
  effective MQTT configuration, dependency/partition/unsigned-flash gates, and
  firmware size (1,376,256 / 3,145,728 bytes, 43.8%). No Blynk credential or
  board/cloud claim was added; console/mobile/provisioning/reconnect/
  notification scenarios remain target-pending.
- 2026-08-18: Provisioned the connected KFB003 through UART0 and confirmed the
  redacted configuration after signed 0.15.0 reboot without rewriting NVS.
  Configured the owner Blynk Console device, all 26 datastreams with sync/replay
  disabled, five notification events, and a saved 22-widget web dashboard via
  headed Playwright. Re-ran all nine host tests under normal and ASan/UBSan and
  the ESP-IDF 6.0.2 target gate; both pass, with the signed application using
  1,380,352 / 3,145,728 bytes (43.9%). Live TLS/status/command/notification and
  reconnect/no-replay remain pending because the selected iPhone hotspot is
  not currently visible to the board; no cloud or mobile result is claimed.
- 2026-08-18: Provisioned the home 2.4 GHz STA through a preserved application
  NVS image without changing the host network, then observed KFB003 WPA2/IP,
  regional TLS validation, Blynk Online state, complete live status, simulated
  Start/Stop with semantic acceptance and heater OFF after Stop, reboot to
  IDLE/OFF without Start replay, and a Blynk-triggered M13 check reporting
  `UP_TO_DATE`. Triggered the configured remote-error event with a rejected
  malformed command while heater remained OFF; the owner confirmed Blynk
  e-mail delivery for both the pre-rotation and final-device triggers, while
  phone push remains pending. Rotated/reprovisioned the Blynk device after a Playwright
  snapshot exposed the previous token, deleted the old device to revoke it,
  retained exactly one Online device, cleared the clipboard, and securely
  overwrote/deleted credential-bearing NVS and Playwright temporary files.
