# M0-M15 Requirements Traceability

Status: **M15 software, including atomic one-message remote Start and
fail-closed legacy rejection, is host/sanitizer and ESP-IDF cross-build
validated; manual Blynk Console migration remains pending. The previous Console
protocol and KFB003 provisioning, live TLS/status/commands, reboot no-replay,
firmware check, and remote-error e-mail delivery passed;
M7 is complete for its defined MAX31865 software integration and connected
ordinary-runtime functional activation after host/API validation, connected
SPI/configuration/raw/shutdown T-pass, and a signed three-reading 179-second
ordinary target run;
the preserved first board diagnostic failed at pull-following MISO before a
later corrected setup succeeded; the inactive M9 staged one-or-two-ADS1115
sequencer and explicit i2cdev ownership are host/source-tested and API cross-
built. One installed device passed connected
GPIO17/18, `0x48`, register, and floating-A0 single-shot checks; a later
temporary direct-I2C diagnostic preserved an initial A3 wiring failure and then
showed corrected room/heating response through A3 only. Production probe
acquisition remains simulated, A0-A2 analog behavior and calibration remain
pending, and the last known board image is the temporary A3 diagnostic;
the first inactive M8 PID slice is host-tested and API cross-built
while production demand remains deterministic 100/0 and heater output simulated;
phone push receipt, native mobile layout, exact broker timing,
deliberate transport loss, the M14 Wi-Fi-loss scenario, M12 radio edge cases,
and all remaining external-sensor qualification/hardware-safety evidence remain
pending**

This matrix separates implementation status from validation strength. A rule is
not considered target- or hardware-validated merely because host tests or an
ESP-IDF cross-build pass.

## Validation levels

- **H-pass** — native host behavior test passes; sanitizer coverage is also run
  for the complete host suite.
- **B-pass** — ESP-IDF v6.0.2 cross-build for the ESP32-S3 family passes.
- **T-pass** — behavior was executed and observed on the connected target.
- **T-fail** — behavior was executed on the exact target but a required check
  failed; the observation is evidence of failure, not validation of the
  intended behavior.
- **T-pending** — behavior must be executed on the exact target board.
- **HW-pending** — electrical/thermal behavior requires identified hardware and
  an independent hardware test/review.
- **Guardrail** — an intentional absence/architecture constraint checked by
  source/dependency review and build, not a runtime product behavior.
- **Deferred Mx** — the rule belongs to a future roadmap milestone and M0-M5 do
  not claim implementation.

Test names below refer to functions in `tests/host/smoker_core_tests.cpp`,
`tests/host/smoker_v0_tests.cpp`, `tests/host/smoker_m7_tests.cpp`,
`tests/host/smoker_m8_tests.cpp`, `tests/host/smoker_m9_tests.cpp`,
`tests/host/smoker_m12_tests.cpp`,
`tests/host/smoker_m13_tests.cpp`,
`tests/host/smoker_m14_tests.cpp`, and `tests/host/smoker_m15_tests.cpp`. All
host test groups are registered in `tests/CMakeLists.txt`.

## Business rules

| Rule | Milestone/status | Implementation evidence | Test/evidence | Validation |
|---|---|---|---|---|
| BR-001 | M3 implemented | `SmokerApplication::process(StartSessionCommand)` rejects Start while running | `test_m3_session_and_snapshot` | H-pass, B-pass |
| BR-002 | M1/M5 implemented | separate chamber state and `ProbeRuntime`/`ProbeSnapshot` types | `test_m5_complete_slice` | H-pass, B-pass |
| BR-003 | M2 implemented | single `IChamberSensor` input used by control and safety | `test_m2`, `test_m4_invalid_and_latched_fault` | H-pass, B-pass |
| BR-004 | M5 implemented/M9 adapter inactive | probe configuration remains a vector; the inactive exact-pinned ADS1115 sequencer accepts one or two explicitly configured devices and unique logical mappings without creating a core capacity constant; the selected two-converter/six-probe product capacity remains physical M6B/M9 evidence | `test_m5_complete_slice`, `test_m5_validation_queue_and_combined_order`, `test_ads1115_invalid_incomplete_configurations_are_rejected`, `test_ads1115_one_device_sequencer_never_touches_device_one`; ESP-IDF API cross-build | H-pass, B-pass inactive adapter; real capacity HW-pending M6B/M9 |
| BR-005 | M5 implemented | `calculate_heater_demand()` accepts only chamber current/target; probe logic emits alarms only | `test_m5_complete_slice` | H-pass, B-pass |
| BR-006 | M5 implemented | `ProbeSnapshot` exposes all required properties | `test_m5_complete_slice` | H-pass, B-pass |
| BR-007 | M5 implemented/clarified | `evaluate_probe_state()` gates target alarms on RUNNING, enabled, alarm-enabled, and one latch per target/session | `test_m5_complete_slice`, `test_m5_same_cycle_command_semantics`, `test_m5_alarm_lifecycle_and_probe_defaults` | H-pass, B-pass |
| BR-008 | M3 implemented | `Recipe` owns exactly one `Stage` | `test_m3_session_and_snapshot` | H-pass, B-pass |
| BR-009 | M3 guardrail | session enum contains only Idle/Running/Stopped/Fault; labels remain strings | enum/source review plus ESP/host builds | Guardrail, B-pass |
| BR-010 | M3 implemented/clarified | `StageTimer` and `update_stage_timer()` support the three starts, wait for an enabled valid selected-probe sample, and execute one-way | `test_m3_timer_conditions`, `test_m3_probe_timer_availability` | H-pass, B-pass |
| BR-011 | M10 | no store/recovery implementation in M0-M5 | roadmap and absence review | Deferred M10 |
| BR-012 | M3 guardrail | recipe values drive control; no food-safety engine exists | source/dependency review | Guardrail, B-pass |

## History rules

