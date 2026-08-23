# Inactive M8 PID software-integration plan

## Goal

Implement the first inactive M8 software slice around the exact-pinned official
`espressif/pid_ctrl` 0.3.1 component. Route requested chamber demand through an
application-owned synchronous controller port while production remains on the
existing deterministic 100/0 controller and simulated heater output.

## Scope

- add and resolve `espressif/pid_ctrl ==0.3.1` only in `smoker_platform`;
- introduce the smallest ESP-IDF-free `IChamberController` application port;
- make `SmokerApplication` own controller eligibility, reset/failure handling,
  synchronous safety ordering, and the sole final heater write;
- add a deterministic platform adapter around the existing M2 core controller;
- add a host-testable float PID adapter and a target-only non-copyable RAII
  backend over the exact 0.3.1 float lifecycle/compute/reset APIs;
- represent positional accumulated-error bounds separately from incremental
  retained-output saturation, with no ignored project-visible incremental
  bound promise;
- harden construction/reset lifecycle ordering and the optional managed-source
  semantic guardrail identified by review;
- add focused M8 host tests, executable architecture guardrails, traceability,
  and evidence-backed documentation.

## Non-goals

- no production PID composition or activation, SSR GPIO, heater GPIO write,
  time-proportioning/window logic, real chamber/probe composition, or board
  action;
- no production gains, positional accumulated-error bounds, common output
  limits, calculation form, sample period, derivative policy, or tuning
  recommendation;
- no Arduino, AutoPID, autotuning library, relay autotune, plant identification,
  excitation behavior, new task, logging, I/O, wait, or delay in PID paths;
- no change to the final M7 MAX31865 diagnostic or its SPI2/GPIO mapping;
- no claim of target runtime, thermal-control, timing, tuning, electrical, or
  independent-safety validation. M6B, M7, M8, and M9 remain incomplete.

## Current repository observations

- HEAD began at `3a519dfc6f6276e81d1ab2fe54f6f75f292a10be` with an empty
  `git status --short`, satisfying the task preconditions.
- `SmokerApplication::tick()` currently calls the core deterministic controller
  directly, safety-gates its request, and owns the only post-boot heater write.
- All application fixtures and the target simulation runtime construct
  `SmokerApplication` explicitly; production uses `SimulatedChamberSensor`,
  `SimulatedFoodProbeSource`, and `SimulatedHeaterOutput`.
- The official 0.3.1 registry archive declares ESP-IDF `>=4.4` and
  `espressif/iqmath ^1.11.0`. It exposes float and IQmath backends and explicit
  positional/incremental forms. It exposes no autotuning, plant-identification,
  sample-period, or delta-time API.
- The reviewed float implementation allocates its control block with `calloc()`
  during `pid_new_control_block_f()`. Valid `pid_compute_f()` and
  `pid_reset_ctrl_block_f()` paths update existing fields without intentional
  allocation; deletion uses `free()`.
- Positional form accumulates raw error per call, clamps that accumulator with
  `min_integral`/`max_integral`, multiplies it by Ki, and differentiates error.
  Incremental form never reads those fields; it updates and clamps retained
  output. Both forms differentiate error, expose no `dt`, derivative filter, or
  derivative-on-measurement surface, and therefore require setpoint-kick review.
- `Temperature` and `HeaterDemand` are floats, and ESP-IDF 6.0.2 records
  `SOC_CPU_HAS_FPU 1` plus a single-precision DFPU for ESP32-S3. The float
  backend therefore avoids an unnecessary numeric conversion boundary; IQmath
  remains an available upstream backend but is not selected for this slice.

## Assumptions

- Controller eligibility means a valid configuration, no active fault, a
  `RUNNING` session, an authoritative chamber value, and an active chamber
  target. Every transition out of eligibility resets/disables latent state.
- A controller compute or reset failure is safety-relevant and latches
  `ControlLoopFailure`; heater demand remains OFF. A later successful reset may
  make that condition clearable, but clearing never restarts the session and a
  new explicit Start remains required.
- PID test coefficients are named fixtures only. They are API/behavior evidence,
  not physical gains or recommendations.
- The PID error passed to the backend is explicitly `target - measured`.
- Non-Stop commands retain the existing final-state-per-tick semantics. RR-003
  requires OFF when the target is absent at control evaluation; unlike the
  explicit manual-Stop rule, it does not define an intermediate command-batch
  barrier for target removal followed by restoration.

## Steps

1. Add the exact manifest pin and use ESP-IDF Component Manager to resolve the
   real component and transitive hashes into `dependencies.lock`.
2. Add `IChamberController`, refactor application demand/reset/failure ordering,
   and update every construction site explicitly.
3. Add the deterministic production adapter, host-testable PID policy/backend
   seam, and target-only float RAII backend using the exact suffixed APIs.
4. Add a focused M8 host group for form-specific configuration, initialization,
   error direction, normalization/rejection, reset/failure, cleanup/allocation,
   construction order, final-state target commands, and application safety.
