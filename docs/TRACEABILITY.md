# M0-M15 Requirements Traceability

Status: **M15 software is host/sanitizer and ESP-IDF cross-build validated;
Blynk Console is configured and KFB003 provisioning, live TLS/status/commands,
reboot no-replay, firmware check, and remote-error e-mail delivery passed;
the inactive M7 MAX31865 adapter is host-tested and API cross-built while
production remains simulated; phone push receipt, native mobile layout, exact
broker timing, deliberate transport loss, the M14 Wi-Fi-loss scenario, M12
radio edge cases, and all external-sensor/hardware safety evidence remain
pending**

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
`tests/host/smoker_v0_tests.cpp`, `tests/host/smoker_m7_tests.cpp`,
`tests/host/smoker_m12_tests.cpp`, `tests/host/smoker_m13_tests.cpp`,
`tests/host/smoker_m14_tests.cpp`, and `tests/host/smoker_m15_tests.cpp`. All
host test groups are registered in `tests/CMakeLists.txt`.

## Business rules

| Rule | Milestone/status | Implementation evidence | Test/evidence | Validation |
|---|---|---|---|---|
| BR-001 | M3 implemented | `SmokerApplication::process(StartSessionCommand)` rejects Start while running | `test_m3_session_and_snapshot` | H-pass, B-pass |
| BR-002 | M1/M5 implemented | separate chamber state and `ProbeRuntime`/`ProbeSnapshot` types | `test_m5_complete_slice` | H-pass, B-pass |
| BR-003 | M2 implemented | single `IChamberSensor` input used by control and safety | `test_m2`, `test_m4_invalid_and_latched_fault` | H-pass, B-pass |
| BR-004 | M5 implemented/M9 dependency selected | probe configuration is a vector; non-empty/unique IDs are validated; exact-pinned dual-ADS1115 driver is available for the future platform adapter, while physical channel capacity remains M6B/M9 evidence | `test_m5_complete_slice`, `test_m5_validation_queue_and_combined_order`; ESP-IDF component cross-build | H-pass, B-pass component; real capacity HW-pending M6B/M9 |
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
| HR-003 | M14 implemented/guarded | preallocated SPSC publication drops rather than waits; HistoryTask failure/flash coordination remains platform-only and has no command/heater path | `test_sampling_and_mailbox_saturation`, `test_control_output_is_independent_of_history_saturation`, `test_flash_operation_serialization`; architecture guardrail | H-pass, B-pass, Guardrail; Wi-Fi-loss T-pending |
| HR-004 | M14 implemented | exact 4 MiB log initializes random/empty media lazily, commits a whole-session eviction tombstone before multi-page erase, evicts completed sessions first, and marks a partition-filling retained session truncated | `test_empty_random_and_reboot`, `test_torn_and_corrupt_records`, `test_rollover_eviction_truncation_and_interruption` including reset after partial victim erase; partition and retained-session readback | H-pass, B-pass, Guardrail; T-pass partition/reconstruction |
| HR-005 | M14 implemented | records always carry monotonic `session_elapsed`; Unix UTC is optional and can first appear after START | `test_empty_random_and_reboot`; KFB003 samples after SNTP sync | H-pass, B-pass, T-pass |
| HR-006 | M14 implemented | reboot reconstruction marks a retained session without committed END interrupted and does not enter application recovery | `test_rollover_eviction_truncation_and_interruption` | H-pass, B-pass; T-pending |
| HR-007 | M14 implemented | only two read-only operational routes exist; strict bounded query parsers reject duplicate/unknown/malformed fields; commissioning rejection is retained | `test_strict_history_queries`; HTTP/browser fixtures; authenticated KFB003 200/400/404 responses | H-pass, B-pass, Guardrail; T-pass operational API |
| HR-008 | M14 implemented | durable uint64 history ID is reconstructed independently from application session ID and emitted as a JSON decimal string | `test_empty_random_and_reboot`; KFB003 history IDs 1--3 retained across reset | H-pass, B-pass, T-pass |

## Remote-access rules

