# M0-M13 Requirements Traceability

Status: **M13 software implemented; physical connectivity and OTA/rollback
validation pending**

This matrix separates implementation status from validation strength. A rule is
not considered target- or hardware-validated merely because host tests or an
ESP-IDF cross-build pass.

## Validation levels

- **H-pass** — native host behavior test passes; sanitizer coverage is also run
  for the complete host suite.
- **B-pass** — ESP-IDF v6.0.2 cross-build for the ESP32-S3 family passes.
- **T-pass** — behavior was executed and observed on the connected target.
- **T-pending** — behavior must be executed on the exact target board.
- **HW-pending** — electrical/thermal behavior requires identified hardware and
  an independent hardware test/review.
- **Guardrail** — an intentional absence/architecture constraint checked by
  source/dependency review and build, not a runtime product behavior.
- **Deferred Mx** — the rule belongs to a future roadmap milestone and M0-M5 do
  not claim implementation.

Test names below refer to functions in `tests/host/smoker_core_tests.cpp`,
`tests/host/smoker_v0_tests.cpp`, `tests/host/smoker_m12_tests.cpp`, and
`tests/host/smoker_m13_tests.cpp`. All host test groups are registered in
`tests/CMakeLists.txt`.

## Business rules

| Rule | Milestone/status | Implementation evidence | Test/evidence | Validation |
|---|---|---|---|---|
| BR-001 | M3 implemented | `SmokerApplication::process(StartSessionCommand)` rejects Start while running | `test_m3_session_and_snapshot` | H-pass, B-pass |
| BR-002 | M1/M5 implemented | separate chamber state and `ProbeRuntime`/`ProbeSnapshot` types | `test_m5_complete_slice` | H-pass, B-pass |
| BR-003 | M2 implemented | single `IChamberSensor` input used by control and safety | `test_m2`, `test_m4_invalid_and_latched_fault` | H-pass, B-pass |
| BR-004 | M5 implemented | probe configuration is a vector; non-empty/unique IDs are validated | `test_m5_complete_slice`, `test_m5_validation_queue_and_combined_order` | H-pass, B-pass |
| BR-005 | M5 implemented | `calculate_heater_demand()` accepts only chamber current/target; probe logic emits alarms only | `test_m5_complete_slice` | H-pass, B-pass |
| BR-006 | M5 implemented | `ProbeSnapshot` exposes all required properties | `test_m5_complete_slice` | H-pass, B-pass |
| BR-007 | M5 implemented/clarified | `evaluate_probe_state()` gates target alarms on RUNNING, enabled, alarm-enabled, and one latch per target/session | `test_m5_complete_slice`, `test_m5_same_cycle_command_semantics`, `test_m5_alarm_lifecycle_and_probe_defaults` | H-pass, B-pass |
| BR-008 | M3 implemented | `Recipe` owns exactly one `Stage` | `test_m3_session_and_snapshot` | H-pass, B-pass |
| BR-009 | M3 guardrail | session enum contains only Idle/Running/Stopped/Fault; labels remain strings | enum/source review plus ESP/host builds | Guardrail, B-pass |
| BR-010 | M3 implemented/clarified | `StageTimer` and `update_stage_timer()` support the three starts, wait for an enabled valid selected-probe sample, and execute one-way | `test_m3_timer_conditions`, `test_m3_probe_timer_availability` | H-pass, B-pass |
| BR-011 | M10 | no store/recovery implementation in M0-M5 | roadmap and absence review | Deferred M10 |
| BR-012 | M3 guardrail | recipe values drive control; no food-safety engine exists | source/dependency review | Guardrail, B-pass |

## Session rules

