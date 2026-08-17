# Start Codex Here

This repository has completed the M0-M5 V0 simulated application/control slice.
It is no longer an M0 seed package.

## Current state

- M0-M5: complete at host-test/ESP-IDF-build validation levels;
- active maintenance scope: preserve and improve the simulated slice;
- next roadmap milestone: M6, not started and blocked until exact hardware is
  available;
- real controller, target-runtime, electrical, and hardware-safety behavior is
  not yet validated.

`V0 simulated slice complete` does not mean `product V0 complete`. Persistence,
power recovery, real sensors, real SSR output, and independent protection remain
at their explicit future milestones.

## First instruction for a new Codex task

```text
Read AGENTS.md and every document it references before making changes.

Read docs/TRACEABILITY.md to determine which rules are implemented, deferred,
and validated only on host/build versus target/hardware.

Current completed scope: M0-M5 simulated application/control slice.
Next milestone: M6, which must not start until exact hardware is available.

Inspect the repository and current git diff first. Implement only the requested
scope. Do not invent board details, GPIO, flash/PSRAM, sensors, SSR behavior,
physical safety limits, or hardware test results. Keep main/app_main.cpp thin,
preserve the single-writer/single-ControlTask model, and keep every heater write
behind the synchronous safety gate. Preserve exact ESP-IDF v6.0.2 and strict
C++20 for project-owned host/target code unless a new accepted decision changes
those baselines.

For a multi-step change, create/update an execution plan following
.agent/PLANS.md. Build and run relevant tests, then report validation levels and
everything still unverified on real hardware.
```

## Required read order

1. `AGENTS.md`
2. `docs/BUSINESS_RULES.md`
3. `docs/ARCHITECTURE.md`
4. `docs/SAFETY.md`
5. `docs/DATA_MODEL.md`
6. `docs/DECISIONS.md`
7. `docs/ROADMAP.md`
8. `docs/TRACEABILITY.md`

Future roadmap entries are not permission to implement them early.