| Rule | Milestone/status | Implementation evidence | Test/evidence | Validation |
|---|---|---|---|---|
| HR-001 | M14 implemented | `HistoryObservationMailbox` emits only START/RUNNING observations and terminal END; idle snapshots are not appended | `test_sampling_and_mailbox_saturation`; KFB003 retained-session readback | H-pass, B-pass, T-pass |
| HR-002 | M14 implemented | START/END and semantic changes publish on the first observed cycle; complete periodic records use a 60-second RUNNING-only interval; API stride never suppresses lifecycle/change records | `test_sampling_and_mailbox_saturation`, `test_pagination_and_stride`; KFB003 one-minute sessions and target/alarm transition | H-pass, B-pass, T-pass |
| HR-003 | M14 implemented/guarded | preallocated SPSC publication drops rather than waits; HistoryTask failure/flash coordination remains platform-only and has no command/heater path; lifecycle retry is allowed only before the log reports `FAILED`, after which the portable write policy enters fail-stop and invokes no further history flash work while preserving initialized-log health counters | `test_sampling_and_mailbox_saturation`, `test_control_output_is_independent_of_history_saturation`, `test_lifecycle_terminal_failure_stops_writes`, `test_ordinary_terminal_failure_stops_writes`, `test_flash_operation_serialization`; architecture guardrail | H-pass, B-pass, Guardrail; Wi-Fi-loss and injected target flash-failure T-pending |
| HR-004 | M14 implemented | exact 4 MiB log initializes random/empty media lazily, commits a whole-session eviction tombstone before multi-page erase, evicts completed sessions first, and marks a partition-filling retained session truncated | `test_empty_random_and_reboot`, `test_torn_and_corrupt_records`, `test_rollover_eviction_truncation_and_interruption` including reset after partial victim erase; partition and retained-session readback | H-pass, B-pass, Guardrail; T-pass partition/reconstruction |
| HR-005 | M14 implemented | records always carry monotonic `session_elapsed`; Unix UTC is optional and can first appear after START | `test_empty_random_and_reboot`; KFB003 samples after SNTP sync | H-pass, B-pass, T-pass |
| HR-006 | M14 implemented | reboot reconstruction marks a retained session without committed END interrupted and does not enter application recovery | `test_rollover_eviction_truncation_and_interruption` | H-pass, B-pass; T-pending |
| HR-007 | M14 implemented | only two read-only operational routes exist; strict bounded query parsers reject duplicate/unknown/malformed fields; commissioning rejection is retained | `test_strict_history_queries`; HTTP/browser fixtures; authenticated KFB003 200/400/404 responses | H-pass, B-pass, Guardrail; T-pass operational API |
| HR-008 | M14 implemented | durable uint64 history ID is reconstructed independently from application session ID and emitted as a JSON decimal string | `test_empty_random_and_reboot`; KFB003 history IDs 1--3 retained across reset | H-pass, B-pass, T-pass |

## Remote-access rules

| Rule | Milestone/status | Implementation evidence | Test/evidence | Validation |
|---|---|---|---|---|
| RA-001 | M15 implemented | Blynk/MQTT is platform-only, observes immutable snapshots, and has no heater/control dependency; service startup failure is non-fatal | `test_control_is_independent_of_blynk_transport`; architecture guardrail; KFB003 live online status and safe reboot | H-pass, B-pass, Guardrail, T-pass startup/live path; deliberate outage pending |
| RA-002 | M15 implemented/atomic Start hardened | callback allowlist crosses a reserved-Stop raw SPSC mailbox, stateless atomic Start mapping, a second application SPSC mailbox, and ControlTask-only round-robin submission; deprecated Start names fail closed and results are correlation-filtered | `test_allowlisted_deterministic_command_mapping`, `test_atomic_start_mapping_and_strict_parser`, `test_legacy_start_protocol_fails_closed`, `test_atomic_start_feedback_and_correlation`, `test_raw_mailbox_stop_reservation_and_concurrency`, `test_shared_ids_wrap_concurrency_and_fair_drain`, `test_results_and_events_are_separate_and_not_replayed`; historical KFB003 legacy Blynk Start/Stop results | H-pass, B-pass, Guardrail; atomic Console/target path T-pending |
| RA-003 | M15 implemented | one complete connect projection plus normalized equality, five-second minimum, newest-value coalescing, retry, and unchanged silence | `test_projection_connect_throttle_coalescing_and_retry`, `test_status_timer_normalization_and_serializer_budget`; KFB003 initial live status | H-pass, B-pass; T-pass initial projection, exact broker timing/silence pending |
| RA-004 | M15 implemented | `LastCommandResult` and five per-type throttled/coalesced events are separate from the 15-field bounded `batch_ds` projection | `test_status_timer_normalization_and_serializer_budget`, `test_results_and_events_are_separate_and_not_replayed`; enabled Console events, live malformed-command trigger, owner-confirmed e-mail | H-pass, B-pass; T-pass event/e-mail, phone push pending |
| RA-005 | M15 implemented/hardened | clean session and no get/sync/retained publish; Start has no retained cross-message parameter; independent disconnect and connection generations force cleanup before reconnect activation; both Blynk command stages discard stale generations; results/feedback/events and the old drop watermark are cleared while new-generation input remains live | `test_atomic_start_mapping_and_strict_parser`, `test_disconnect_reconnect_boundary_discards_old_connection_state`, `test_translated_commands_do_not_cross_reconnect_boundary`, `test_results_and_events_are_separate_and_not_replayed`, `test_raw_mailbox_stop_reservation_and_concurrency`; MQTT source guardrail; historical KFB003 reboot after Stop | H-pass, B-pass, Guardrail; atomic reconnect target scenario pending |
| RA-006 | M15 implemented | Blynk mapper emits only check/install intent into the existing fixed-source M13 service; it carries no URL/image | `test_allowlisted_deterministic_command_mapping`, `test_application_update_permission`; D050/D054/D058 guardrail; KFB003 Blynk firmware check | H-pass, B-pass, Guardrail, T-pass check; install release-gated |

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
| CR-006 | M2 production/M8 adapter inactive | `IChamberController` now owns the application request boundary; production explicitly composes the deterministic 100/0 adapter, while exact-pinned `pid_ctrl` 0.3.1 float code is target-only and uncomposed; requested demand remains before synchronous safety/gate/write | `test_m2`, `test_deterministic_production_adapter_preserves_m2_behavior`, focused M8 configuration/error/reset/failure/safety tests; ESP-IDF API cross-build | H-pass, B-pass inactive adapter, Guardrail; real cadence/tuning/SSR HW-pending M6B/M7/M8 |

## Safety rules