| Rule | Milestone/status | Implementation evidence | Test/evidence | Validation |
|---|---|---|---|---|
| SR-001 | M3 implemented | each Start constructs/replaces one `Session` run with an ID | `test_m3_session_and_snapshot` | H-pass, B-pass |
| SR-002 | M3 implemented | recipe is moved/swapped into `recipe_snapshot`; caller edits do not mutate it | `test_m3_session_and_snapshot`, `test_m5_start_is_heap_quiet_and_rearms_target_alarm` | H-pass, B-pass |
| SR-003 | M3 implemented/P0 hardened | accepted Stop ends command draining for the tick, freezes timer, records reason, and final gate writes OFF; Stops separated by intervening commands retain distinct FIFO intent | `test_m3_session_and_snapshot`, `test_m5_same_cycle_command_semantics`, `test_p0_sr_003_manual_stop_is_off_barrier`, `test_p0_stop_coalescing_preserves_fifo_intent` | H-pass, B-pass |
| SR-004 | M3/M4 implemented | `SetChamberTargetCommand` updates active target only and rejects above safety maximum | `test_m3_session_and_snapshot`, `test_m4_over_temperature_and_limits` | H-pass, B-pass |
| SR-005 | M5 implemented/clarified | live target changes `session_target_temperature`; immutable defaults are restored on new Start | `test_m5_alarm_lifecycle_and_probe_defaults` | H-pass, B-pass |
| SR-006 | M3/M4 implemented | `SessionStatus` contains exactly the approved four values | `test_m3_session_and_snapshot`, `test_m4_invalid_and_latched_fault` | H-pass, B-pass |
| SR-007 | future guardrail | no Pause command/state exists | enum/command/source review | Guardrail |

## Recipe and timer rules

| Rule | Milestone/status | Implementation evidence | Test/evidence | Validation |
|---|---|---|---|---|
| RR-001 | M3 implemented | recipe is copied/moved into session snapshot, separate from live target/settings | `test_m3_session_and_snapshot` | H-pass, B-pass |
| RR-002 | M3 implemented | `Recipe::stage` is a scalar, not a collection | `test_m3_session_and_snapshot`, `test_m3_timer_conditions` | H-pass, B-pass |
| RR-003 | M2/M3 implemented | Stage has name, optional chamber target, optional timer; missing target returns OFF | `test_m2`, `test_m3_timer_conditions` | H-pass, B-pass |
| TR-001 | M3 implemented/clarified | Immediate/chamber/probe threshold variants; disabled/disconnected selected probes wait until a later enabled valid sample | `test_m3_timer_conditions`, `test_m3_probe_timer_availability` | H-pass, B-pass |
| TR-002 | M3 implemented | started timer ignores later threshold movement | `test_m3_timer_conditions` | H-pass, B-pass |
| TR-003 | M3/M5 implemented | Notify raises timer alarm; StopSession stops and writes OFF | `test_m3_timer_conditions`, `test_m5_complete_slice` | H-pass, B-pass |

## Event, alarm, and fault semantics

| Rule | Milestone/status | Implementation evidence | Test/evidence | Validation |
|---|---|---|---|---|
| EV-001 | M5 implemented | bounded `Event` model/sink includes lifecycle and queue-overflow facts | `test_m5_complete_slice`, `test_m5_bounded_event_sink`, `test_m5_alarm_lifecycle_and_probe_defaults` | H-pass, B-pass |
| EV-002 | M5 implemented/clarified | Alarm has independent acknowledged/resolved flags; active snapshot filters resolved alarms | `test_m5_complete_slice`, `test_m5_alarm_lifecycle_and_probe_defaults` | H-pass, B-pass |
| EV-003 | M4 implemented | `raise_fault()` latches fault and safety gate forces OFF | `test_m4_invalid_and_latched_fault`, `test_m4_over_temperature_and_limits` | H-pass, B-pass |

## Control rules

| Rule | Milestone/status | Implementation evidence | Test/evidence | Validation |
|---|---|---|---|---|
| CR-001 | M2/M5 implemented | heater control reads only authoritative chamber input | `test_m2`, `test_m5_complete_slice` | H-pass, B-pass |
| CR-002 | M2 implemented | missing target returns `HeaterDemand::off()` while chamber remains readable | `test_m2` | H-pass, B-pass |
| CR-003 | M1/M2 implemented | `HeaterDemand` enforces finite 0..100 percent; platform output owns electrical conversion | `test_heater_demand`, `test_m2` | H-pass, B-pass |
| CR-004 | M4 implemented | recipe/live chamber targets cannot exceed configured simulation maximum | `test_m4_over_temperature_and_limits` | H-pass, B-pass |
| CR-005 | M3/M4 implemented/P0 hardened | `apply_safety_gate()` allows demand only for Running without active fault | `test_m3_session_and_snapshot`, `test_p0_cr_005_heating_state_invariants` | H-pass, B-pass |
| CR-006 | M2 implementation choice | simple deterministic 100/0 controller documented; no PID claim | `test_m2` | H-pass, B-pass |

