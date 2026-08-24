# M7 MAX31865 Production Activation Plan

Status: **Complete for implementation/build-only activation. Ordinary target
execution and remaining physical validation are explicitly pending.**

## Goal

Make the real MAX31865/PT100 path the ordinary firmware's sole authoritative
chamber source while retaining simulated food probes and heater output, the
deterministic controller, the inactive PID adapter, one `ControlTask`, and the
existing synchronous fail-closed safety gate.

## Scope

- Audit and consolidate the complete 2026-08-24 connected chronology.
- Centralize the evidence-backed production SPI, RTD, filter, standard, and
  provisional operational Rref configuration.
- Add target-only RAII SPI-bus ownership and compose the existing MAX31865
  adapter in the ordinary runtime.
- Replace unsafe persistent configuration writes with exact active `0xD1`
  write/readback, exact terminal `0x11` cleanup, and freshness-resetting fault
  recovery.
- Add a real ESP-IDF monotonic `IClock` and a bounded pre-ControlTask 66 ms
  first-conversion bootstrap.
- Update focused tests, executable architecture guardrails, plans, hardware
  evidence, architecture/decision/roadmap/traceability text, and current
  user-facing composition language.
- Run host, sanitizer, ESP-IDF, ordinary/diagnostic overlay, strict-C++20,
  size, partition, dependency, flash-rejection, source, and ELF-isolation
  validation without performing any board-facing action.

## Non-goals

- No flash, monitor, reset, wiring manipulation, provisioning, NVS erase, OTA,
  signing, release, tag, commit, push, or other connected-board action.
- No real heater/SSR GPIO, switching window, PID activation/tuning, ADS1115
  activation, fan, smoke-generator, persistence, or recovery work.
- No claim of calibrated accuracy, fitted Rref measurement/tolerance,
  deliberate open/short qualification, sustained behavior, noise, settling,
  heater interference, or independent electrical safety.
- No change to the default-OFF diagnostic's runtime behavior.

## Pre-implementation repository observations

- Initial `HEAD` is `ab29c2171bcfceb134eed359c13b001a3a49f841`; `main` is
  six commits ahead of `origin/main`.
- The index is empty. Exactly six expected documentation paths are initially
  modified from the first failed connected run; they must be preserved and
  corrected rather than restored.
- `esp-idf-lib/max31865` is exact-pinned at 1.0.8. Its device setup uses SPI
  mode 1, while `max31865_set_config()` preserves command/self-clearing bits
  D5/D3:D2/D1 from a preceding register read.
- The existing inactive adapter already maps `NotReady`, driver errors,
  MAX31865 faults, and non-finite values to absence without caching.
- Ordinary composition still uses `SimulatedChamberSensor`, `SimulatedClock`,
  `SimulatedFoodProbeSource`, `DeterministicChamberController`, and
  `SimulatedHeaterOutput`.
- The application already writes heater OFF at construction and synchronously
  latches `ChamberSensorInvalid` before its sole final heater write.

## Connected evidence audited

1. The first run recorded in the initially dirty documentation observed
   pull-following MISO (`0xff`/`0x00`), failed fallback readback, and restored
   the ordinary signed simulated image.
2. `idf_py_stdout_output_46058`, SHA-256
   `f448d05c0dfc35be8dff0aa7e392ec8449b17a369f1ad14e3070f19649a727c3`,
   later passed SPI/configuration and exact `0x11` shutdown but reported ten
   raw-zero samples with fault `0x40` before RTD wiring correction.
3. `idf_py_stdout_output_47381`, SHA-256
   `5979dcb174bdc49661c77cb26a67a2ca7db16f3bc42c7daf45a5dbd516d3916d`,
   passed pull discrimination, complementary patterns, exact active `0xD1`,
   ten raw readings of 8548/8549 with fault `0x00`, zero transaction/sensor
   errors, exact terminal `0x11`, and final transaction/shutdown pass.
4. With provisional operational Rref 430.0 ohm and ITS-90, raw 8548/8549
   calculate to approximately 31.287679/31.321568 C. This corroborates the
   maintainer-observed approximately 31.3 C ambient result; it is not a
   reference-temperature or accuracy result.

## Assumptions

