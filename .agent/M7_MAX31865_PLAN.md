Historical status note

This plan records the earlier dependency-import checkpoint. Its blanket
adapter-development gate was refined by
`.agent/M7_MAX31865_SOFTWARE_INTEGRATION_PLAN.md` on 2026-08-21: inactive
adapter development and cross-build evidence are now allowed, while production
activation, bus/pin configuration, wiring, and hardware validation remain
gated by the missing M6B facts.

The 2026-08-21 freshness audit further requires the inactive target backend to
distinguish descriptor/configuration success from sample readiness. The
software-integration plan now records the nonblocking, filter-dependent
first-conversion boundary and post-reconfiguration reset work; this historical
dependency-import checkpoint does not supersede it.

Goal

Select and import a maintained MAX31865 ESP-IDF driver without rewriting the
chip protocol, while preserving the M6B hardware-evidence gate and the M7
authoritative-sensor safety contract.

Scope

- evaluate ESP-IDF, ESP Component Registry, and Git driver options;
- pin the selected registry component and its lockfile hash;
- make the selected driver part of the ESP32-S3 build dependency graph;
- document the selection, known software facts, and missing physical facts;
- validate repository guardrails and an ESP-IDF 6.0.2 cross-build.

Non-goals

- no GPIO assignment;
- no assumed breakout-board schematic, reference resistor, PT100/PT1000 type,
  or 2/3/4-wire selection;
- no real `IChamberSensor` adapter or production runtime activation before the
  physical component record is complete;
- no claim of sensor accuracy, fault behavior, or thermal validation without
  connected hardware tests.

Current repository observations

- M6A is complete; the chamber part of M6B and M7 are still open.
- `IChamberSensor` already represents the required application boundary.
- production currently composes `SimulatedChamberSensor`.
- ESP-IDF 6.0.2 does not bundle a MAX31865 driver.
- ESP Component Registry provides `esp-idf-lib/max31865` 1.0.8 for ESP32-S3;
  its release includes the ESP-IDF 6 driver-component split fix and BSD-3
  licensing.
- the selected component's blocking 70 ms `max31865_measure()` convenience API
  is unsuitable for direct use by the final critical-cycle adapter; M7 will
  use a nonblocking filter-dependent readiness boundary around provisional
  continuous reads, subject to physical frontend evidence.

Assumptions

- the user-selected first external sensing frontend contains a genuine
  MAX31865-compatible device. PT100 and three-wire operation are confirmed, but
  its exact module revision and remaining RTD assembly facts are not documented.
- 50 Hz rejection is likely relevant in Romania, but remains a configuration
  decision until the physical setup is recorded and tested.

Steps

1. Add exact `esp-idf-lib/max31865 ==1.0.8` component dependency.
2. Regenerate and inspect `dependencies.lock` for the registry hash.
3. Record the driver decision and partial M6B status without claiming M7.
4. Run architecture/traceability checks and the ESP-IDF 6.0.2 cross-build.
5. Confirm the selected driver's source is compiled in the target build.

Validation commands

- `python3 tools/check_architecture.py`
- `python3 tools/check_traceability.py`
- `git diff --check`
- `tools/verify.sh --idf-only`
- inspect target `compile_commands.json` for `max31865.c`

Risks / unresolved items

- exact MAX31865 module manufacturer/revision and schematic;
- PT100 accuracy class, rated range, sheath, and cable construction;
- actual reference-resistor value/tolerance fitted to the module;
- selected SPI host/GPIO and carrier boot-state checks;
- initialization, open/short fault injection, accuracy, noise, and sustained
  runtime evidence on the connected final hardware.

Completion result

- `esp-idf-lib/max31865` 1.0.8 is exact-pinned in the platform manifest and
  locked to registry hash
  `c7a027843a3f9cf4b06e7e216b25b2089115568f288c8682defd84c018a5b80f`.
- the fresh ESP-IDF 6.0.2 ESP32-S3 build compiled `max31865.c` and passed all
  IDF-only verification gates.
- D056 and the hardware/roadmap/traceability records preserve the physical M6B
  gate and fail-OFF M7 policy.
- production remains on `SimulatedChamberSensor`; the unresolved physical
  facts above are required before activating the adapter.
- PT100 with three leads is now confirmed, fixing the future driver fields to
  `rtd_nominal = 100.0F` and `MAX31865_3WIRE`.
- The subsequent freshness remediation adds the official 55 ms/66 ms monotonic
  first-conversion boundary before any register read and after every successful
  continuous reconfiguration. It is host/sanitizer and target-cross-build
  evidence only; additional physical settling and SPI timing remain pending.
