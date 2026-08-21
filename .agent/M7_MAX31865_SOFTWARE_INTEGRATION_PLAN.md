# M7 MAX31865 Software Integration Plan

Status: freshness-boundary remediation implemented and host/sanitizer plus
ESP-IDF 6.0.2 cross-build validated. The inactive adapter remains software-only,
with no connected MAX31865/PT100 or hardware validation. M6B and M7 remain
incomplete.

## Goal

Implement and cross-build the inactive `smoker_platform` integration boundary
for the already pinned `esp-idf-lib/max31865` 1.0.8 driver behind
`smoker::app::IChamberSensor`, while retaining simulated production composition.

## Scope

- A small host-testable MAX31865 result/configuration policy and chamber-sensor
  adapter in `smoker_platform`.
- A target-only RAII backend that acquires/configures the real driver descriptor
  and performs synchronous continuous-conversion reads through the pinned API.
- Focused host behavior/allocation tests and source architecture guardrails.
- A host-testable monotonic first-conversion readiness policy used by the
  target backend after every successful continuous configuration.
- Precise M7/M6B documentation updates at host and cross-build evidence strength.

## Non-goals

- Runtime activation, SPI-bus initialization, GPIO assignment, wiring, flashing,
  monitoring, provisioning, calibration, or electrical/thermal validation.
- ADS1115, PID, SSR/heater output, fan, smoke-generator, or unrelated cleanup.
- Inventing a breakout identity, reference resistor, supply/logic behavior,
  bus ownership, filter/standard choice, or physical validity limits.

## Current repository observations

- M15 reconnect remediation was validated and checkpointed as commit
  `ddd4b2666a744a30e4787ae7125dc50f1ccad43d`; the worktree was clean before M7.
- `IChamberSensor` already returns `std::optional<core::Temperature>` and the
  application already latches `ChamberSensorInvalid` and forces heater OFF for
  an absent authoritative reading.
- `esp-idf-lib/max31865` is exactly pinned at 1.0.8 and locked at component hash
  `c7a027843a3f9cf4b06e7e216b25b2089115568f288c8682defd84c018a5b80f`.
- The managed 1.0.8 source shows `max31865_measure()` performs
  `vTaskDelay(pdMS_TO_TICKS(70))`; `max31865_read_temperature()` performs a
  synchronous raw SPI read/conversion without a delay.
- The official MAX31865 datasheet gives RTD MSB/LSB POR values of zero and
  maximum first-conversion times of 55 ms for the 60 Hz notch and 66 ms for
  the 50 Hz notch. Driver 1.0.8 converts raw zero to a finite value near
  -242.02 C and returns success, so configured is not sample-ready.
- Production `main` delegates to `start_simulation_runtime()`, which owns a
  `SimulatedChamberSensor`; no real SPI bus or external GPIO is configured.

## Assumptions

- Confirmed sensor facts are PT100 nominal 100 ohm and three-wire connection.
- Breakout manufacturer/revision, fitted reference resistor/tolerance,
  supply/logic behavior, SPI host/ownership, CS/MOSI/MISO/SCLK pins, filter,
  RTD standard, and hardware validity/calibration limits remain unknown.
- Continuous conversion with bias enabled is the smallest delay-free read
  boundary supported by driver 1.0.8. It is provisional integration intent,
  not a tuned or physically validated conversion strategy.
- The datasheet first-conversion maximum is the minimum software freshness
  boundary. Any additional module/input-network or bias-settling interval is
  still unknown and must remain explicit hardware-pending evidence.
- SPI bus initialization and ownership remain outside this inactive device
  backend and cannot be composed until M6B supplies concrete bus/pin facts.

## Steps

1. Add a platform-neutral backend seam, configuration validation, and
   `IChamberSensor` adapter which never caches a previous valid value.
2. Add a target-only RAII backend using the real 1.0.8 descriptor/config/read/
   fault/free APIs in automatic conversion mode, with required explicit config.
3. Add focused host tests for valid/non-finite/failure/recovery behavior,
   application safety latching, and observed ordinary C++ allocation-free reads.
