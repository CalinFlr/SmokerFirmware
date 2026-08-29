# Staged ADS1115 device-count and i2cdev-ownership plan

## Goal

Allow the inactive M9 acquisition boundary to describe the one ADS1115 that is
actually installed, while retaining support for the eventual second converter,
and explicitly own the locked `i2cdev` 2.1.2 process-lifetime subsystem before
any descriptor or I2C access.

## Scope

- accept one or two explicitly configured ADS1115 device records;
- retain the existing per-device sequencing, provenance, quarantine, timeout,
  cache, and failure behavior for two devices;
- add a target-only non-copyable `i2cdev` subsystem owner and make the target
  backend require its active ownership evidence;
- provide checked descriptor and subsystem shutdown with descriptor-before-
  subsystem ordering;
- add focused host configuration/sequencing coverage and offline-compatible
  architecture/source guardrails;
- update the M9 architecture, decision, hardware, roadmap, traceability, and
  historical plans without activating the backend in production.

## Non-goals

- no board, serial, GPIO, flash, monitor, reset, provisioning, or connected
  diagnostic access;
- no production ADS1115 construction, `ControlTask` placement, sensor task,
  polling loop, delay, calibration, NTC conversion, or physical defaults;
- no second address assignment and no claim of module identity, timing,
  temperature accuracy, calibration, or completed M6B/M9 work;
- no MAX31865, PID, SSR/heater, Wi-Fi, OTA, history, or Blynk behavior change.

## Current repository observations

- Initial HEAD is `8d7743957d4fe0b515ce841f09ebbe3efa631d2b` on
  `main`, ahead 9 and behind 0, with a clean worktree/index.
- `valid_ads1115_acquisition_configuration()` requires exactly two devices and
  requires a channel on both; `Ads1115TargetBackend::initialize()` likewise
  requires both descriptor slots.
- Only the GPIO17/GPIO18, address-`0x48` converter is installed. The selected
  second converter remains deferred and its address is unconfirmed.
- Locked `ads111x` 1.1.14 creates a descriptor mutex in
  `ads111x_init_desc()`. Locked `i2cdev` 2.1.2 requires `i2cdev_init()` before
  device initialization/first I2C setup, but the inactive backend currently
  reaches `ads111x_set_mode()` without a project-owned call.
- Locked `i2cdev_done()` deletes port locks but does not reset the function-
  local static `initialized` flag in `i2cdev_init()`. A completed subsystem
  shutdown therefore cannot be followed safely by same-boot reinitialization.
- Production constructs `Max31865ChamberSensor`,
  `SimulatedFoodProbeSource`, `DeterministicChamberController`, and
  `SimulatedHeaterOutput`; PID and ADS1115 target paths remain uncomposed.

## Assumptions

- The owner instance is non-restartable, but is not a process-wide lifecycle
  authority. Future activation/composition must create exactly one owner and
  permit exactly one initialization attempt per boot; no project-global mutable
  state will be added for the inactive boundary.
- One backend may register one descriptor-owner lease with that owner. Checked
  subsystem shutdown refuses while the lease remains, and the backend releases
  it only after every acquired descriptor reports successful release.
- Initialization-time storage/mutex allocation remains allowed. Existing
  steady-state project paths retain their no-allocation/no-wait contract.
- The locked dependency sources may be absent in a clean offline checkout;
  manifest and lock checks remain mandatory, while exact lifecycle-source
  checks run only when the complete managed source is present.

## Steps

1. Generalize configuration validation to one or two devices and add focused
   valid/invalid host cases plus a one-device no-index-1 sequencing proof.
2. Add the target-only subsystem owner, direct exact `i2cdev` dependency, and
   backend active-owner/lease plus aggregate checked descriptor shutdown.
3. Extend architecture guardrails for lifecycle call-site exclusivity,
   descriptor-before-subsystem cleanup, non-restartability, optional exact
   upstream lifecycle validation, production composition, and dependency pins.
4. Reconcile architecture, D057, hardware, roadmap, traceability, and M9 plans
   with staged one/two-device support and the still-inactive ownership path.
