# Smoker Controller — Agent Instructions

## Mission

Build a reliable smart smoker controller for ESP32-S3 using native ESP-IDF and modern C++.

The repository documentation is the source of truth. Before making changes, read:

1. `docs/BUSINESS_RULES.md`
2. `docs/ARCHITECTURE.md`
3. `docs/SAFETY.md`
4. `docs/DATA_MODEL.md`
5. `docs/DECISIONS.md`
6. `docs/ROADMAP.md`
7. `docs/TRACEABILITY.md`

For large or multi-step work, also read `.agent/PLANS.md` and create/update an execution plan before implementation.

## Technology decisions

- Framework: native ESP-IDF.
- Target family: ESP32-S3.
- ESP-IDF baseline: pin to `v6.0.2` unless explicitly changed.
- Project-owned C++ baseline: strict C++20 on host and ESP-IDF target.
- Do not use PlatformIO.
- Do not use Arduino unless explicitly approved later.
- Keep `main/app_main.cpp` as a thin composition/bootstrap layer.

## Architecture rules

Use exactly these logical layers:

- `smoker_core`: platform-independent domain/control logic.
- `smoker_app`: orchestration, runtime-state ownership, commands, snapshots, application ports.
- `smoker_platform`: ESP-IDF and hardware-specific implementations.

Dependency direction:

`main -> smoker_app -> smoker_core`

`main -> smoker_platform`

`smoker_platform` implements ports declared by `smoker_app`.

Rules:

- `smoker_core` must not depend on ESP-IDF, FreeRTOS, GPIO, SPI, I2C, Wi-Fi, NVS, OTA, or hardware drivers.
- Hardware access must not appear in `smoker_core`.
- `SmokerApplication` is the single writer/owner of mutable runtime state.
- External actors modify state only through commands.
- UI/network consumers receive snapshots/events, never mutable runtime objects.
- V0 uses one critical `ControlTask`.
- Safety evaluation runs synchronously in the critical control cycle before the final heater command.
- Do not create separate PID, sensor, timer, or safety tasks in V0.
- Network, display, OTA, persistence, and telemetry must never become dependencies of the heater-control loop.
- In M5, `SmokerApplication::submit()` is single-owner/ControlTask-only; do not call it concurrently or add external producers before their roadmap milestone.

## Implementation discipline

Implement the simplest design required by the current roadmap milestone.

Do not:

- scaffold future features merely because they are documented;
- implement multi-stage recipes in V0;
- invent GPIO mappings;
- invent board flash/PSRAM size;
- invent hardware capabilities;
- implement fan or smoke-generator control before those features are explicitly designed;
- add a separate food-safety engine;
- add cloud dependencies to local control;
- modify generated `sdkconfig` manually.

Use `sdkconfig.defaults` for versioned project configuration.

Before adding an external dependency, prefer in this order:

1. ESP-IDF built-in functionality;
2. ESP Component Registry;
3. explicit Git/component dependency.

Document why a new dependency is required.

## C++ rules

Prefer:

- RAII;
- explicit ownership;
- strong domain types where useful;
- `std::optional` for absent values;
- monotonic time for durations/timers;
- deterministic control-loop behavior.

Avoid by default:

- exceptions;
- RTTI;
- global mutable state;
- unnecessary dynamic allocation in the control loop;
- I/O or blocking network/storage work in the critical control loop.

Collections/configuration may allocate during initialization; avoid repeated heap allocation inside `tick()`.

## Safety rules

Safety overrides recipe, user, and controller commands.

Every boot/reset begins with heating disabled.

An invalid authoritative chamber measurement causes `FAULT` and heater OFF.

A latched V0 fault must not automatically resume heating.

No API/UI/debug command may bypass safety to force heater ON.

Firmware and SSR are not considered sufficient independent safety protection; hardware protection is designed separately.

Never claim hardware safety was tested when only simulation/build/unit tests were run.

## Testing and validation

For every meaningful implementation change:

1. inspect existing code;
2. state assumptions briefly;
3. implement only the requested milestone;
4. build;
5. run relevant tests;
6. fix errors introduced by the change;
7. report what remains unverified on real hardware.

Primary test strategy:

- fast native host tests for `smoker_core`;
- target/integration tests for ESP-IDF/platform code.

Business rules in `docs/BUSINESS_RULES.md` require tests when implemented.

## Git

Keep changes focused.

Do not overwrite unrelated user changes.

Do not perform major architectural changes without documenting the reason in `docs/DECISIONS.md`.

Future items in `docs/ROADMAP.md` are not permission to implement them early.