## Safety rules

| Rule | Milestone/status | Implementation evidence | Test/evidence | Validation |
|---|---|---|---|---|
| SF-001 | M4 implemented/P0 guarded | safety evaluated synchronously before sole final heater write | `test_m4_invalid_and_latched_fault`, `test_p0_cr_005_heating_state_invariants`; `tools/check_architecture.py` write-path check | H-pass, B-pass; T-pending |
| SF-002 | M4 implemented for simulated source | absent authoritative reading raises ChamberSensorInvalid; no last-known fallback | `test_m4_invalid_and_latched_fault` | H-pass, B-pass; real policy Deferred M7 |
| SF-003 | M4 implemented for configured simulation limit | safety evaluation faults above maximum; target validation rejects above it | `test_m4_over_temperature_and_limits` | H-pass, B-pass; physical value HW-pending |
| SF-004 | M0/M4 partial | constructor writes OFF; config/sensor/safety validated before final demand | `test_m2`, M4 tests, config-invalid M5 test | H-pass, B-pass; reset reason/recovery Deferred M10 |
| SF-005 | M5 configured/M6A target-validated | platform runtime subscribes/resets TWDT; defaults set 5 s and panic/reset | target diagnostic deliberately stalled `ControlTask` for 7 s; TWDT identified the task, panicked/reset, and next boot reported watchdog reset reason; normal image restored | B-pass, T-pass |
| SF-006 | M10 | reset reason and recovery policy are absent by design | roadmap/absence review | Deferred M10 |
| SF-007 | M4 implemented | fault persists after signal recovery; clear requires resolved condition and leaves session stopped | `test_m4_invalid_and_latched_fault` | H-pass, B-pass |
| SF-008 | M5 implemented | food probe disconnect creates alarm only and does not change heater/fault | `test_m5_complete_slice`, `test_m5_alarm_lifecycle_and_probe_defaults` | H-pass, B-pass |
| SF-009 | M12 implemented/target pending | HTTP uses bounded mailbox/snapshot transports; only ControlTask submits and connectivity failure does not gate its creation | `test_mailbox_concurrency`, `test_snapshot_exchange_concurrency`; architecture review | H-pass, B-pass; T-pending Wi-Fi-loss run |
| SF-010 | M4 implemented/P0 guarded | only `SmokerApplication` writes heater; every tick output passes `apply_safety_gate()` | `test_p0_sr_003_manual_stop_is_off_barrier`, `test_p0_cr_005_heating_state_invariants`; `tools/check_architecture.py` write-path check | H-pass, B-pass; T-pending |
| SF-011 | M6B external-hardware gate | explicitly no external safety-hardware implementation/claim | `docs/HARDWARE.md` M6B checklist | HW-pending M6B |
| SF-012 | future | no current-sensing/stuck-ON simulation exists | source/hardware capability review | Deferred, HW-pending |

## OTA rules

M13 implements the software OTA boundary with simulated-I/O and cross-build
evidence. Live GitHub download, two-slot boot, mark-valid, and rollback remain
target-pending on KFB003.