| Rule | Milestone/status | Implementation evidence | Test/evidence | Validation |
|---|---|---|---|---|
| SF-001 | M4 implemented/P0 guarded | safety evaluated synchronously before sole final heater write | `test_m4_invalid_and_latched_fault`, `test_p0_cr_005_heating_state_invariants`; `tools/check_architecture.py` write-path check | H-pass, B-pass; T-pending |
| SF-002 | M4 implemented; M7 authoritative adapter active | absent authoritative reading raises ChamberSensorInvalid with no last-known fallback; exact-pinned backend maps not-ready, driver/fault, non-finite, and finite out-of-range outcomes to absence; sensor bootstrap failure continues into an observable first-IDLE-tick FAULT runtime | `test_m4_invalid_and_latched_fault`, `test_max31865_initialization_failure_while_idle_latches_safety_fault`, `test_max31865_valid_then_out_of_range_latches_without_cached_temperature`, `test_max31865_later_driver_failure_latches_without_cached_temperature`; exact ordinary target build and source guardrail | H-pass, B-pass activation, Guardrail; M7 functional activation T-pass; controlled physical faults and recovery remain M6B/pre-heater qualification |
| SF-003 | M4 implemented for configured simulation limit | safety evaluation faults above maximum; target validation rejects above it | `test_m4_over_temperature_and_limits` | H-pass, B-pass; physical value HW-pending |
| SF-004 | M0/M4 partial | constructor writes OFF; config/sensor/safety validated before final demand | `test_m2`, M4 tests, config-invalid M5 test | H-pass, B-pass; reset reason/recovery Deferred M10 |
| SF-005 | M5 configured/M6A target-validated | platform runtime subscribes/resets TWDT; defaults set 5 s and panic/reset | target diagnostic deliberately stalled `ControlTask` for 7 s; TWDT identified the task, panicked/reset, and next boot reported watchdog reset reason; normal image restored | B-pass, T-pass |
| SF-006 | M10 | reset reason and recovery policy are absent by design | roadmap/absence review | Deferred M10 |
| SF-007 | M4 implemented | fault persists after signal recovery; clear requires resolved condition and leaves session stopped | `test_m4_invalid_and_latched_fault` | H-pass, B-pass |
| SF-008 | M5 implemented/M9 adapter inactive | food probe disconnect/invalid input creates absence/alarm only; per-probe ADS1115 failures clear only that cache and do not change chamber fault or heater demand | `test_m5_complete_slice`, `test_m5_alarm_lifecycle_and_probe_defaults`, `test_ads1115_per_probe_failures_clear_only_the_affected_sample`, `test_ads1115_missing_or_invalid_food_probe_never_changes_chamber_control` | H-pass, B-pass inactive adapter |
| SF-009 | M12 implemented/target pending | HTTP uses bounded mailbox/snapshot transports; only ControlTask submits and connectivity failure does not gate its creation | `test_mailbox_concurrency`, `test_snapshot_exchange_concurrency`; architecture review | H-pass, B-pass; T-pending Wi-Fi-loss run |
| SF-010 | M4 implemented/P0 guarded | only `SmokerApplication` writes heater; every tick output passes `apply_safety_gate()` | `test_p0_sr_003_manual_stop_is_off_barrier`, `test_p0_cr_005_heating_state_invariants`; `tools/check_architecture.py` write-path check | H-pass, B-pass; T-pending |
| SF-011 | M6B external-hardware gate | explicitly no external safety-hardware implementation/claim | `docs/HARDWARE.md` M6B checklist | HW-pending M6B |
| SF-012 | future | no current-sensing/stuck-ON simulation exists | source/hardware capability review | Deferred, HW-pending |

## OTA rules

M13 implements the software OTA boundary with simulated-I/O and cross-build
evidence. A connected KFB003 additionally passed anonymous public-release
download, two-slot boot, mark-valid, forced rollback, and final reinstall.

| Rule | Milestone/status | Implementation evidence | Test/evidence | Validation |
|---|---|---|---|---|
| OTA-001 | M13 implemented | OTA helpers/service remain in `smoker_platform`; app owns only internal permission commands and interlock; core has no OTA/platform include | `test_semantic_versions_and_descriptors`; `tools/check_architecture.py` layer checks | H-pass, B-pass, Guardrail |
| OTA-002 | M13 implemented | immutable snapshot admission plus correlated Prepare rejects RUNNING; dedicated atomic Prepare is ordered before Finish so timeout cannot orphan a delayed reservation; Start is blocked while update active | `test_application_update_permission` Stop/Prepare/Finish FIFO regression, `test_update_coordinator`; source guardrail; stopped-session public-release install on KFB003 | H-pass, B-pass, Guardrail; T-pass stopped-session install path |
| OTA-003 | M13 implemented | manual image-prefix check is independent of application reservation; real chip ID/project/version admission, HTTPS-only bounded redirects, guarded 4,096-byte request TX buffer, total deadlines, non-RUNNING install permission, and ESP-IDF RSA-3072 signed-update verification are enforced; the public fixed source requires no device credential, while release signing is isolated from ordinary CI and checked against the versioned public key; D051 makes the missing independent reviewer conditional on sole-maintainer repository/tag control | `test_application_update_permission`, `test_semantic_versions_and_descriptors`, `test_image_metadata_and_deadlines`, `test_update_coordinator`; effective-Kconfig/signing/serial source guardrails; anonymous `v0.13.0` download and real GitHub redirect on KFB003 | H-pass, B-pass, Guardrail; T-pass live signed image and redirect; live tamper/timeout not executed |
| OTA-004 | M13 implemented | exact custom table has `otadata` and equal `ota_0`/`ota_1` 3 MiB slots within confirmed 16 MiB flash; rollback config enabled; signed serial preflight rejects stale configuration, unsigned/build-mismatched apps, and mismatched generated layouts; ordinary unsigned/partial ESP-IDF flash targets fail closed | `test_update_coordinator`; `tools/check_effective_sdkconfig.py`, `tools/flash_signed_firmware.py --check-only`, `tools/check_partitions.py`, blocked ESP-IDF flash-target validation, ESP-IDF partition output; signed KFB003 USB migration, `ota_0` to `ota_1` install, forced rollback, and final reinstall | H-pass, B-pass, Guardrail; T-pass USB migration and both-slot rollback path |
| OTA-005 | M13 implemented | an actual OTA-installed `PENDING_VERIFY` image blocks Start and requires five safe TWDT-reset cycles before mark-valid; an observable sensor-bootstrap FAULT, any later fault, timeout, or mark error rolls back through normal validation; runtime-context/ControlTask/OtaTask critical bootstrap failure invokes immediate rollback reboot. Blank serial initial metadata instead selects `ota_0` directly as `VALID` under ESP-IDF 6.0.2 and does not weaken this OTA contract | `test_application_update_permission`; M7 IDLE initialization-failure host test; `tools/check_architecture.py` pending-bootstrap/source-order guardrails; forced pending-image reset and clean reinstall on KFB003; reviewed ESP-IDF blank-metadata selection path | H-pass, B-pass, Guardrail; T-pass rollback and five-cycle mark-valid for real pending images; sensor-faulting pending image target execution separately pending |
| OTA-006 | M13 implemented | static low-priority core-0 OtaTask owns bounded SNTP/HTTPS/flash operations and is outside TWDT; its stack/TCB are internal-DRAM objects, not part of the PSRAM-eligible heap service; ControlTask exchanges ordered bounded atomic signals/snapshots only; unavailable worker reports `FAILED` and rejects work | `test_image_metadata_and_deadlines`, `test_update_coordinator`; `tools/check_architecture.py` task-placement/order/availability checks; live check/install on KFB003 | H-pass, B-pass, Guardrail; T-pass normal install; T-pending Wi-Fi-loss install |

