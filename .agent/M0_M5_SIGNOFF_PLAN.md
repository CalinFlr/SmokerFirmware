# M0-M5 Sign-off Stabilization Plan

Status: **Complete — 2026-08-16**

## Goal

Make the completed M0-M5 simulated application/control slice reproducible and
ready for sign-off without beginning M6 or claiming target-runtime/hardware
validation.

## Scope

- use strict C++20 consistently for host and project-owned ESP-IDF sources;
- reject every ESP-IDF version except exactly `v6.0.2` in both direct CMake and
  the verification entrypoint;
- document the M5 command-admission and probe-timer contracts;
- test disabled/disconnected probe timer behavior, unknown test groups, and a
  representative allocation-instrumented control tick;
- record deferred resource-limit decisions without inventing hardware values;
- pin CI runners/actions reproducibly;
- update only affected M0-M5 documentation and traceability.

## Non-goals

- no M6 board, pins, flash/PSRAM, sensors, SSR, or electrical design;
- no persistence, recovery, UI/network command transport, OTA, or telemetry;
- no new command-result API or cross-task synchronization;
- no claim that host tests or cross-builds validate physical safety.

## Current repository observations

- the P0 Stop barrier, architecture checks, traceability checks, host tests,
  sanitizers, and ESP-IDF cross-build already pass in the current worktree;
- project-owned host code is strict C++20 while the ESP-IDF target currently
  inherits `gnu++26`;
- `tools/verify.sh` accepts an IDF version by substring and direct CMake has no
  exact version gate;
- `submit()` admission versus semantic processing is not explicit enough in the
  source documentation;
- current probe-timer behavior waits for a valid enabled probe reading but lacks
  an explicit contract/regression test;
- an unknown `smoker_v0_tests` group currently exits successfully;
- CI uses mutable runner/action references;
- required runtime, traceability, verification, and CI files are present but the
  aggregate worktree is not committed, as explicitly requested.

## Assumptions

- C++20 is sufficient for every current project-owned source file;
- a disabled or disconnected timer-source probe is an absent non-authoritative
  reading, so the timer waits without raising a heater-control fault;
- re-enable/reconnection may start an unstarted timer when the selected probe
  reading meets its threshold;
- concrete device limits for probe count, name/recipe size, and persisted input
  remain M9/M10 decisions; M5 uses trusted bounded startup configuration;
- no commit or staging operation is authorized by this task.

## Steps

1. Add exact IDF and C++20 build contracts and verify target compile commands.
2. Clarify command admission, probe-timer, allocation-evidence, and deferred
   resource-limit semantics in source/docs.
3. Add focused timer, invalid-group, and representative heap-instrumented tests.
4. Pin CI runner/actions and strengthen local verification/guardrails.
5. Update traceability and execute clean host, sanitizer, ESP-IDF, diff, absence,
   and patch-completeness checks.

## Validation commands

```sh
tools/verify.sh --host-only
tools/verify.sh --idf-only
build-host/smoker_v0_tests invalid-group  # must fail
python3 tools/check_architecture.py
python3 tools/check_traceability.py
git diff --check
```

Additionally inspect `build/compile_commands.json` to prove project-owned target
C++ sources use `-std=c++20` and do not retain `-std=gnu++26` as their effective
last language-standard option.

## Risks / unresolved items

- regex/source guardrails remain structural checks rather than a C++ AST proof;
- allocation instrumentation observes replaceable ordinary/aligned C++
  allocation functions on the tested host paths, not all possible libc or
  target-runtime allocation mechanisms;
- CI configuration cannot be executed locally as GitHub Actions;
- target scheduling/watchdog/reset/stack behavior and all electrical/thermal
  behavior remain pending at M6+.

## Outcome

- the aggregate M0-M5 patch is complete in the worktree and intentionally
  remains unstaged/uncommitted;
- strict C++20 is enforced for host and all nine project-owned ESP-IDF C++
  translation units;
- direct CMake and the verification entrypoint require exactly ESP-IDF 6.0.2;
- command admission, probe-timer availability, allocation-observation scope,
  and deferred M9/M10 resource limits are documented and traced;
- focused probe-timer, unknown-group, and representative control-tick tests are
  present and invoked;
- CI uses explicit Ubuntu 24.04 runners and immutable action commit SHAs;
- `tools/verify.sh` passed end to end: architecture and traceability guardrails,
  clean host build, 5/5 host tests, unknown-group rejection, clean sanitizer
  build, 5/5 ASan/UBSan tests, ESP-IDF 6.0.2 cross-build, and target C++20
  compile-command verification;
- `git diff --check`, shell/Python syntax checks, untracked-file whitespace
  checks, future-scope absence review, and documentation-claim review passed;
- no target-runtime, board, electrical, thermal, or physical safety behavior was
  validated, and M6 remains unimplemented.
