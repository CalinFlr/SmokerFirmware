# Inactive dual-ADS1115 software integration plan

## Goal

Implement the smallest host-testable and ESP-IDF-cross-buildable dual-ADS1115
food-probe acquisition boundary while production remains on
`SimulatedFoodProbeSource` and every unknown physical value stays mandatory
configuration.

## Scope

- add one `smoker_platform` acquisition owner/sequencer behind
  `IFoodProbeSource`;
- stage explicit single-shot mux/gain/rate selection, start, later readiness,
  and later raw-value retrieval without a polling loop;
- cache independently timestamped per-probe results for allocation-free reads;
- require an injected raw-code calibration/validity policy with no physical
  defaults;
- add a target-only RAII backend over the exact-pinned `ads111x` 1.1.14 API;
- add host behavior tests, source guardrails, and evidence-backed docs.

## Non-goals

- no PID/M8, GPIO assignment, concrete I2C bus, physical address, pull-up,
  frontend, probe curve, voltage range, temperature range, or calibration;
- no `i2cdev_init()` call, runtime adapter construction, hardware activation,
  flashing, monitoring, provisioning, NVS work, new task, or push;
- no claim that locked `i2cdev` latency, retry delays, mutex waits, allocation,
  or real ControlTask suitability is validated;
- no change to chamber control, heater demand, or chamber fault semantics.

## Current repository observations

- M6B and M9 remain incomplete; two ADS1115 converters and the exact registry
  dependency are selected, but the physical modules/frontend are unknown.
- Production and all runtime composition still use `SimulatedFoodProbeSource`.
- `IFoodProbeSource::read()` is called during application raw acquisition and
  food-probe absence already remains an alarm/monitoring condition only.
- `ads111x_init_desc()` writes 1 MHz to the public descriptor and creates a
  device mutex. Its mode, mux, gain, rate, start, busy, and value APIs each use
  I2C transactions through locked `i2cdev` 2.1.2.
- Locked `i2cdev` lazily creates the port/device on first I/O, can wait up to
  `CONFIG_I2CDEV_TIMEOUT`, and retries with internal `vTaskDelay()`. That is
  incompatible with claiming a proven bounded ControlTask path.
- TI ADS1115 Rev. E states that conversion time is `1 / DR`, data-rate
  variation is +/-10%, and single-shot power-up is approximately 25 us. The
  approximate power-up and task scheduling margin have no documented maximum,
  so the stuck deadline remains explicit configuration and must exceed the
  worst conversion period derived from the selected nominal rate.

## Assumptions

- exactly two configured ADS1115 device records represent the selected
  converters; they may share one compatible bus or occupy genuinely separate
  buses;
- initialization-time vector/mutex/driver allocation is allowed; steady-state
  project-owned `service()` and `read()` paths must not allocate;
- a service caller will eventually be selected only after target timing is
  measured. The inactive slice exposes the boundary but does not place it in
  `ControlTask` or create another task.

## Steps

1. Add explicit platform configuration types, validation, calibration seam,
   cached food-probe source, and one round-robin state machine.
2. Add the target-only two-descriptor backend, override the driver-owned 1 MHz
   clock before first I2C I/O, and call only the pinned mode/mux/gain/rate/start/
   busy/value/free APIs.
3. Add focused host tests for configuration, sequencing/freshness, failures,
   expiry, timeout, allocation observation, and chamber-control isolation.
4. Extend architecture guardrails for platform confinement, target-only driver
   use, API/call ordering, absence of project waits/tasks, explicit
   configuration, and continued simulated composition.
5. Update the historical M9 plan and ARCHITECTURE, DECISIONS, HARDWARE,
   ROADMAP, and TRACEABILITY without changing the incomplete M6B/M9 status.
6. Run the focused M9 host test, architecture/traceability checks, full host and
   target verification, whitespace check, and final inventory audit.

## Validation commands

```sh
cmake -S tests -B build-m9-host
cmake --build build-m9-host --target smoker_m9_tests
ctest --test-dir build-m9-host -R smoker_v0.m9_ads1115 --output-on-failure
python3 tools/check_architecture.py
python3 tools/check_traceability.py
./tools/verify.sh --host-only
./tools/verify.sh --idf-only
git diff --check
git status --short --untracked-files=all
```

## Risks / unresolved items

- module identity, supplies, addresses, buses/pins, pull-ups, analog frontend,
  signal map, calibration, validity limits, and connected behavior remain
  hardware-pending;
- the real backend inherits bounded-time and allocation uncertainty from
  `i2cdev`/ESP-IDF, including lazy bus setup, mutex waits, transaction timeouts,
  retry delays, and possible handle recreation after some errors;
- a target service cadence and task placement cannot be approved until those
  timings are measured against the final bus and ControlTask budget;
- the explicit timeout must include a frontend-specific and scheduling margin
  beyond the datasheet-derived conversion-period floor.

## Implementation outcome

- Added one inactive `Ads1115FoodProbeSource` round-robin sequencer and a
  target-only `Ads1115TargetBackend`; production remains simulated and no
  `i2cdev_init()` call or runtime service placement was added.
- Exercised the exact pinned target APIs `ads111x_init_desc()`,
  `ads111x_free_desc()`, `ads111x_set_mode()`,
  `ads111x_set_input_mux()`, `ads111x_set_gain()`,
  `ads111x_set_data_rate()`, `ads111x_start_conversion()`,
  `ads111x_is_busy()`, and `ads111x_get_value()`.
- Rejected continuous conversion because changing configuration can complete
  the prior conversion before new settings take effect and would complicate
  channel freshness. Rejected independent per-probe machines because they can
  race a shared descriptor. Rejected synchronous readiness polling and a new
  task because both violate the requested staged V0 boundary. Deferred direct
  ControlTask placement because locked `i2cdev` timing is not target-proven.
- Focused M9 host build/test passed. Architecture and traceability guardrails
  passed. Full host plus sanitizer verification passed all 11 tests. Full
  ESP-IDF 6.0.2 verification passed the strict C++20 build, pinned-component,
  effective-config, partition, size, and unsigned-flash rejection gates.
- No physical module, address, bus, GPIO, pull-up, analog frontend, probe curve,
  calibration, service cadence, or connected target behavior was supplied or
  inferred. M6B/M9 remain incomplete.