| Rule | Milestone/status | Implementation evidence | Test/evidence | Validation |
|---|---|---|---|---|
| RA-001 | M15 implemented | Blynk/MQTT is platform-only, observes immutable snapshots, and has no heater/control dependency; service startup failure is non-fatal | `test_control_is_independent_of_blynk_transport`; architecture guardrail; KFB003 live online status and safe reboot | H-pass, B-pass, Guardrail, T-pass startup/live path; deliberate outage pending |
| RA-002 | M15 implemented | callback allowlist crosses a reserved-Stop raw SPSC mailbox, BlynkTask mapping, a second application SPSC mailbox, and ControlTask-only round-robin submission; results are correlation-filtered | `test_allowlisted_deterministic_command_mapping`, `test_raw_mailbox_stop_reservation_and_concurrency`, `test_shared_ids_wrap_concurrency_and_fair_drain`, `test_results_and_events_are_separate_and_not_replayed`; KFB003 Blynk Start/Stop results | H-pass, B-pass, Guardrail, T-pass Start/Stop |
| RA-003 | M15 implemented | one complete connect projection plus normalized equality, five-second minimum, newest-value coalescing, retry, and unchanged silence | `test_projection_connect_throttle_coalescing_and_retry`, `test_status_timer_normalization_and_serializer_budget`; KFB003 initial live status | H-pass, B-pass; T-pass initial projection, exact broker timing/silence pending |
| RA-004 | M15 implemented | `LastCommandResult` and five per-type throttled/coalesced events are separate from the 15-field bounded `batch_ds` projection | `test_status_timer_normalization_and_serializer_budget`, `test_results_and_events_are_separate_and_not_replayed`; enabled Console events, live malformed-command trigger, owner-confirmed e-mail | H-pass, B-pass; T-pass event/e-mail, phone push pending |
| RA-005 | M15 implemented/hardened | clean session and no get/sync/retained publish; independent disconnect and connection generations force cleanup before reconnect activation; both Blynk command stages discard stale generations; pending Start parameter, results/feedback/events, and the old drop watermark are cleared while new-generation input remains live | `test_disconnect_reconnect_boundary_discards_old_connection_state`, `test_translated_commands_do_not_cross_reconnect_boundary`, `test_results_and_events_are_separate_and_not_replayed`, `test_raw_mailbox_stop_reservation_and_concurrency`; MQTT source guardrail; KFB003 reboot after Stop | H-pass, B-pass, Guardrail, T-pass reboot/no-Start-replay; collapsed transport-boundary target scenario pending |
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
| CR-006 | M2 current/M8 specified | M2 retains the simple deterministic 100/0 controller; D055/M8 select a future exact-pinned `espressif/pid_ctrl` platform adapter whose normalized request remains before the safety gate | `test_m2`; planned M8 adapter, timing/allocation, reset/OFF, ESP32-S3, and real-plant tuning tests | M2 H-pass/B-pass; Deferred M8, HW-pending tuning |

## Safety rules

| Rule | Milestone/status | Implementation evidence | Test/evidence | Validation |
|---|---|---|---|---|
| SF-001 | M4 implemented/P0 guarded | safety evaluated synchronously before sole final heater write | `test_m4_invalid_and_latched_fault`, `test_p0_cr_005_heating_state_invariants`; `tools/check_architecture.py` write-path check | H-pass, B-pass; T-pending |
| SF-002 | M4 implemented; M7 adapter implemented/inactive | absent authoritative reading raises ChamberSensorInvalid with no last-known fallback; the exact-pinned backend maps configured-but-not-ready, driver/fault, and non-finite outcomes to absence while production still uses simulation | `test_m4_invalid_and_latched_fault`, `test_max31865_initialization_and_configuration_failures_are_absent`, `test_max31865_read_policy_never_reuses_a_previous_value`, `test_max31865_premature_application_tick_latches_fault_and_heater_off`; ESP-IDF API cross-build | H-pass, B-pass API compatibility, Guardrail; real timing and physical faults HW-pending M6B/M7 |
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
evidence. A connected KFB003 additionally passed anonymous public-release
download, two-slot boot, mark-valid, forced rollback, and final reinstall.