| Rule | Milestone/status | Implementation evidence | Test/evidence | Validation |
|---|---|---|---|---|
| OTA-001 | M13 implemented | OTA helpers/service remain in `smoker_platform`; app owns only internal permission commands and interlock; core has no OTA/platform include | `test_semantic_versions_and_descriptors`; `tools/check_architecture.py` layer checks | H-pass, B-pass, Guardrail |
| OTA-002 | M13 implemented | immutable snapshot admission plus correlated Prepare rejects RUNNING; dedicated atomic Prepare is ordered before Finish so timeout cannot orphan a delayed reservation; Start is blocked while update active | `test_application_update_permission` Stop/Prepare/Finish FIFO regression, `test_update_coordinator`; source guardrail | H-pass, B-pass, Guardrail; T-pending live install |
| OTA-003 | M13 implemented | manual image-prefix check is independent of application reservation; real chip ID/project/version admission, HTTPS-only bounded redirects, total deadlines, non-RUNNING install permission, and ESP-IDF RSA-3072 signed-update verification are enforced; the public fixed source requires no device credential, while release signing is isolated from ordinary CI and checked against the versioned public key; D051 makes the missing independent reviewer conditional on sole-maintainer repository/tag control | `test_application_update_permission`, `test_semantic_versions_and_descriptors`, `test_image_metadata_and_deadlines`, `test_update_coordinator`; effective-Kconfig/signing/serial source guardrails | H-pass, B-pass, Guardrail; T-pending live signed/tampered install, redirect, and timeout |
| OTA-004 | M13 implemented | exact custom table has `otadata` and equal `ota_0`/`ota_1` 3 MiB slots within confirmed 16 MiB flash; rollback config enabled; signed serial preflight rejects stale configuration, unsigned/build-mismatched apps, and mismatched generated layouts; ordinary unsigned/partial ESP-IDF flash targets fail closed | `test_update_coordinator`; `tools/check_effective_sdkconfig.py`, `tools/flash_signed_firmware.py --check-only`, `tools/check_partitions.py`, blocked ESP-IDF flash-target validation, ESP-IDF partition output; signed KFB003 USB migration with pre/post logical NVS audit | H-pass, B-pass, Guardrail; T-pass first serial migration/preserved state, T-pending two-slot rollback |
| OTA-005 | M13 implemented | `PENDING_VERIFY` blocks Start; five safe TWDT-reset cycles mark valid; fault/10 s/mark error or runtime-context/ControlTask/OtaTask bootstrap failure invokes rollback reboot | `test_application_update_permission`; `tools/check_architecture.py` pending-bootstrap source guardrail | H-pass, B-pass, Guardrail; T-pending mark-valid/rollback |
| OTA-006 | M13 implemented | static low-priority core-0 OtaTask owns bounded SNTP/HTTPS/flash operations and is outside TWDT; its stack/TCB are internal-DRAM objects, not part of the PSRAM-eligible heap service; ControlTask exchanges ordered bounded atomic signals/snapshots only; unavailable worker reports `FAILED` and rejects work | `test_image_metadata_and_deadlines`, `test_update_coordinator`; `tools/check_architecture.py` task-placement/order/availability checks | H-pass, B-pass, Guardrail; T-pending Wi-Fi-loss install |

## M5 command/runtime contracts added by review remediation

| Contract | Implementation evidence | Test/evidence | Validation |
|---|---|---|---|
| raw acquisition has no probe event/alarm side effects | `acquire_raw_inputs()` separate from `evaluate_probe_states()` | `test_m5_same_cycle_command_semantics` | H-pass, B-pass |
| Stop admission and coalescing preserve FIFO intent | 15 regular slots plus one reserved Stop slot; `trailing_stop_is_pending()` coalesces only consecutive Stops and preserves a Stop after an intervening command | `test_m5_validation_queue_and_combined_order`, `test_p0_stop_coalescing_preserves_fifo_intent` | H-pass, B-pass |
| correlated coalesced Stops share actual semantics | bounded coalesced IDs remain on the queued Stop and are recorded only after its semantic processing | idle/running cases in `test_m5_command_result_correlation` | H-pass, B-pass |
| accepted manual Stop creates an OFF-cycle barrier | command draining stops after a valid manual Stop; later FIFO commands remain queued | `test_p0_sr_003_manual_stop_is_off_barrier` | H-pass, B-pass |
| regular-command overflow is observable | `CommandQueueOverflow` plus cumulative snapshot count | queue section of `test_m5_validation_queue_and_combined_order` | H-pass, B-pass |
| command submission has one M5 owner | header/architecture contract; runtime submits from ControlTask | source/dependency review | Guardrail; future transport deferred |
| Duration differs from monotonic time point | tagged `MonotonicClock::time_point` | `test_monotonic_time_types` | H-pass, B-pass |
| `submit()` Boolean is admission, not semantic success | header/docs contract; semantic validation remains in command processing | active-Start rejection in `test_m3_session_and_snapshot`; queue admission in `test_m5_validation_queue_and_combined_order` | H-pass, B-pass |
| initialized representative tick is ordinary-C++-allocation quiet | constructor reserves control-cycle collections; measured tick processes commands/events/alarm/safety/output | `test_m5_representative_tick_is_cpp_heap_quiet` | H-pass; host instrumentation scope only |