4. Extend architecture guardrails for layer confinement, real target API use,
   production simulation, and prohibition of `max31865_measure()`/delays in the
   critical integration path.
5. Update ROADMAP, ARCHITECTURE, HARDWARE, DECISIONS, and TRACEABILITY without
   claiming M6B/M7 completion or physical validation.
6. Run focused tests, complete host/sanitizer validation, ESP-IDF 6.0.2
   cross-build, diff whitespace checks, and final dirty-tree review.
7. Remediate the reviewed first-conversion defect with an explicit `NotReady`
   result, a fake-clock-tested 55/66 ms monotonic policy, and a target read
   guard before any fault/temperature register access. Reset that boundary
   after initialization, successful reconfiguration, and conservative fault
   recovery; never reuse a prior sample.
8. Correct documentation so descriptor/configuration success is not described
   as sample readiness, and source/host evidence is not described as proof of
   real SPI boundedness or target allocation behavior.

## Validation commands

```text
cmake --build build-host --target smoker_m7_tests
ctest --test-dir build-host --output-on-failure -R smoker_v0.m7_max31865
python3 tools/check_architecture.py
./tools/verify.sh --host-only
./tools/verify.sh --idf-only
git diff --check
git status --short
```

## Risks / unresolved items

- Cross-build proves only source/API compatibility with ESP-IDF 6.0.2 and the
  pinned component; it does not prove SPI timing, conversion cadence, accuracy,
  noise, wiring, fault behavior, recovery, or sustained operation.
- The driver call is synchronous over an already initialized SPI bus. Actual
  worst-case timing must be measured on the selected module/bus before runtime
  activation.
- The official first-conversion maximum does not determine module-specific RC
  settling, bias/input-network settling, bus ownership, or physical accuracy.
- Continuous conversion, bias behavior, filter, RTD standard, reference value,
  and physical validity/calibration policy all require the missing M6B facts and
  connected M7 evidence.
- M6B and M7 remain incomplete after this software-only integration.

## Execution log

- 2026-08-21: Inspected the managed 1.0.8 header/source without modifying it.
  Confirmed `max31865_measure()` contains `vTaskDelay(70 ms)` and selected an
  inactive continuous-conversion boundary over the delay-free fault and
  temperature APIs.
- 2026-08-21: Added the host-safe policy/adapter and target-only RAII backend.
  All unknown software-selectable hardware values remain explicit required
  configuration; production composition remains simulated and no SPI bus or
  GPIO assignment was added.
- 2026-08-21: Added focused host cases for configuration/initialization/read/
  fault/non-finite behavior, no last-value reuse, later-value recovery with a
  latched application fault, and observed ordinary C++ allocation-free reads.
- 2026-08-21: `./tools/verify.sh --host-only` passed all 10 normal and all 10
  ASan/UBSan groups. `./tools/verify.sh --idf-only` passed with ESP-IDF exactly
  6.0.2, strict C++20 across 26 project sources, the real pinned API backend,
  all guardrails, and a 1,376,256 / 3,145,728-byte image (43.8%).
- 2026-08-21: Remediated first-conversion freshness with explicit
  `ConfiguredAwaitingFirstSample`/`NotReady` semantics and a fake-clock-tested
  monotonic 55 ms/66 ms policy used before target fault/temperature reads.
  Successful initialization, reinitialization, reconfiguration, and fault
  recovery reset the boundary. Focused, full normal, full ASan/UBSan,
  architecture, traceability, and ESP-IDF 6.0.2 gates passed.
- 2026-08-21: Documentation now distinguishes project-owned absence of explicit
  waits/allocations from unproven ESP-IDF/driver/SPI allocation and worst-case
  blocking behavior. Bus ownership/timing, bias/input settling, accuracy, and
  physical behavior remain M6B/M7 pending.
- 2026-08-21: No flash, monitor, provisioning, NVS erase, connected sensor,
  electrical, thermal, calibration, or sustained hardware action was performed.