| Rule | Milestone/status | Implementation evidence | Test/evidence | Validation |
|---|---|---|---|---|
| OTA-001 | M13 implemented | OTA helpers/service remain in `smoker_platform`; app owns only internal permission commands and interlock; core has no OTA/platform include | `test_semantic_versions_and_descriptors`; `tools/check_architecture.py` layer checks | H-pass, B-pass, Guardrail |
| OTA-002 | M13 implemented | immutable snapshot admission plus correlated Prepare rejects RUNNING; dedicated atomic Prepare is ordered before Finish so timeout cannot orphan a delayed reservation; Start is blocked while update active | `test_application_update_permission` Stop/Prepare/Finish FIFO regression, `test_update_coordinator`; source guardrail; stopped-session public-release install on KFB003 | H-pass, B-pass, Guardrail; T-pass stopped-session install path |
| OTA-003 | M13 implemented | manual image-prefix check is independent of application reservation; real chip ID/project/version admission, HTTPS-only bounded redirects, guarded 4,096-byte request TX buffer, total deadlines, non-RUNNING install permission, and ESP-IDF RSA-3072 signed-update verification are enforced; the public fixed source requires no device credential, while release signing is isolated from ordinary CI and checked against the versioned public key; D051 makes the missing independent reviewer conditional on sole-maintainer repository/tag control | `test_application_update_permission`, `test_semantic_versions_and_descriptors`, `test_image_metadata_and_deadlines`, `test_update_coordinator`; effective-Kconfig/signing/serial source guardrails; anonymous `v0.13.0` download and real GitHub redirect on KFB003 | H-pass, B-pass, Guardrail; T-pass live signed image and redirect; live tamper/timeout not executed |
| OTA-004 | M13 implemented | exact custom table has `otadata` and equal `ota_0`/`ota_1` 3 MiB slots within confirmed 16 MiB flash; rollback config enabled; signed serial preflight rejects stale configuration, unsigned/build-mismatched apps, and mismatched generated layouts; ordinary unsigned/partial ESP-IDF flash targets fail closed | `test_update_coordinator`; `tools/check_effective_sdkconfig.py`, `tools/flash_signed_firmware.py --check-only`, `tools/check_partitions.py`, blocked ESP-IDF flash-target validation, ESP-IDF partition output; signed KFB003 USB migration, `ota_0` to `ota_1` install, forced rollback, and final reinstall | H-pass, B-pass, Guardrail; T-pass USB migration and both-slot rollback path |
| OTA-005 | M13 implemented | `PENDING_VERIFY` blocks Start; five safe TWDT-reset cycles mark valid; fault/10 s/mark error or runtime-context/ControlTask/OtaTask bootstrap failure invokes rollback reboot | `test_application_update_permission`; `tools/check_architecture.py` pending-bootstrap source guardrail; forced pending-image reset and clean reinstall on KFB003 | H-pass, B-pass, Guardrail; T-pass rollback and five-cycle mark-valid |
| OTA-006 | M13 implemented | static low-priority core-0 OtaTask owns bounded SNTP/HTTPS/flash operations and is outside TWDT; its stack/TCB are internal-DRAM objects, not part of the PSRAM-eligible heap service; ControlTask exchanges ordered bounded atomic signals/snapshots only; unavailable worker reports `FAILED` and rejects work | `test_image_metadata_and_deadlines`, `test_update_coordinator`; `tools/check_architecture.py` task-placement/order/availability checks; live check/install on KFB003 | H-pass, B-pass, Guardrail; T-pass normal install; T-pending Wi-Fi-loss install |

## Inactive M7 MAX31865 software-integration contracts

These checks cover a platform software boundary and ESP-IDF 6.0.2 source/API
compatibility only. M6B and M7 are not complete, and no row is physical sensor
or electrical evidence.

