# Control Readiness Handshake Plan

Status: **Complete — 2026-08-29**

## Goal

Publish local HTTP control readiness only after `ControlTask` completes a cycle
whose immutable snapshot is successfully published and whose task-watchdog
reset succeeds.

## Scope

- add a small host-portable one-shot readiness latch;
- observe the real snapshot-publication and watchdog-reset results in
  `ControlTask`;
- move the sole readiness publication from startup orchestration into
  `ControlTask` after both results succeed;
- add executable host behavior tests and narrow source/integration guardrails;
- clarify the existing architecture, safety, and traceability contracts;
- run host and ESP-IDF 6.0.2 verification, then deliver an unmerged PR.

## Non-goals

- no `EspMonotonicClock` changes;
- no API schema, fault, safety, command, OTA-validation, history, Blynk,
  MAX31865, PID, ADS1115, heater, task-priority, affinity, watchdog-policy, or
  firmware-version changes;
- no new task, lifecycle framework, waiting, logging loop, or service reordering;
- no target/hardware validation claim from host tests or a cross-build.

## Current repository observations

- freshly fetched `main` is `76c103d5b7b142a154e9a595392e9744b9442aa6`;
  merged PR #8 is `d060b53` and merged PR #9 is `76c103d`;
- production uses `ordinary_runtime.cpp` and the existing
  `EspMonotonicClock` adapter;
- `start_ordinary_runtime()` creates `ControlTask`, starts auxiliary services,
  then unconditionally calls `connectivity.mark_control_ready()`;
- watchdog-subscription failure suspends `ControlTask` before its first tick,
  while startup can continue to that unconditional readiness call;
- one `SmokerApplication::tick()` performs safety evaluation and the sole final
  heater write before returning;
- `snapshot_view()` follows the tick, `SnapshotExchange::publish()` reports
  success/failure, and `esp_task_wdt_reset()` follows publication;
- local snapshot, command, and firmware-install handlers already return the
  established `503` response while their atomic readiness flag is false.

## Assumptions

- readiness means the control cycle is observable, not that the application is
  fault-free or able to heat;
- a successful publish followed by a failed watchdog reset must remain
  not-ready and retain the existing `ESP_ERROR_CHECK` fail/panic path;
- a failed publish may retry readiness on a later complete cycle;
- `mark_control_ready()` remains a bounded atomic store and is safe even when
  the HTTP service has not completed startup;
- construction creates a naturally false readiness state on every reboot.

## Steps

1. Add the latch to the existing portable runtime transport support and cover
   initial, failure, retry, first-success, one-shot, and fault-independent
   behavior in the M12 host suite.
2. Capture the real snapshot publish result, reset TWDT, feed both results to
   the latch, and publish readiness only on its first successful transition.
3. Remove the startup readiness call and extend architecture guardrails for
   ownership, order, one-shot behavior, and control-path exclusions.
4. Update only the relevant architecture, safety, and traceability text.
5. Run both required verification modes, audit the complete requirement list,
   commit, push, and open an unmerged PR.

## Validation commands

```sh
bash tools/verify.sh --host-only
bash tools/verify.sh --idf-only
git diff --check
```

## Risks / unresolved items

- source guardrails prove intended ownership/order only; executable latch tests
  provide the behavioral proof;
- cross-build success does not prove target scheduling, watchdog behavior, HTTP
  timing, sensor behavior, heater behavior, or hardware safety;
- no connected-target validation is authorized or required by this task.

## Outcome

- `ControlReadinessLatch` now accepts only actual snapshot-publication and TWDT
  reset results and emits one transition after the first cycle where both are
  true;
- `ControlTask` owns the transition after `tick()`, immutable snapshot publish,
  and TWDT reset; startup orchestration no longer publishes readiness;
- a complete `FAULT` snapshot remains eligible and observable because no
  application state enters the latch API;
- host behavior, sanitizer behavior, source guardrails, HTTP fixtures, and the
  ESP-IDF 6.0.2 ESP32-S3 cross-build pass through `bash tools/verify.sh`;
- no connected target or hardware behavior was exercised or claimed.
