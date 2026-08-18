# M14 Durable Telemetry and Session History — Execution Plan

## Goal

Deliver durable, local, read-only session history for the simulated controller
without making storage, HTTP, wall-clock synchronization, Wi-Fi, or OTA part of
the heater-control dependency chain.

## Scope

- Add a 4 MiB raw-flash `history` partition after the two 3 MiB OTA slots.
- Add a versioned, CRC-checked, commit-last circular page/record log with lazy
  sector initialization, reboot reconstruction, completed-session-first
  eviction, long-session truncation, and interrupted-session reporting.
- Publish bounded history observations from `ControlTask` through a preallocated
  16-entry SPSC mailbox only after safety-gated `tick()` and snapshot exchange.
- Add one static low-priority core-0 `HistoryTask`, optional synchronized UTC,
  shared platform flash-operation serialization with OTA, health/capacity
  counters, authenticated STA-only history APIs, and a bounded Romanian chart.
- Add M14 host, sanitizer, HTTP/browser, architecture, traceability, partition,
  ESP-IDF, signed-helper, and connected-target validation where available.
- Update living documentation and set `version.txt` to `0.14.0` only after the
  review checkpoint and all non-hardware gates pass.

## Non-goals

- M10 configuration/session recovery or automatic session resume.
- Real sensors, real SSR output, thermal/electrical validation, or independent
  hardware-safety claims.
- Cloud synchronization, CSV export, manual erase/delete, high-frequency
  telemetry, telemetry outside an active session, or fixed retention promises.
- A public tag or release.

## Current repository observations

- M13 is complete and the worktree is initially clean on `main`.
- `SmokerApplication` remains the sole runtime-state writer; `ControlTask` is the
  sole production caller of `submit()` and writes the safety-gated heater output.
- A 16-entry HTTP-to-control SPSC mailbox and preallocated triple snapshot
  exchange already isolate network work from `ControlTask`.
- `OtaTask` is a static low-priority core-0 task and currently owns OTA flash
  writes; no shared flash-operation lock or wall-clock service exists yet.
- The custom table preserves 24 KiB NVS and dual 3 MiB OTA slots, ending at
  `0x620000`; the confirmed target flash is 16 MiB.
- The embedded dashboard is vanilla HTML/CSS/JS and the HTTP server already
  enforces socket scope, Host, cookie authentication, and exact supplied Origin.
- The active I/O remains simulated and M10 persistence/recovery is intentionally
  absent.

## Assumptions

- The custom history subtype will be `data, 0x40`; partition lookup uses its
  stable label so no product capability is inferred from the numeric subtype.
- Flash sectors and log pages are both 4 KiB. Records never cross a page, and a
  session begins on a fresh page. Capacity/retention is reported, not promised.
- Monotonic `session_elapsed` is authoritative and is computed from the existing
  application session start/stop points. UTC is optional and may first appear
  midway through a session after SNTP synchronization.
- History is an auxiliary platform concern. A full mailbox drops the observation;
  lock contention/OTA defers writes; repeated storage errors mark history
  `FAILED` and never submit a command, raise an application fault, alter timers,
  or write the heater.
- Connected KFB003 and signing-key availability will be discovered rather than
  assumed. No serial write is performed unless the existing signed helper's
  preflight succeeds and the intended device is positively detected.

## Steps

1. Define M14 history rules/data/decision and executable storage/runtime
   contracts; update the partition and validation guardrails.
2. Add `session_elapsed` to allocating and view snapshots and to
   `SnapshotExchange`, with application tests.
3. Implement dependency-free history models, fixed observations/mailbox,
   encoding, flash interface, circular reconstruction/append/query logic, and
   in-memory NOR-flash tests for power-loss/corruption/rollover/eviction.
4. Add platform flash mutex, optional UTC helper, ESP partition adapter, static
   `HistoryTask`, OTA coordination, status publication, and ControlTask change /
   60-second sampling policy.
5. Add strict authenticated operational history APIs with incremental bounded
   JSON, then add the responsive no-dependency Romanian history chart.
6. Extend host/concurrency/allocation, HTTP, browser, architecture,
   traceability, effective-Kconfig, task/flash ownership, signed-helper, and
   documentation validation.
7. Run the full host/sanitizer/guardrail/browser suite and a clean ESP-IDF
   v6.0.2 build/size gate. Review recovery, critical-path isolation, HTTP
   authorization, OTA coexistence, and documentation; resolve findings.
8. After review, bump to `0.14.0`, run signed serial-helper preflight, and—only
   when the expected KFB003 and key are available—perform and record the scoped
   partition/NVS/session/Wi-Fi-loss/reboot/task/TWDT runtime scenario.

## Validation commands

```text
tools/verify.sh --host-only
tools/check_m14_browser.sh
tools/verify.sh --idf-only
python3 tools/flash_signed_firmware.py --check-only ...
```