## Active M7 MAX31865 ordinary-runtime contracts

These checks separate platform behavior/build activation from connected
functional and still-pending physical evidence. The first connected failure is
preserved alongside the corrected diagnostic and ordinary-runtime T-pass. M7
is complete for its defined integration/activation; M6B and pre-real-heater/
release hardware qualification remain incomplete.

| Contract | Implementation evidence | Test/evidence | Validation |
|---|---|---|---|
| hardware and policy values preserve their evidence class | ordinary configuration centralizes SPI2/GPIO12/11/13/10, checked MISO pull-up, 100 kHz, PT100/three-wire, 50 Hz, ITS-90, provisional 430-ohm Rref, inclusive -50..+200 C validity, active `0xD1`, terminal `0x11`, and 66 ms boundary; source comments distinguish supplier/maintainer facts, T-pass observations, and operational choices | conversion and validity-policy tests; source guardrail; `docs/HARDWARE.md` dossier and connected logs | H-pass, B-pass, Guardrail, functional T-pass; range is supplier documentation rather than calibration; fitted Rref, continuity, calibration HW-pending |
| first-conversion freshness is explicit | configuration reports `ConfiguredAwaitingFirstSample`; a fake-clock-tested policy blocks before 55 ms at 60 Hz and 66 ms at 50 Hz, accepts exactly at each boundary, and resets after successful configuration, reinitialization, and fault recovery | `test_max31865_60_hz_first_conversion_boundary`, `test_max31865_50_hz_first_conversion_boundary`, `test_max31865_reconfiguration_resets_readiness_without_reuse`, `test_max31865_reinitialization_resets_readiness`, `test_max31865_fault_recovery_requires_fresh_current_value`; source ordering guardrail | H-pass, B-pass, Guardrail; module settling and target timing HW-pending |
| POR/stale values cannot cross an early read | explicit `NotReady` maps to absence; target readiness is checked before fault/temperature reads; repeated early reads neither touch emulated registers nor reuse a prior sample | `test_max31865_por_value_is_not_exposed_before_readiness`, `test_max31865_reconfiguration_resets_readiness_without_reuse` | H-pass, B-pass, Guardrail |
| every current result maps independently | adapter returns `Temperature` only for a current finite in-range backend result; bounds must be finite/ordered, exact endpoints pass, just-outside/raw-zero-like/NaN/infinity fail, initialization/configuration/read/fault failures are absent, and no last value is stored | `test_max31865_temperature_validity_policy_is_finite_ordered_and_inclusive`, `test_max31865_temperature_validity_accepts_boundaries_and_rejects_outside`, `test_max31865_initialization_and_configuration_failures_are_absent`, `test_max31865_read_policy_never_reuses_a_previous_value` | H-pass, B-pass, Guardrail |
| existing safety remains authoritative and boot faults are observable | bus/pull, descriptor/configuration, or boundary failure leaves the sensor unavailable until reboot but still starts ControlTask/services; the first IDLE tick or a later invalid result raises and latches `ChamberSensorInvalid`, commands heater OFF, and exposes no cached/fabricated temperature; pending OTA cannot count a safe cycle and rolls back on the fault | premature-tick, IDLE initialization-failure, valid-then-out-of-range, and later-driver-failure M7 tests; startup/OTA source guardrail | H-pass, B-pass, Guardrail; boot-fault and controlled fault target injection T-pending |
| project-owned read code has no explicit wait/allocation | common and target read code contains no explicit delay, task creation, heap allocation, or `max31865_measure()` call | `test_max31865_read_is_observed_allocation_free`; `tools/check_architecture.py` | H-pass ordinary-C++ allocation observation, Guardrail; ESP-IDF/driver allocation and real SPI worst-case blocking unproven |
| ordinary composition and lifecycle are explicit | `ordinary_runtime.*` target-only RAII initializes bus then checked GPIO13 pull-up before descriptor access; backend rejects a bus it does not own; successful exact `0xD1` write/readback and >=66 ms real-clock wait precede ControlTask; checked terminal `0x11` and descriptor removal precede bus free and floating pull cleanup; composition retains simulated food/heater and deterministic controller, with PID/SSR absent | ordinary target build, ELF/compile-database/source guardrails; signed serial target cycles 1/60/180 at 25.7/25.7/25.8 C over about 179 seconds with no chamber/control failure | B-pass activation, Guardrail, functional T-pass; longer-duration behavior and physical regulation remain M6B/M8 qualification |
| serial activation and OTA state are classified correctly | the signed 1,445,888-byte image has SHA-256 `4b1541202eaa7388b79c48f9f615fd7443959b5ad943f12652e3cfa4ea95ffcc`; blank all-`0xff` OTA metadata in the no-factory layout selects `ota_0` directly as `ESP_OTA_IMG_VALID`, while only selected `ESP_OTA_IMG_NEW` transitions to `PENDING_VERIFY`; no pending state is created or forced | preserved artifacts/logs and ESP-IDF 6.0.2 `bootloader_utility.c`; cycles 1/60/180 satisfy the intended at-least-120-second functional observation without exact cycle 120 | T-pass serial activation; five-cycle criterion waived only here; OTA-005 preserved for actual pending images |
| board diagnostic is isolated, quiescent at ownership release, and evidence-bounded | Kconfig defaults OFF and remains compile-time exclusive; exact software/driver access, bounded ten-sample reporting, and descriptor-before-bus cleanup are unchanged | architecture guardrail; separate ordinary/diagnostic builds and ELF isolation; preserved connected logs/hashes in the connected plan | T-fail preserved for first pull-following run; corrected T-pass for pull independence, `0x00`/`0x91`/`0xD1`, ten raw 8548/8549 zero-fault samples, zero transaction/sensor-fault counts, and both terminal `0x11`; raw-zero/`0x40` is observation only; calibration/controlled fault/physical quiescence HW-pending |