5. Run focused M9, architecture, traceability, full host, full ESP-IDF,
   tracked-files-only offline, dependency/composition/call-site/C++20/size/
   partition/unsigned-flash, and whitespace validation.

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

The final audit also runs the architecture checker from a fresh tracked-files-
only copy without `managed_components`, inspects exact manifest/lock pins and
production construction, searches all project lifecycle call sites, and
reviews target compile commands plus the existing partition, firmware-size,
and unsigned-flash guard results.

## Risks / unresolved items

- `i2cdev` cleanup can fail after partial descriptor or bus teardown. Backend
  success proves only that all `ads111x_free_desc()` calls returned `ESP_OK`,
  and subsystem success proves only that `i2cdev_done()` returned `ESP_OK`.
  Locked nested cleanup errors can be swallowed, so neither proves complete
  teardown or physical/driver quiescence.
- The per-composition owner prevents its own unsafe reinitialization and
  overlapping backend ownership without introducing global state; it cannot
  exclude another owner instance. Callers must preserve backend-before-owner
  lifetime, and activation must enforce one owner/initialization per boot.
- Locked `i2cdev` latency, retries, mutex waits, allocation, and recovery remain
  unsuitable for an unmeasured critical-loop claim.
- Module identity, external pull-ups, actual rail/resistors, A0-A2, second ADC,
  calibration/curve, timing, accuracy, sustained operation, faults, and heater
  interference remain physical M6B/M9 gates.

## Outcome

- Follow-up review confirmed that locked descriptor cleanup can swallow nested
  device/bus deletion failures and that instance state does not enforce
  process-wide owner exclusivity. The boundary now states only direct API-return
  success, makes one owner/initialization per boot an activation precondition,
  and keeps same-boot restart unsupported without adding global mutable state.
- All five audited findings were confirmed against the starting project and
  exact locked sources; none was rejected. In particular, `ads111x` 1.1.14
  creates the descriptor mutex, while `i2cdev` 2.1.2 requires subsystem
  initialization before first I2C setup and cannot safely restart after done.
- Configuration and the real backend now accept one or two devices. Validation
  rejects zero/more-than-two devices, an unconfigured-device channel, any
  configured device without a channel, duplicate probe IDs, and duplicate
  mux-per-device mappings. Fixed two-slot storage is bounded by the configured
  count, and the focused one-device trace touches only index 0.
- The target-only non-copyable owner initializes locked `i2cdev`, grants one
  backend lease, refuses subsystem shutdown while descriptors remain, and
  permanently rejects reinitialization through the same instance after a
  release attempt. Backend shutdown attempts all acquired descriptors and
  releases the lease only when every free returns `ESP_OK`; subsystem shutdown
  reports the direct `i2cdev_done()` result. Neither result proves nested
  teardown or quiescence, and another owner instance is not excluded.
  Production constructs neither target object; one owner/initialization per
  boot is an explicit future activation precondition.
- `esp-idf-lib/i2cdev` is now a direct exact `==2.1.2` dependency; its existing
  lock hash `ad8981cc64533dcaced5107d72e42bcebe79345e194e82795792af531b300ce3`
  is unchanged. No dependency was floated or upgraded.
- Focused M9, architecture, traceability, full host, sanitizer, and full ESP-IDF
  verification passed. All 12 host tests passed in normal and sanitizer builds;
  all 33 target project sources passed strict C++20 auditing; firmware uses
  1,441,792 of 3,145,728 bytes in the smallest app partition; partition and
  unsigned-flash guards passed.
- A fresh tracked-files-only copy passed architecture/traceability checks with
  `managed_components` absent. A separate isolated fixture passed with exact
  managed sources present and then rejected an intentionally incompatible
  `i2cdev` lifecycle source with the expected clean diagnostic.
- Production remains `Max31865ChamberSensor`, `SimulatedFoodProbeSource`,
  `DeterministicChamberController`, and `SimulatedHeaterOutput`; PID remains
  inactive. No board, serial, GPIO, flash, monitor, reset, provisioning, or
  connected-diagnostic action was performed.