| Contract | Implementation evidence | Test/evidence | Validation |
|---|---|---|---|
| required hardware values are not invented | reference resistance, filter, RTD standard, SPI host, CS GPIO, and SPI clock are required configuration without defaults; PT100 nominal 100 ohm and three-wire are the only fixed confirmed choices | `test_max31865_configuration_policy_requires_explicit_valid_values`; source guardrail | H-pass, B-pass, Guardrail; physical values HW-pending |
| first-conversion freshness is explicit | configuration reports `ConfiguredAwaitingFirstSample`; a fake-clock-tested policy blocks before 55 ms at 60 Hz and 66 ms at 50 Hz, accepts exactly at each boundary, and resets after successful configuration, reinitialization, and fault recovery | `test_max31865_60_hz_first_conversion_boundary`, `test_max31865_50_hz_first_conversion_boundary`, `test_max31865_reconfiguration_resets_readiness_without_reuse`, `test_max31865_reinitialization_resets_readiness`, `test_max31865_fault_recovery_requires_fresh_current_value`; source ordering guardrail | H-pass, B-pass, Guardrail; module settling and target timing HW-pending |
| POR/stale values cannot cross an early read | explicit `NotReady` maps to absence; target readiness is checked before fault/temperature reads; repeated early reads neither touch emulated registers nor reuse a prior sample | `test_max31865_por_value_is_not_exposed_before_readiness`, `test_max31865_reconfiguration_resets_readiness_without_reuse` | H-pass, B-pass, Guardrail |
| every current result maps independently | adapter returns `Temperature` only for a current finite valid backend result; initialization/configuration/read/fault failures are absent and no last value is stored | `test_max31865_initialization_and_configuration_failures_are_absent`, `test_max31865_read_policy_never_reuses_a_previous_value` | H-pass, B-pass |
| existing safety remains authoritative | an early/absent adapter reading raises and latches `ChamberSensorInvalid`, commands heater OFF, and a later fresh reading does not resume heating automatically | `test_max31865_premature_application_tick_latches_fault_and_heater_off` | H-pass, B-pass; real fault injection HW-pending |
| project-owned read code has no explicit wait/allocation | common and target read code contains no explicit delay, task creation, heap allocation, or `max31865_measure()` call | `test_max31865_read_is_observed_allocation_free`; `tools/check_architecture.py` | H-pass ordinary-C++ allocation observation, Guardrail; ESP-IDF/driver allocation and real SPI worst-case blocking unproven |
| pinned API is exercised but inactive | target-only RAII backend uses the tested readiness policy before descriptor fault/temperature APIs in provisional continuous mode; `main`/runtime still compose `SimulatedChamberSensor` and contain no concrete SPI bus/GPIO | target compilation and architecture guardrail | B-pass API compatibility, Guardrail; bus ownership/timing and physical validation HW-pending |

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

## M14 history/runtime contracts

| Contract | Implementation evidence | Test/evidence | Validation |
|---|---|---|---|
| history cannot enter the critical dependency chain | only post-tick `SmokerSnapshotView` crosses a preallocated SPSC mailbox; ControlTask contains no history flash, query, mutex, wall-clock, or logging call | `test_sampling_and_mailbox_saturation`, `test_control_output_is_independent_of_history_saturation`; `tools/check_architecture.py` | H-pass, B-pass, Guardrail |
| critical history publication avoids allocation and 64-bit atomic helpers | probe/alarm vectors are reserved before task start; mailbox sequences/drop counter are uint32 atomics and publication drops on saturation | `test_sampling_and_mailbox_saturation`; target archive atomic-helper audit | H-pass, B-pass; T-pending |
| raw log survives incomplete writes and eviction | page/record CRC and commit-last markers retain committed prefixes; a failed runtime program abandons the tail; a committed eviction tombstone hides the whole victim until cleanup finishes | `test_torn_and_corrupt_records`; injected mid-eviction erase failure and reboot in `test_rollover_eviction_truncation_and_interruption` | H-pass, B-pass; T-pending power interruption |
| history lifecycle remains complete under same-cycle termination and transient writes | a first-observed STOPPED/FAULT session atomically publishes START+END; HistoryTask retains failed START/END ahead of later observations until durable | direct-terminal/saturation cases in `test_sampling_and_mailbox_saturation`; START/END retry cases in `test_torn_and_corrupt_records`; source guardrail | H-pass, B-pass, Guardrail |
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
| result/event streams and reconnect generations are isolated and non-replayed | bounded Blynk-only correlation filter, five event schedulers, independent disconnect/connect generations, generation tags through both Blynk mailboxes, callback-ordered drop acknowledgement, disconnect clearing, and no control datastream readback | `test_disconnect_reconnect_boundary_discards_old_connection_state`, `test_translated_commands_do_not_cross_reconnect_boundary`, `test_results_and_events_are_separate_and_not_replayed`; live Start/Stop results, reboot no replay, enabled event trigger and owner-confirmed e-mail | H-pass, B-pass, Guardrail, T-pass result/no-replay/e-mail; collapsed reconnect, phone push pending |
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
- validate real authoritative sensor behavior at M7;
- validate real heater output and electrical safe state at M8;
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