## Inactive M8 PID software-integration contracts

These checks cover the application controller boundary, a platform PID policy,
and ESP-IDF 6.0.2 source/API compatibility only. Production still uses the
deterministic adapter and simulated heater. M7's sensor-integration prerequisite
is satisfied; M6B, M8, and M9 remain incomplete, and no row is cadence, tuning,
SSR, thermal, electrical, or independent-safety evidence.

| Contract | Implementation evidence | Test/evidence | Validation |
|---|---|---|---|
| dependency is exact and reproducible | platform manifest pins `espressif/pid_ctrl ==0.3.1`; lock records hash `974be066...d4979`, mandatory `espressif/iqmath` 1.11.0~1 hash `39448db7...1b9d`, and IDF 6.0.2; the optional source check fails cleanly on partial/corrupt generated inputs while remaining offline when all managed components are absent | Component Manager resolution; lock/optional managed-source architecture guardrail and negative fixtures | B-pass, Guardrail |
| application boundary is typed and platform-free | injected synchronous `IChamberController` accepts authoritative/target `Temperature`, returns optional typed `HeaterDemand`, and reports reset failure; no PID/ESP-IDF type enters core/app | explicit construction-site build coverage; `tools/check_architecture.py` | H-pass, B-pass, Guardrail |
| form-specific configuration and error sign are explicit | project temperature/demand and ESP32-S3 hardware evidence favor float; common adapter calculates `target - measured`; positional requires finite ordered accumulated-error bounds containing zero; incremental exposes none and rejects contradictory bounds; common normalized output bounds remain explicit | `test_valid_positional_configuration_with_accumulated_error_bounds`, `test_invalid_common_and_positional_configurations`, `test_valid_incremental_configuration_has_no_integral_bound_promise`, `test_incremental_configuration_rejects_contradictory_positional_bounds`, `test_target_minus_measurement_and_normalized_output` | H-pass, B-pass; neither form/gains production-selected |
| upstream form semantics remain exact | positional accumulates/clamps raw per-call error then applies Ki; incremental ignores accumulator/bounds and retains/clamps output; target maps ignored incremental fields to `0/0`; both differentiate error with implicit per-call gains and no derivative filtering/on-measurement | optional exact-pinned managed-source semantic guardrail; target mapping source check | Guardrail; setpoint-kick/cadence/form tuning HW-pending |
| backend failures/results fail closed | initialization/compute/reset failure, non-finite output, and output outside configured normalized bounds are explicit failure rather than valid OFF/on demand | `test_backend_initialization_failure`, `test_backend_compute_and_output_failures`, `test_reset_failure_and_steady_allocation_without_destructor_reset` | H-pass, B-pass |
| lifecycle ownership and steady paths are bounded | target-only non-copyable RAII owner calls exact float create/compute/reset/delete APIs; wrapper destruction relies on backend RAII release rather than an ignored reset; upstream `calloc` is confined to creation; valid reviewed compute/reset and project paths have no intentional allocation/task/I/O/log/delay/wait/lock | allocation observation in `test_reset_failure_and_steady_allocation_without_destructor_reset`; managed-source and project-source guardrails; API cross-build | H-pass ordinary-C++ observation, B-pass, Guardrail; target worst-case timing unproven |
| reset/failure/safety ordering remains authoritative | construction writes observable OFF before the first controller reset callback without substituting for real-driver safe initialization; request/reset share a no-I/O/wait/block/task/steady-allocation contract; effective transitions from eligible RUNNING reset; request precedes safety, gate, and sole write; compute/reset failure latches `ControlLoopFailure`; clear leaves STOPPED and Start is explicit | `test_constructor_writes_observable_off_before_first_controller_reset`, `test_application_off_reset_and_stop_lifecycle`, `test_invalid_measurement_fault_resets_and_never_resumes`, `test_compute_failure_latches_and_requires_clear_then_start`, `test_reset_failure_fails_closed_and_can_only_resolve_latched_fault`, `test_safety_overrides_positive_request_before_only_write`, `test_firmware_update_interlock_never_calls_controller` | H-pass, B-pass, Guardrail; real heater/safety HW-pending |
| same-tick target commands use existing final-state semantics | non-Stop commands drain before one control evaluation; remove then restore leaves a present final target, while RR-003 forces OFF only when target is absent at evaluation; SR-003/D031 define the sole explicit command-batch OFF barrier for accepted manual Stop | `test_same_tick_target_removal_and_restoration_uses_final_state`; BR/RR/SR/D031 review | H-pass, B-pass |
| production controller/output behavior remains deterministic and simulated | `RuntimeContext` in `ordinary_runtime.cpp` constructs the real `Max31865ChamberSensor`, `SimulatedFoodProbeSource`, `DeterministicChamberController`, and `SimulatedHeaterOutput`; PID backend and SSR output are absent from `main`/runtime composition | `test_deterministic_production_adapter_preserves_m2_behavior`, `test_m2`; architecture guardrail | H-pass, B-pass, Guardrail |
| autotuning and production approval claims are absent | reviewed 0.3.1 has float/IQmath and positional/incremental forms, but no autotuning, plant identification, sample period, `dt`, derivative filter, or derivative-on-measurement; no form, tuning code/default/result is approved | optional managed-source semantic guardrail; D055/roadmap/source review | Guardrail; future hardware-backed tuning decision |

