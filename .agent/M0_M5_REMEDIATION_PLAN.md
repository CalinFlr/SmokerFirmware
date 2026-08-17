# M0-M5 Review Remediation Plan

Status: **Complete — 2026-08-16**

## Goal

Resolve the independent-review findings for the completed M0-M5 simulated
application/control slice without starting M6 or adding real-hardware features.

## Scope

- make the M0-M5 simulated-slice boundary explicit and consistent;
- add rule-to-code-to-test traceability with validation levels;
- restore `main/app_main.cpp` to a thin composition/bootstrap role;
- make command, probe, alarm, and monotonic-time semantics deterministic;
- extend host tests for the clarified M5 behavior;
- validate the ESP-IDF build and native host tests, including sanitizers.

## Non-goals

- no real board, GPIO, chamber sensor, food-probe frontend, or SSR driver;
- no physical safety limit or hardware capability assumptions;
- no persistence, power recovery, Wi-Fi, display, OTA, telemetry, fan, smoke
  generator, multi-stage recipe, or cloud implementation;
- no claim of target-runtime or physical safety validation.

## Current repository observations

- M0-M5 behavior builds and its existing host tests pass at commit `eef91e8`;
- documentation calls M5 a complete simulated slice while some product-V0
  requirements are explicitly deferred to M10 and M13;
- `START_CODEX.md` still presents the repository as an M0 seed;
- `main/app_main.cpp` owns the FreeRTOS control task and simulation runtime;
- raw probe acquisition currently also derives events/alarms before commands;
- the fixed command queue has no documented ownership/overflow/Stop contract;
- probe defaults and active-session probe settings share one mutable object;
- alarm acknowledgement and condition resolution are not separate concepts;
- `Duration` and monotonic timestamps use the same C++ type.

## Assumptions

- the active scope is M5 remediation; M6 remains not started;
- M5 has one command producer/owner: the critical control task; future external
  producers require a later transport adapter and must not call `submit()`
  concurrently;
- probe target/disconnection alarms are active-session behavior; connectivity
  events may still describe enabled probes outside a session;
- a target notification is latched once per session/target configuration;
- acknowledging an alarm and resolving its underlying lifecycle are distinct;
- the numeric `150 C` maximum remains simulation input only.

## Steps

1. Introduce a platform-owned ESP-IDF simulation runtime and leave `app_main`
   with only configuration composition and runtime startup.
2. Replace duration-shaped monotonic timestamps with a distinct time-point type.
3. Split raw input acquisition from derived probe state evaluation so queued
   commands deterministically affect same-cycle alarms.
4. Separate immutable probe defaults from mutable active-session settings and
   reset the latter on every explicit Start.
5. Define alarm acknowledgement versus resolution, reconnect behavior, and
   suppression outside `RUNNING`; expose only unresolved alarms as active.
6. Reserve command-queue admission for Stop, coalesce a trailing duplicate Stop,
   and expose/report normal-command overflow without adding concurrency.
7. Extend host tests for same-cycle commands, alarm lifecycle, reconnect,
   per-session probe configuration, queue overflow, and Stop admission.
8. Update architecture/data/safety/decision/status documentation and add a
   complete M0-M5 traceability matrix.
9. Run all validation commands and audit every goal requirement against current
   files and results.

## Validation commands

```sh
export IDF_TOOLS_PATH="$PWD/.tools/espressif"
. "$PWD/.tools/esp-idf-v6.0.2/export.sh"
idf.py build

cmake -S tests -B build-host -G Ninja
cmake --build build-host
ctest --test-dir build-host --output-on-failure

cmake -S tests -B build-host-sanitize -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
  -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined'
cmake --build build-host-sanitize
ctest --test-dir build-host-sanitize --output-on-failure
```

## Risks / unresolved items

- FreeRTOS task, stack-watermark, watchdog timeout/reset, and reset-reason
  behavior remain build-validated only until an actual ESP32-S3 board is known;
- command transport for future UI/network tasks remains deliberately deferred;
- persisted defaults/recovery remain M10 work even though runtime/default
  ownership is clarified now;
- electrical safe state, sensor validity policy, SSR switching, and independent
  protection remain blocked on M6 hardware identification.

## Outcome

- `main/app_main.cpp` is a 52-line composition/bootstrap layer; ESP-IDF runtime
  mechanics moved to `smoker_platform`;
- command ordering/admission, alarm lifecycle, probe defaults/session settings,
  and monotonic time-point semantics are implemented and host-tested;
- documentation now distinguishes the M0-M5 simulated slice from product V0 and
  `docs/TRACEABILITY.md` maps every BR/SR/RR/TR/SF rule;
- ESP-IDF v6.0.2 ESP32-S3 build: pass;
- native host CTest: 5/5 pass;
- ASan/UBSan host CTest: 5/5 pass;
- target runtime and physical hardware validation remain pending at M6+ exactly
  as listed above.