Focused host binaries and fixture checks will also run during implementation so
failures are localized before the complete suites.

## Risks / unresolved items

- Raw-flash recovery and eviction are the highest data-integrity risk; tests
  must inject torn header, payload, and commit writes plus arbitrary media.
- ESP flash-cache suspension requires both auxiliary task stacks/TCBs in
  internal DRAM and a platform-owned lock whose use cannot extend to
  `ControlTask`.
- Existing HTTP code is large and single-tasked; response generation must remain
  page-bounded and stream records rather than building full sessions in RAM.
- Browser point budgeting must preserve lifecycle/change records while applying
  stride only to periodic samples.
- Physical validation may be unavailable or require user interaction despite
  the stated device/key availability. Such gaps will be reported separately
  from host/build/browser evidence.

## Progress

- 2026-08-18: the evidence-based post-review findings are fixed. Whole-session
  eviction now commits a page-header tombstone before the first victim erase,
  keeps it until the other pages are gone, and completes interrupted cleanup on
  reboot; an injected failure between two victim erases proves that no partial
  old session is exposed. The dashboard now paginates through terminal END and
  applies its 1,200-point bound by removing SAMPLE records first; a real-browser
  fixture with 700 CHANGE records proves END remains visible beyond the former
  650-point cutoff. Host, ASan/UBSan, HTTP, Playwright, architecture/
  traceability, and a fresh ESP-IDF 6.0.2 strict-C++20 build all pass for this
  remediation.
- 2026-08-18: evidence-based diff review opened three remediation checkpoints:
  retry a failed durable START without starving later mailbox records; preserve
  START+END when a session reaches STOPPED/FAULT in its first observed control
  cycle; and reload the selected chart once when an active history receives its
  terminal END. Each fix requires a focused regression before the complete
  host, browser, and fresh ESP-IDF gates are repeated.
- 2026-08-18: all three review checkpoints are closed. HistoryTask now retries
  failed START/END lifecycle writes in FIFO order; first-observed terminal
  sessions admit an all-or-nothing START+END pair; and the dashboard reloads
  once when its selected active history becomes terminal. Focused host tests,
  complete native plus ASan/UBSan verification, the real Playwright transition,
  architecture/traceability/HTTP guardrails, and a fresh ESP-IDF v6.0.2 build
  with the exact M14 table all pass. No new hardware-safety claim is made.
- 2026-08-18: source-of-truth documents and current M13 architecture inspected;
  plan created before implementation.
- 2026-08-18: durable format/mailbox/task, flash/OTA coordination, authenticated
  APIs, bounded dashboard, documentation, and M14 guardrails implemented and
  audited. Focused tests, complete host + sanitizer suite, HTTP fixture, real
  Playwright browser contract, and a clean ESP-IDF v6.0.2 build all pass;
  `0.14.0` is set. Connected-target preflight/runtime evidence remains.
- 2026-08-18: USB preflight positively identified the connected ESP32-S3 rev.
  0.2 with 8 MiB PSRAM and 16 MiB flash. Target installation stopped safely:
  no local signing-key source is configured, and the existing signed image does
  not match the M14 build. No serial flash write was attempted.
- 2026-08-18: the existing local signing key was subsequently made available.
  The reviewed M14 image was signed and installed with the signed USB helper on
  KFB003. Readback confirmed the exact M14 partition table and byte-for-byte NVS
  preservation. A target-only review found that HistoryTask retained its flash
  lease during the idle wait, causing transient history API 503 responses; the
  lease scope was corrected, all gates were rerun, and the corrected signed
  image was installed.
- 2026-08-18: target runtime recorded two 66--67 second simulated sessions and
  one short diagnostic session. Reboot reconstruction retained durable history
  IDs 1--3, START/SAMPLE/END lifecycle records, optional UTC, and the first
  session's live target/alarm CHANGE. The operational API reported READY,
  4,194,304-byte capacity, 1,054 bytes used, and zero mailbox drops, corrupt
  records, or write errors; strict malformed/not-found responses were 400/404.
  Serial evidence confirmed ControlTask on core 1 and the static HistoryTask on
  core 0 outside TWDT. The physical Wi-Fi disconnect/reconnect-during-RUNNING
  scenario remains unexecuted because no non-invasive target control exists;
  saved credentials were not deliberately damaged to manufacture that case.
- 2026-08-18: post-review recovery fixes are complete: an unreadable page now
  aborts and retries initialization rather than becoming reusable; only a fully
  committed record clears the consecutive-write-error counter; HistoryTask
  retains a failed END for retry; and a CHANGE at a 60-second boundary also
  emits the due SAMPLE. Host, ASan/UBSan, architecture, HTTP fixture, real
  browser, and clean ESP-IDF v6.0.2 validation pass for the corrected code.