- The recorded SPI2/GPIO12/11/13/10 assignment and 100 kHz clock are the
  production configuration because the final diagnostic exercised them.
- PT100 nominal 100.0 ohm, three-wire, 50 Hz, and ITS-90 are explicit selected
  operating inputs. Rref 430.0 ohm is provisional operational configuration,
  not a measurement of the fitted resistor or its tolerance.
- The one-time startup wait occurs outside the steady-state critical loop. Once
  control evaluation begins, every absent/invalid reading immediately retains
  the existing latched fault and heater-OFF behavior.

## Steps

1. Add centralized production configuration, ESP monotonic clock, and RAII
   SPI-bus ownership; preserve descriptor-before-bus release ordering.
2. Harden the target backend with exact raw configuration access, `0xD1`
   active verification, `0x11` checked cleanup, and exact freshness-resetting
   fault recovery.
3. Refactor the mixed ordinary runtime naming/composition, enforce the bounded
   first-sample bootstrap before `ControlTask`, and keep all actuators and food
   inputs simulated.
4. Extend M7 host tests and source guardrails for initialization failure,
   no-cache failure/recovery, exact production configuration, bootstrap
   ordering, composition, cleanup order, diagnostic isolation, and absence of
   target heater GPIO.
5. Consolidate plans, active documentation, traceability, hardware evidence,
   and user-visible ordinary-composition language with T-pass/B-pass/H-pass/
   T-pending/HW-pending kept distinct.
6. Run all required focused/full validation and a requirement-by-requirement
   completion audit; record final sizes, paths, HEAD/status, and remaining
   target/physical gates.

## Validation commands

```text
cmake --build build-host --target smoker_m7_tests
ctest --test-dir build-host --output-on-failure -R smoker_v0.m7_max31865
git diff --check
python3 tools/check_architecture.py
python3 tools/check_traceability.py
./tools/verify.sh --host-only
./tools/verify.sh --idf-only
idf.py ... fresh ordinary build
idf.py ... fresh diagnostic-overlay build
python3 tools/check_target_compile_commands.py <ordinary compile database>
python3 tools/check_target_compile_commands.py <diagnostic compile database>
python3 tools/check_partitions.py <generated partition table>
python3 tools/check_firmware_size.py <ordinary application binary>
nm/rg source, map, and ELF composition/actuator audits
```

## Risks / unresolved items

- Target execution of the ordinary real-sensor composition is still pending.
- Connected diagnostic functional bring-up is T-pass, but physical Rref value/
  tolerance, calibration/accuracy, deliberate fault qualification, longer-term
  timing/settling/noise, sustained behavior, heater interference, and complete
  M6B electrical/safety work remain HW-pending.
- ESP-IDF/driver/SPI internal allocation and worst-case blocking remain target
  properties; project-owned steady read code must remain wait/task/allocation
  free.

## Remediation review — 2026-08-24

### Findings

1. **Confirmed — finite alone was insufficient.** The chamber adapter accepted
   every finite driver result. Pinned 1.0.8 converts a clear-fault raw zero to
   approximately -242.02 C and returns `ESP_OK`, while the global `Temperature`
   domain intentionally accepts all finite values. That result could therefore
   cross the authoritative boundary and request maximum deterministic demand.
2. **Confirmed — boot sensor faults were electrically safe but not observable.**
   Bus, descriptor/configuration, and first-boundary failure returned before
   `ControlTask`, the first application tick, snapshot/event publication, and
   local/Blynk services. The host failure test covered application behavior but
   ordinary composition prevented it from running.
3. **Confirmed — ordinary MISO had no deterministic pull.** ESP-IDF 6.0.2
   `spi_bus_initialize()` routes GPIO13 into the SPI input through IOMUX/matrix
   setup but does not call a GPIO pull API. The first diagnostic already showed
   why pull-following MISO is useful failure discrimination.

### Remediation decisions

- Add a mandatory host-testable MAX31865 validity policy with finite, strictly
  ordered inclusive bounds. Production explicitly uses -50.0..+200.0 C from
  the supplier-documented assembled probe range. This is operational policy,
  not measured calibration, and does not narrow the global `Temperature` type.