## M12 connectivity/runtime contracts

| Contract | Implementation evidence | Test/evidence | Validation |
|---|---|---|---|
| HTTP never calls application submit | only `ControlTask` drains `SpscCommandMailbox` and calls `SmokerApplication::submit()` | `tools/check_architecture.py` production call-site check | Guardrail, B-pass |
| transport preserves FIFO and reserved Stop admission | 15 regular admissions plus one Stop; every cross-core Stop is a distinct entry and no racy transport coalescing occurs | `test_mailbox_fifo_stop_and_overflow`, concurrent alternating Stop coverage in `test_mailbox_concurrency` | H-pass, B-pass |
| cross-task snapshots are consistent and non-blocking | three preallocated slots with atomic read leases and dropped-publish counter | `test_snapshot_exchange`, `test_snapshot_exchange_concurrency` | H-pass, B-pass |
| critical snapshot publication is ordinary-C++-allocation quiet | slot vectors are sized during initialization and publication only copies bounded values | `test_snapshot_exchange` | H-pass; host instrumentation scope only |
| boot requires explicit Start | target runtime no longer queues a startup command; constructor/tick remain IDLE/OFF | existing application lifecycle tests plus source review | H-pass, B-pass; T-pending |
| connectivity work is isolated by core | ControlTask is pinned to core 1; HTTP and configured ESP networking services use core 0 | target boot logged ControlTask core 1, Wi-Fi core 0, and captive DNS core 0 | B-pass; T-pass startup/affinity, scenario pending |
| UI/API security boundary is explicit | D047/D049 classify local AP/STA socket addresses before auth and reject operational scope whenever the D046 open AP is active; the AP remains Wi-Fi setup-only and cannot expose runtime/control even with a valid cookie; STA uses the D045 initial password with one replaceable 256-bit cookie session, exact-Origin writes, and no Basic/admin, Bearer, or STA OPEN support | APSTA-overlap case in `test_http_security_policy`, authority/session/rate-limiter host tests, `check_m12_http_fixture.py`, source guardrails, and two-scope Playwright validation | H-pass, B-pass; D049 overlap failure-injection T-pending; historical D046 open-AP boot remains T-pass only for link configuration |
| Wi-Fi discovery is bounded and sanitized | fixed raw/result buffers omit hidden SSIDs, sanitize text, deduplicate strongest RSSI, sort, and cap at 20 | `test_wifi_scan_curation` | H-pass, B-pass; T-pending radio |
| scan serializes STA reconnect | coalesced scan coordinator blocks reconnect during scan and resumes configured STA after completion/failure; a 15-second timer stops a wedged scan and enters the same recovery path | `test_wifi_scan_transitions`, target build/source review | H-pass, B-pass; T-pending timeout/radio |
| STA outage recovery converges | the first disconnect arms one 30-second deadline, repeated auth failures cannot restart it, AP_STOP/enable/connect errors retain bounded recovery, startup errors expose AP, and serialized/rechecked mode transitions make `GOT_IP` converge to STA-only | `test_wifi_fallback_deadline`, source guardrail, target STA boot/API evidence (`[REDACTED_STA_SSID]`, `[REDACTED_STA_IP]`, AP false on 2026-08-17) | H-pass, B-pass; T-pass saved-STA boot, wrong-password scenario pending |
| captive DNS parser is bounded | parser validates one uncompressed question, rejects malformed/truncated packets, and answers A with the SoftAP address | `test_captive_dns_parser` | H-pass, B-pass; T-pending portal |
| captive helper cannot enter control | static 4 KiB DNS task is pinned to core 0, exists only with SoftAP, and connectivity has no `submit()` call | guardrails plus target log `Captive DNS active on SoftAP core 0` | Guardrail, B-pass; T-pass startup, portal pending |
| Fumuri UI remains embedded and accessible | separate data-free commissioning and full operational pages, canonical local tokens, system fonts, password-only login, logout/password change, no external request, completion-scheduled non-overlapping polls, keyed live probe readings during focus, bounded temperature validation, WPA2/WPA3-only selection, and correlated command feedback | versioned two-port HTTP fixture plus Playwright AP/STA, delayed-response, focus, and command validation | Browser-pass; T-pending iPhone |
| HTTP admission has observable semantic outcome | allocation-free admission JSON is completed before mailbox publication; 32-bit command IDs traverse mailbox/application and a bounded immutable result history distinguishes application acceptance/rejection from HTTP `202`; overflow/coalescing resolve IDs using processed semantics | `test_m5_command_result_correlation`, allocation-free admission-body test, mailbox correlation and snapshot-exchange tests, Playwright command feedback | H-pass, B-pass; T-pending |
| ControlTask transport avoids 64-bit atomic helpers | mailbox read/write sequences are 32-bit wrapping counters; target archive has no undefined `__atomic_load_8`/`__atomic_store_8` | M12 concurrency tests plus target `nm` audit | H-pass, B-pass |
| firmware image retains growth margin | custom dual-OTA table has 3 MiB slots and a 75% automated ceiling; target verification checks the effective generated M13 configuration so a stale build cannot pass under an old layout | `check_effective_sdkconfig.py`, `check_firmware_size.py`, fresh target build | B-pass, Guardrail |
| local credential failure paths remain coherent | mapped IPv4 peers retain per-client throttling; allocation-free bounded error envelopes cover cJSON OOM; Wi-Fi/auth companion fields update through separate versioned single-entry NVS blobs; legacy auth migration rejects read/corruption and claimed-without-password states instead of installing the public initial password | legacy-migration policy cases in `test_http_security_policy`, D048/D049 source guardrails, preserved-NVS target boot and read-only key audit | H-pass, B-pass, T-pass preserved state; forced NVS-failure injection pending |