## Inactive M9 staged ADS1115 software-integration contracts

These checks cover one platform software sequencer, ESP-IDF 6.0.2 source/API
compatibility, the separately authorized digital bring-up of the installed
physical ADC, and later response evidence through A3 only. M6B and M9 remain
incomplete and production uses simulated food probes. The connected rows keep
digital, A3-response, terminal-state, and production-integration evidence
separate; none is calibration or accuracy evidence.

| Contract | Implementation evidence | Test/evidence | Validation |
|---|---|---|---|
| all unknown physical values stay explicit | one or two device records require port, SDA/SCL, clock, pull-up policy, and address; zero/more-than-two, unconfigured-device channels, configured devices without channels, duplicate probe IDs, and duplicate mux-per-device mappings are rejected; channels require probe/device map, mux, gain, and rate; timeout and sample maximum age have no defaults; raw conversion requires an injected calibration/validity policy | `test_ads1115_invalid_incomplete_configurations_are_rejected`, `test_ads1115_one_device_sequencer_never_touches_device_one`; source guardrail | H-pass, B-pass, Guardrail; physical values/calibration HW-pending |
| installed ADS1115 responds digitally | maintainer-reported 3.3 V/GND, GPIO17 SDA, GPIO18 SCL, ADDR=GND (`0x48`), ALERT/RDY open; 100 kHz diagnostic read reset config/thresholds, wrote/read the 16-bit configuration word `0xC383`, completed one A0 single shot, and restored/read terminal `0x8583`; A0..A3 were floating | connected ESP-IDF 6.0.2 `i2c_tools` diagnostic on 2026-08-25 | T-pass digital communication only; module identity, pull-ups, known-voltage, `NTC100`, calibration, accuracy, disconnect/short, sustained behavior, and production activation pending |
| A3 connected path responds after a preserved wiring failure | four nominal 100 kOhm 0.1%/100 nF high-side-divider networks were reported assembled, but only A3 was exercised with an NTC. Initial `0xF383` run stayed raw about -4..-2 and -0.0005..-0.0003 V; after correction, 20 room samples moved 18329 -> 18040, 2.2911 -> 2.2550 V, and nominally 227.10 -> 215.79 kOhm; uncontrolled soldering-tool heating moved overall 8300 -> 1174, 1.0375 -> 0.1468 V, and nominally 45.86 -> 4.65 kOhm with a small reversal | maintainer-provided, hash-verified local session transcript and hash-verified temporary source; direct ESP-IDF `i2c_master` diagnostic with internal pulls | Initial T-fail wiring observation preserved; corrected A3 response T-pass only. A0-A2, actual rail/resistor values, temperature, R25/Beta/curve, calibration, and accuracy remain pending |
| A3 procedure and production boundaries remain explicit | temporary diagnostic did not use `Ads1115TargetBackend`, pinned `i2cdev`, M9 sequencer, production composition, ControlTask timing, or sustained acquisition. It wrote `0x8583` then immediately read `0x0583`, so terminal idle was not proved; the last known board image is that diagnostic. Ordinary source still composes `SimulatedFoodProbeSource` | hash-verified transcript and temporary-source inspection plus production composition guardrail | Guardrail; A3 idle poll/readback remediation, backend/sequencer integration, timing, production activation, and ordinary-image restoration remain pending; separate 2026-08-25 terminal `0x8583` T-pass retained |
| initial use and recovery prove each configured ADC idle | every configured device begins unsynchronized; only `busy=false` synchronizes one device, discards any stale result, and returns without same-step configure/start/read; busy/error devices remain quarantined while round-robin progress reaches another configured ADC; one-device sequencing never calls index 1 | `test_ads1115_one_device_sequencer_never_touches_device_one`, `test_ads1115_both_devices_require_initial_idle_synchronization`, `test_ads1115_initial_stale_result_is_discarded_before_later_restart`, `test_ads1115_recovery_requires_idle_and_never_reads_or_restarts_same_step`, `test_ads1115_quarantined_device_does_not_block_the_other_adc` | H-pass, B-pass, Guardrail; externally powered reset behavior target-pending |
| one owner preserves latched channel provenance | the fake and sequencer distinguish configured from in-flight mux/gain/rate/probe state; later configuration or a start while busy cannot replace in-flight provenance; timeout/ambiguous-start/busy-error results are never read, reconfigured over, or attributed to the next channel; the fixture includes consecutive device-0 channels | `test_ads1115_fake_latches_in_flight_provenance_across_reconfiguration`, `test_ads1115_timed_out_conversion_cannot_be_reconfigured_or_misattributed`, `test_ads1115_ambiguously_failed_start_is_quarantined_and_discarded`, `test_ads1115_busy_observation_error_uses_the_same_quarantine_boundary` | H-pass, B-pass, Guardrail |
| readiness precedes timeout without synchronous wait | each active step observes busy once before the deadline; ready at or after the deadline is accepted because polling does not reveal completion time; still busy at/after it clears only the active sample and quarantines that ADC without `get_value`; the configured deadline still exceeds the TI `1 / (0.9 * DR)` floor | `test_ads1115_ready_exactly_at_deadline_is_accepted`, `test_ads1115_ready_after_deadline_is_accepted`, `test_ads1115_busy_exactly_at_deadline_quarantines_without_read`; source guardrail | H-pass, B-pass; real service cadence/I2C timing target-pending |
| cached reads are independent and age-bounded | `service()` alone performs backend work; `read(probe_id)` only checks the timestamped cache; first/missing/failed/expired samples are absent and one failure does not invalidate unrelated probes | `test_ads1115_per_probe_failures_clear_only_the_affected_sample`, `test_ads1115_cached_readings_expire_and_unknown_ids_are_absent` | H-pass, B-pass |
| failure classification preserves the narrowest safe boundary | start/busy ambiguity quarantines only that ADC; pinned 1.1.14 configuration writes cannot start conversion, ready-then-get proves idle, and calibration/validity affects only the probe; all clear only the attempted/active cache | `test_ads1115_per_probe_failures_clear_only_the_affected_sample`, `test_ads1115_idle_configure_get_and_calibration_failures_do_not_quarantine`; exact pinned `write_conf_bits()` source guardrail | H-pass, B-pass, Guardrail |
| monitoring inputs remain outside chamber control | acquisition/recovery/calibration failures become affected-probe absence only; missing/invalid food inputs do not create a chamber fault or change heater demand | `test_ads1115_missing_or_invalid_food_probe_never_changes_chamber_control` | H-pass, B-pass; physical disconnect behavior HW-pending |
| project steady paths are ordinary-C++-allocation quiet | configuration/cache storage allocates during initialization; measured fake-backend start/poll/read paths contain no repeated allocation and project sources contain no delay/task creation | `test_ads1115_steady_state_service_and_read_are_observed_allocation_free`; `tools/check_architecture.py` | H-pass ordinary-C++ observation, Guardrail; driver/ESP-IDF allocation and boundedness unproven |
| pinned backend is real but inactive | target-only RAII uses fixed two-slot storage but initializes/accesses/releases only the configured one or two descriptors; it calls init/free, single-shot mode, mux, gain, rate, start, busy, and value APIs, and explicit clock/pull-ups overwrite init values before first I2C I/O; runtime/main retain `SimulatedFoodProbeSource` and construct no backend/owner | ESP-IDF target compilation, one-device host call trace, and architecture guardrail | H-pass sequencing, B-pass API compatibility, Guardrail; connected backend and ControlTask placement target-pending |
| locked i2cdev ownership and cleanup boundary are explicit | direct exact `esp-idf-lib/i2cdev ==2.1.2` remains locked at hash `ad8981cc...ce3`; target-only non-copyable owner calls real init and backend requires its active lease. Backend success means every `ads111x_free_desc()` returned `ESP_OK`; subsystem success means `i2cdev_done()` returned `ESP_OK`; swallowed nested cleanup failures and physical/driver quiescence remain unproved. Member state rejects same-owner restart but not another owner; activation requires exactly one composition/initialization per boot and forbids restart after real release without a demonstrably restartable future pin. No project-global mutable state is added | manifest/lock audit, optional exact managed-source cleanup/lifecycle guardrail, target API cross-build, and lifecycle call-site/composition search | B-pass, Guardrail; production composition and connected timing remain inactive/pending |
| shared-bus consistency is validated without overconstraining separate buses | same port requires equal pins/clock/pull-up policy and distinct addresses; separate non-overlapping ports may reuse an address; both selected devices require at least one mapped channel | configuration host tests and source guardrail | H-pass, B-pass; actual bus topology HW-pending |

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
| control readiness is owned by the completed control cycle | readiness starts false; startup code never publishes it; `ControlTask` uses a portable one-shot latch only after `tick()` has completed safety/final output, the immutable snapshot publication succeeds, and TWDT reset returns `ESP_OK`. The latch has no session/fault/temperature/heater inputs, so a published `FAULT` remains observable; Wi-Fi/history/Blynk state cannot gate or reset it | `test_control_readiness_latch`; `tools/check_architecture.py` ownership/order/bounded-path guardrails | H-pass, B-pass, Guardrail; target handshake timing not executed |
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