- Keep sensor hardware/bootstrap failure distinct from critical runtime
  construction. Bus/pull, descriptor/configuration, or boundary failure makes
  the chamber source permanently unavailable until reboot while the ordinary
  `ControlTask` and observation/connectivity services start. Its first IDLE
  tick publishes no chamber value, latches `ChamberSensorInvalid`, reports
  FAULT, and retains heater OFF. Runtime-context allocation and task-creation
  failure retain immediate pending-image rollback.
- Preserve the existing OTA contract: a sensor-faulting pending image reaches
  normal control-cycle validation, cannot contribute a safe cycle, and rolls
  back on the published fault. Five consecutive safe cycles remain mandatory.
- Make the bus owner establish a checked GPIO13 internal pull-up after successful
  `spi_bus_initialize()` and before descriptor access. The backend rejects
  descriptor creation unless that owner proves it owns initialized SPI2. Exact
  converter shutdown/descriptor removal precedes bus release, and successful
  bus release precedes restoring GPIO13 to floating.
- Rename the active composition from `simulation_runtime.*` to
  `ordinary_runtime.*`; clearly historical plan/evidence text retains its old
  symbol names.

### Remediation validation and outcome

- Focused validity, no-cache, and IDLE initialization-failure host tests: PASS.
- Architecture and traceability guardrails plus `git diff --check`: PASS at the
  first implementation checkpoint.
- Full host/sanitizer, ESP-IDF, fresh ordinary/diagnostic, strict-C++20,
  size/partition/dependency/flash rejection, ELF/source composition, offline
  clean-checkout, and final stale-language audits: PASS. Historical plan and
  evidence references retain their former runtime names intentionally.

## Execution log and outcome — 2026-08-24

- Added centralized evidence-classified production configuration, real ESP
  monotonic clock, explicit non-copyable SPI-bus ownership, exact register
  access, and checked descriptor-before-bus teardown.
- Ordinary composition now uses MAX31865 for chamber sensing, simulated food
  probes and heater output, deterministic control, inactive PID, one
  `ControlTask`, and the existing synchronous safety gate.
- Startup initializes bus/descriptor/exact `0xD1`, waits beyond the 66 ms
  boundary, and creates `ControlTask` only afterward on the healthy path.
  Bus/pull, descriptor/configuration, or boundary failure instead leaves the
  chamber source unavailable and continues into the ordinary runtime so the
  first IDLE tick exposes the latched FAULT and OFF state. Later driver/fault/
  non-finite/out-of-range results also become absence and cannot reuse a cached
  temperature.
- Architecture and traceability guardrails pass; `git diff --check` passes.
- All 12 host groups pass in ordinary and sanitizer builds. Focused M7 covers
  invalid policy construction, inclusive boundaries, raw-zero rejection,
  initialization failure while IDLE, out-of-range no-cache behavior after a
  valid sample, and later driver failure.
- `./tools/verify.sh --idf-only` passes ESP-IDF v6.0.2 build, effective config,
  strict C++20, partitions, size, and unsigned-flash-target rejection.
- Fresh ordinary build
  `build-m7-remediation-smoke` passes strict C++20 for 32 project
  sources. Its unsigned application is 1,441,792 / 3,145,728 bytes (45.8%),
  SHA-256
  `a56d4b2114b9aaef980e4cf6b93502f3ea25708c18a82ca47715c7f1f40cccd7`.
- Fresh diagnostic-overlay build
  `build-m7-remediation-diagnostic` passes strict C++20 for 33 project
  sources. Its unsigned application is 262,144 / 3,145,728 bytes (8.3%),
  SHA-256
  `6b90a997bb8cc508122488a77cb7b3cb5f34a54fe769707534bc980cd91312c6`.
- Both generated partition tables match SHA-256
  `fc2d47b7e29632ea559f93af4694854ed158e2fa548dcb09162365f950708432`.
- ELF audits retain application tick, ordinary runtime, ControlTask, MAX31865
  bus/backend/sensor, real clock, deterministic controller, simulated food,
  and simulated heater only in the ordinary composition. The diagnostic ELF
  retains its diagnostic owners/entrypoint and excludes the application,
  ordinary runtime, ControlTask, heater/controller, and production MAX31865
  adapter/backend. Only the overlay compile database contains the diagnostic
  source.
- No board-facing, signing, release, or repository-history action was run.