## M0 sign-off/reproducibility contracts

| Contract | Evidence | Validation |
|---|---|---|
| exact ESP-IDF baseline | direct CMake fatal gate and exact `idf.py --version` comparison require only `6.0.2` | B-pass |
| one strict project C++ baseline | host CMake uses C++20 without extensions; target replaces GNU++26 with `-std=c++20` | H-pass; `tools/check_target_compile_commands.py` B-pass |
| unknown host-test group is rejected | `smoker_v0_tests` validates its argument; `tools/verify.sh --host-only` executes the negative check | H-pass |
| configuration resource limits are not invented at M5 | M5 accepts trusted startup configuration; device probe capacity is M9 and persisted-input bounds are M10 | Guardrail; Deferred M9/M10 |
| CI inputs are reproducible | explicit Ubuntu runner and immutable action commit SHAs | CI configuration review; no target-runtime claim |

## Remaining validation gates

The following cannot be closed by M0-M5:

- identify sensor/probe frontends, SSR/power interface, final external pin
  assignments, and independent safety protection at M6B;
- validate real authoritative sensor behavior at M7;
- validate real heater output and electrical safe state at M8;
- validate persistence, reset reason, and `resumeAfterPowerFailure` at M10;
- validate M12 AP/STA provisioning, automatic captive opening, real scan and
  hidden-SSID/wrong-password fallback, mDNS, authentication, Wi-Fi-loss
  isolation, ten-minute runtime, affinity/watermark, and TWDT on the final board;
- validate M13 live GitHub check/install, both-slot boot, five-cycle mark-valid,
  reset-during-`PENDING_VERIFY` rollback, and final reinstall on KFB003.