## M14 history/runtime contracts

| Contract | Implementation evidence | Test/evidence | Validation |
|---|---|---|---|
| history cannot enter the critical dependency chain | only post-tick `SmokerSnapshotView` crosses a preallocated SPSC mailbox; ControlTask contains no history flash, query, mutex, wall-clock, or logging call | `test_sampling_and_mailbox_saturation`, `test_control_output_is_independent_of_history_saturation`; `tools/check_architecture.py` | H-pass, B-pass, Guardrail |
| critical history publication avoids allocation and 64-bit atomic helpers | probe/alarm vectors are reserved before task start; mailbox sequences/drop counter are uint32 atomics and publication drops on saturation | `test_sampling_and_mailbox_saturation`; target archive atomic-helper audit | H-pass, B-pass; T-pending |
| raw log survives incomplete writes and eviction | page/record CRC and commit-last markers retain committed prefixes; a failed runtime program abandons the tail; a committed eviction tombstone hides the whole victim until cleanup finishes | `test_torn_and_corrupt_records`; injected mid-eviction erase failure and reboot in `test_rollover_eviction_truncation_and_interruption` | H-pass, B-pass; T-pending power interruption |
| history lifecycle remains complete under same-cycle termination and transient writes | a first-observed STOPPED/FAULT session atomically publishes START+END; HistoryTask retains failed START/END ahead of later observations only while log storage is not terminal `FAILED` | direct-terminal/saturation cases in `test_sampling_and_mailbox_saturation`; `test_start_transient_failure_is_retried`, `test_end_transient_failure_is_retried`; source guardrail | H-pass, B-pass, Guardrail |
| terminal runtime storage failure is fail-stop | the portable HistoryTask write policy uses `CircularHistoryLog::health().state` as its sole terminal authority, releases pending lifecycle state, refuses later observations, and never invokes its writer again; runtime health keeps initialized-log counters and local control remains independent | `test_lifecycle_terminal_failure_stops_writes`, `test_ordinary_terminal_failure_stops_writes` assert stable read/write/erase and writer-invocation counters across extra cycles; extended `test_control_output_is_independent_of_history_saturation`; architecture guardrail | H-pass, B-pass, Guardrail; injected target flash-failure T-pending |
| queries retain bounded result growth | samples scan one 4 KiB page at a time, retain at most requested records, paginate at 60, and apply stride only to periodic SAMPLE records | `test_strict_history_queries`; HTTP fixture pagination | H-pass, B-pass, Browser-pass |
| history and OTA do not overlap flash ownership | shared platform coordinator gives OTA deferral/ownership and keeps acquisition within install deadline; no coordinator call exists in ControlTask | `test_flash_operation_serialization`; source guardrail | H-pass, B-pass, Guardrail; T-pending |
| HistoryTask is non-critical and static | one static 12 KiB internal-DRAM task/TCB is pinned to core 0 at low priority and is not subscribed to TWDT | source/build guardrail; KFB003 serial affinity/watermark/write instrumentation | B-pass, Guardrail, T-pass |
| history UI/API remain local and read-only | two authenticated operational GET routes, commissioning rejection, strict queries, no external assets, full pagination through terminal END, and a 1,200-point browser budget that drops SAMPLE first | HTTP fixture and Playwright dense-change/terminal-END plus responsive/pagination checks; KFB003 authenticated history reads | H-pass, B-pass, Browser-pass; T-pass operational API |
| partition migration is explicit | preserved 24 KiB NVS plus dual 3 MiB OTA and exact 4 MiB history partition; ordinary unsigned/partial flash remains blocked | `tools/check_partitions.py`, signed-helper preflight, ESP-IDF build; signed KFB003 readback/NVS comparison | B-pass, Guardrail, T-pass |

