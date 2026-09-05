# ESP architect review findings remediation

## Goal

Implement the five findings from the current review with focused fixes and
regression evidence, retaining the existing three-layer architecture.

## Scope

- Make the MAX31865 first-conversion bootstrap wait independent of RTOS tick phase.
- Preserve a fault-terminated session's original stop time when clearing the fault.
- Remove readings from probes disabled by restored session defaults before timer evaluation.
- Keep Blynk semantic feedback through queue saturation and handle service-result capacity.
- Give the captive DNS worker a real, bounded opportunity to exit before task-storage reuse.

## Non-goals

No new security layers, dependencies, hardware mappings, real heater/PID/ADS1115
activation, persistence milestone implementation, flashing, or release publication.

## Current repository observations

Baseline commit: `580d129`; initial worktree clean. M0-M5 software and M7 functional
activation are complete; M12/M14/M15 retain their documented target-validation
gates. M6B/M8-M10 remain incomplete. Ordinary food probes and heater are simulated.
The review reproduced the two application defects, Blynk feedback loss, and the
MAX31865 tick-phase failure; DNS uses a zero-tick delay at the configured 100 Hz.

## Assumptions

ESP-IDF stays at v6.0.2 and project C++ stays at C++20. ControlTask remains the only
runtime-state writer; Stop admission and synchronous safety remain authoritative.
No new hardware facts or physical validation are inferred from host/build results.

## Steps

1. [x] Revalidate worktree, instructions, milestone scope, and relevant contracts.
2. [x] Implement and regression-test the MAX31865 bootstrap calculation.
3. [x] Implement and regression-test both application lifecycle fixes.
4. [x] Implement bounded Blynk result retention/admission and saturation regressions.
5. [x] Implement bounded nonzero-tick DNS shutdown and lifecycle regressions.
6. [x] Review integrated changes; update architecture/traceability evidence where needed.
7. [x] Run host tests, ASan/UBSan, guardrails, and an isolated ESP-IDF target build.
8. [x] Audit all five fixes against their original reproducers and report remaining target gates.

## Validation commands

- `bash tools/verify.sh --host-only`
- `SMOKER_VERIFY_BUILD_DIR=<isolated temporary build> bash tools/verify.sh --idf-only`
- Focused regression executables and source-to-target wiring guardrails as needed.
- `git diff --check` and final source/test review.

## Risks / unresolved items

RTOS scheduler models and source checks do not establish physical timing or radio
behavior. Boot/OTA with the connected MAX31865, rapid AP transitions, and Blynk
backlog/reconnect on the target remain hardware/integration follow-ups. Existing
SSR, calibration, and recovery gates remain unchanged. No architecture decision
change is expected; any implementation adjustment will be recorded here.

## Completion evidence

- MAX31865 uses the shared phase-tested `ceil(ms * Hz / 1000) + 1` calculation:
  eight ticks for 66 ms at 100 Hz. The regression covers 55/66 ms, 100/1000/1024
  Hz, and 10,000 phases per tick; a temporary mutation to the former formula
  failed the new assertion. Runtime wiring is checked by the architecture tool.
- Application regressions failed eight assertions against the original source
  and passed against the fixes, including frozen session/timer duration,
  disabled-probe freshness, Stop behavior, and allocation-free Start.
- Blynk shares 16 slots between pending reservations and queued feedback.
  Tests retain the observed semantic result after snapshot overwrite, resolve
  firmware service rejection with the other 15 slots occupied, reject capacity
  theft, and exercise cancellation and reconnect cleanup. Target source checks
  bind reservations to command/firmware admission and retain the Stop exception.
  The Stop capacity message says queued, not semantically accepted.
- DNS tests exercise the production wait helper, worker progress only after a
  nonzero tick, no-worker/already-exited cases, 250 ms receive cleanup, and the
  400 ms deadline. A temporary zero-tick mutation failed the new regression.
  Independent review confirmed exit-before-reuse ownership remains intact.
- `bash tools/verify.sh --host-only`: PASS, 12/12 ordinary and 12/12 ASan/UBSan
  suites, plus release/HTTP fixture and architecture/traceability checks.
  The final strengthened M15 test was rebuilt and rerun in both host variants:
  PASS. Host log: `/tmp/smoker-findings-host.log`.
- Clean ESP-IDF v6.0.2 build in `/tmp/smoker-findings-idf.HaSwJW`, followed by
  final incremental verification after the Stop wording adjustment: PASS.
  Effective configuration, all 33 project C++ sources, partitions, image size,
  and unsigned-flash rejection checks passed. Image: 1,441,792 bytes (45.8% of
  a 3 MiB slot). Final log: `/tmp/smoker-findings-idf-final.log`.
- `git diff --check`: PASS. Architecture, data-model, and traceability docs now
  describe the corrected behavior without extending target/hardware sign-offs.

Changed production files are the application implementation, MAX31865 policy
header/runtime, DNS support header/connectivity implementation, and Blynk
result header/support/service. Tests changed only in the existing V0, M7, M12,
and M15 host suites. The architecture guardrail, three contract documents, and
this plan provide review evidence. No new architecture decision, dependency,
hardware activation, security layer, flash operation, or release was introduced.