5. Extend CMake and architecture guardrails for exact dependency/hash,
   form-specific upstream semantics, clean partial/corrupt-input failure,
   confinement, target-only APIs, unchanged simulated composition, one
   ControlTask, construction/safety/write ordering, and forbidden PID effects.
6. Update ARCHITECTURE, SAFETY, DECISIONS, HARDWARE, ROADMAP, and TRACEABILITY
   with exact positional/incremental behavior, derivative activation concerns,
   lifecycle ownership, inactive scope, and autotuning/activation gates.
7. Run focused host, guardrail, full host/sanitizer, full ESP-IDF, tracked-only
   snapshot, size, strict-C++20, production-composition, source-side-effect, and
   whitespace/status audits.

## Validation commands

```sh
cmake -S tests -B build-m8-host -G Ninja
cmake --build build-m8-host --target smoker_m8_tests
ctest --test-dir build-m8-host -R smoker_v0.m8_pid --output-on-failure
python3 tools/check_architecture.py
python3 tools/check_traceability.py
./tools/verify.sh --host-only
./tools/verify.sh --idf-only
git diff --check
git status --short --untracked-files=all
```

The final audit will additionally run architecture checks from a temporary
tracked-files-only snapshot without `managed_components`, inspect the effective
ESP-IDF version/C++ compile commands and firmware size, and search production
composition/PID paths for activation, SSR writes, tasks, delays, waits, I/O,
logging, and project-owned steady-state allocation.

## Risks / unresolved items

- Upstream has no `dt` input. Gains implicitly depend on call cadence, so a
  measured and validated real control period remains an activation gate.
- Both reviewed forms differentiate target-minus-measured error without a
  derivative filter or derivative-on-measurement option, so setpoint kick and
  any mitigation remain real-plant activation/tuning gates.
- Sensor timing/accuracy, SSR/heater electrical behavior, smoker thermal-plant
  response, independent cutoff, PID form/gains/limits, window timing, and
  autotuning policy remain hardware-pending M6B/M7/M8 work.
- Host allocation interception and source inspection cover project-owned and
  reviewed valid upstream paths only; they do not prove all target runtime or
  allocator behavior.
- The component's mandatory IQmath transitive dependency will be locked even
  though this slice deliberately calls only the float API.

## Implementation outcome

- Resolved `espressif/pid_ctrl` 0.3.1 to component hash
  `974be0666bb4d95f49677327dd8305781d04d8bae284fdde2fbadf06ca9d4979`
  and its mandatory `espressif/iqmath` 1.11.0~1 dependency to
  `39448db759b410373e543798167ca4670bbff3019cb290a2fe8e627221e71b9d`.
- Added the explicit application controller port, retained deterministic 100/0
  production composition, and compiled the inactive target-only float PID
  backend without composing it or adding SSR/GPIO behavior.
- Replaced the misleading common integral-limit fields with optional,
  positional-only accumulated-error bounds. Positional configuration requires
  finite ordered bounds containing zero; incremental configuration requires
  absence and the target adapter maps the upstream ignored fields to `0/0`.
- Confirmed and guarded exact 0.3.1 semantics: positional accumulates/clamps raw
  per-call error before Ki, incremental ignores those fields and retains/clamps
  output, both differentiate error, and the API has no `dt`, derivative filter,
  derivative-on-measurement, autotuning, or plant-identification surface.
- Controller initialization, compute, non-finite math/output, range, and reset
  failures now fail closed. A reset failure masked by a more authoritative
  safety fault remains pending, prevents further requests, and latches
  `ControlLoopFailure` once the earlier fault is resolved; clearing never
  resumes the stopped session.
- Construction now issues the observable OFF write before the first controller
  reset callback, while documentation keeps safe real-driver initialization as
  a separate future responsibility. Request and reset share the critical-path
  no-I/O/wait/block/task/steady-allocation contract. The wrapper no longer
  performs an ignored destructor reset; target-owned handle release remains
  RAII.
- Existing final-state command semantics were retained: target removal followed
  by restoration in one tick does not create a new barrier because RR-003
  applies at control evaluation, while SR-003/D031 explicitly make accepted
  manual Stop the OFF-cycle barrier.
- Focused M8 tests, architecture/traceability guardrails, and documentation were
  added. The proposed-files-only snapshot passes with `managed_components/`
  absent. Partial and semantically corrupted managed PID fixtures fail clearly
  without tracebacks. Production/PID source audits find no PID composition,
  SSR/GPIO output, PID task, delay, wait, I/O, logging, lock, or steady-path
  allocation.
- `./tools/verify.sh --host-only` passes 12/12 normal and 12/12 ASan/UBSan tests.
  `./tools/verify.sh --idf-only` passes under ESP-IDF 6.0.2 with strict C++20
  across 30 project sources. The unsigned image is 1,376,256 / 3,145,728 bytes
  (43.8% used, 1,769,472 bytes free); it was built only, never flashed or run.
- HEAD remains the required starting commit. All changes remain uncommitted and
  unpushed; no hardware, board, GPIO, thermal, electrical, or runtime action was
  taken or claimed.