## M15 Blynk/runtime contracts

| Contract | Implementation evidence | Test/evidence | Validation |
|---|---|---|---|
| Blynk cannot own or block control | platform-only task/callback and two SPSC transports; start failure is logged after ControlTask creation | `test_control_is_independent_of_blynk_transport`; architecture guardrail; live KFB003 boot/status | H-pass, B-pass, Guardrail, T-pass normal path; outage pending |
| external commands preserve bounded fairness | shared atomic IDs skip zero/internal OTA; HTTP/Blynk alternate under a 13-command budget and stop after Stop | `test_shared_ids_wrap_concurrency_and_fair_drain` | H-pass, B-pass |
| status is complete, bounded, and change-driven | 15 normalized fields, explicit timer presence, candidate/commit retry, 960-byte payload capacity under the 1,024-byte public limit | `test_projection_connect_throttle_coalescing_and_retry`, `test_status_timer_normalization_and_serializer_budget`; live KFB003 initial/reconnect status | H-pass, B-pass, T-pass projection; exact broker timing/silence pending |
| atomic Start, result/event streams, and reconnect generations are isolated and non-replayed | strict one-message Start parsing has no retained target; deprecated Start names emit bounded remote errors and cannot enter the application mailbox; the bounded Blynk-only correlation filter, five event schedulers, independent disconnect/connect generations, generation tags through both Blynk mailboxes, callback-ordered drop acknowledgement, disconnect clearing, and no control datastream readback remain intact | `test_atomic_start_mapping_and_strict_parser`, `test_legacy_start_protocol_fails_closed`, `test_atomic_start_feedback_and_correlation`, `test_disconnect_reconnect_boundary_discards_old_connection_state`, `test_translated_commands_do_not_cross_reconnect_boundary`, `test_results_and_events_are_separate_and_not_replayed`; historical live Start/Stop results, reboot no replay, enabled event trigger and owner-confirmed e-mail | H-pass, B-pass, Guardrail, T-pass legacy result/no-replay/e-mail; atomic Console/reconnect and phone push pending |
| credentials are provisioned without repository secrets | bounded UART0 `FUMURI-BLYNK/1` set/status/clear, versioned CRC NVS blob, redacted status, invalid data disables only Blynk | `test_provisioning_blob_and_fragmented_parser`; token-source guardrail; KFB003 redacted status after signed reboot | H-pass, B-pass, Guardrail, T-pass provisioning/persistence |
| MQTT contract is exact and reproducible | `espressif/mqtt ==1.0.0`, TLS CA bundle, MQTT 3.1.1, regional port 8883, clean session, 45-second keepalive, ten-second reconnect, QoS0/no retain, one downlink subscription | dependency/source/effective-sdkconfig guardrails; KFB003 regional TLS certificate and online state | B-pass, Guardrail, T-pass live TLS/connect |

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
- at M7, functional pull-independent register response, complementary
  configuration readbacks, exact software/driver terminal `0x11`, and ten raw
  samples are T-pass after the preserved first failure; the signed ordinary
  target additionally passed cycles 1/60/180 at 25.7/25.7/25.8 C over about
  179 seconds. This completes M7's defined functional activation. Still qualify
  fitted Rref/module identity, continuity/shield termination, calibrated
  accuracy, controlled faults/recovery, longer-duration behavior,
  response/noise, heater interference, and independent electrical/thermal
  safety under M6B/pre-real-heater and release gates;
- bind PID calls/gains to a validated real cadence, select calculation form and
  tuning policy on the real thermal plant, and validate real SSR/heater output,
  independent cutoff, and electrical safe state at M8; the inactive adapter and
  simulated tests close none of those gates;
- at M9, retain the installed-device digital T-pass and corrected A3-only
  response evidence without extending it to A0-A2, temperature, curve,
  calibration, or accuracy. Still identify module revision and external
  pull-ups; measure the rail and each reference resistor; establish R25 and a
  curve from stable co-located points against a separately validated reference;
  exercise A0-A2, the second ADC, known resistance/voltage, disconnect/open/
  short, settling/noise, accuracy/repeatability, sustained operation, and heater
  interference; and validate project backend/sequencer integration plus
  ControlTask timing before production activation. PT100/MAX31865 is only a
  possible future reference after its own checks. No ice-bath/simultaneous
  calibration occurred, the A3 procedure still needs terminal-idle `0x8583`
  verification, and the last known board image is the temporary diagnostic;
- validate persistence, reset reason, and `resumeAfterPowerFailure` at M10;
- validate M12 AP/STA provisioning, automatic captive opening, real scan and
  hidden-SSID/wrong-password fallback, mDNS, authentication, Wi-Fi-loss
  isolation, ten-minute runtime, affinity/watermark, and TWDT on the final board;
- optional additional M13 resilience evidence may exercise a deliberately
  tampered live asset, live operation timeout, and Wi-Fi loss during install;
  the defined signed-release, both-slot, mark-valid, rollback, and reinstall
  completion path has passed on KFB003;
- M14 signed full-serial migration, preserved NVS, durable START/sample/CHANGE/
  END across reboot, API counters, and HistoryTask affinity/stack have passed on
  KFB003 using simulated I/O; deliberate Wi-Fi-loss-during-RUNNING and injected
  target flash-failure behavior remain pending;
- measure the exact initial-snapshot count and five-second/silence behavior at
  the broker, confirm phone push receipt and the native mobile layout,
  and execute deliberate Wi-Fi/Blynk-loss isolation without replay. Live TLS,
  status, Start/Stop semantic results, reboot no replay, remote-error e-mail,
  and the safe firmware-check path passed on KFB003. A real signed `v0.15.0`
  install remains separately gated on authorization of its public release.
