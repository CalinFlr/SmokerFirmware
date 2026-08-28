# ADS1115 A3 connected-evidence documentation remediation plan

## Goal

Audit the uncommitted ADS1115/NTC documentation against the maintainer-provided,
hash-verified separate-session evidence and record the A3 results without
overstating analog, calibration, production-integration, or shutdown evidence.

## Scope

- preserve and correct the existing ADS1115 documentation changes in
  `README.md`, `docs/DECISIONS.md`, `docs/HARDWARE.md`, `docs/ROADMAP.md`, and
  `docs/TRACEABILITY.md`;
- add the connected-evidence boundary to `docs/ARCHITECTURE.md`;
- record the failed initial A3 run, corrected room-condition run, uncontrolled
  soldering-tool heating run, nominal-resistance limitations, terminal-idle
  remediation, last-known board image, and remaining activation gates;
- keep every change unstaged and uncommitted.

## Non-goals

- no board, serial, reset, flash, monitor, signing, provisioning, or other
  hardware access;
- no source code, test, build configuration, dependency, generated-file, or
  production-composition change;
- no A0-A2 analog-validation claim and no inferred NTC R25, Beta, curve,
  temperature, calibration, or accuracy;
- no M6B/M9 completion or `Ads1115TargetBackend` activation claim.

## Current repository observations

- Initial HEAD is exactly
  `9af4200c494aee1409b688219d58ea8423603548` on `main`, ahead eight and behind
  zero from `origin/main`.
- The initial index is empty, there are no untracked files, and exactly five
  unstaged paths exist: `README.md`, `docs/DECISIONS.md`, `docs/HARDWARE.md`,
  `docs/ROADMAP.md`, and `docs/TRACEABILITY.md`.
- The current milestone remains M6B/M9 incomplete. The dual-ADS1115 sequencer
  and pinned backend are inactive; ordinary production composition constructs
  `SimulatedFoodProbeSource`.
- One ADS1115 is installed at 3.3 V on GPIO17/GPIO18, 100 kHz, ADDR=GND
  (`0x48`), with ALERT/RDY disconnected. The second module is deferred.
- Four divider/filter networks were maintainer-reported assembled, but only A3
  was exercised with an NTC.

## Assumptions

- The maintainer-provided, hash-verified local session transcript is the
  authoritative record for the connected runs. Its SHA-256 independently matches
  `dcfa52e2a352519735c28716afb0eb2c34ed53b1fdd0239664a39b06357c695f`.
- The surviving temporary source is supporting procedural evidence. Its
  SHA-256 independently matches
  `4cc3eaf477a160aa5e3ebef44a760ef798cbe08fb78e8eee2bd67b5c424a9a68`.
- Nominal resistance is response evidence only because neither the actual 3V3
  rail nor the individual 100 kOhm reference resistor was measured.
- The last known board image is the temporary signed A3 diagnostic. The board
  is unavailable for this documentation-only goal and will not be accessed.

## Steps

1. Extract only the A3-specific transcript records and inspect the temporary
   source for bus configuration, conversion word, resistance formula, and
   shutdown ordering.
2. Reconcile the failed initial, corrected room-condition, and uncontrolled
   heating evidence across all current-status documentation.
3. Preserve the separate 2026-08-25 floating-A0 digital evidence while clearly
   distinguishing it from the later A3 analog-response evidence and its weaker
   terminal-idle result.
4. Audit repository-wide claims for production simulation, M6B/M9 incomplete
   status, A0-A2 untested status, unknown curve/calibration, and the last-known
   diagnostic board image.
5. Run the required whitespace, architecture, traceability, changed-path,
   empty-index, and no-commit checks.

## Validation commands

```sh
git diff --check
python3 tools/check_architecture.py
python3 tools/check_traceability.py
git diff --name-only
git diff --cached --name-only
git status --short --untracked-files=all
git rev-parse HEAD
```

## Risks / unresolved items

- module manufacturer/revision and external pull-up rail/value;
- actual 3V3 rail and individual reference-resistor measurements;
- NTC R25, Beta/curve or fitted Steinhart-Hart coefficients;
- stable co-located calibration points against a separately validated
  reference; the PT100/MAX31865 is only a possible future reference;
- A0-A2, known-resistance/known-voltage, disconnect/open/short, settling,
  noise, accuracy, repeatability, sustained, and heater-interference tests;
- project backend/sequencer integration, ControlTask timing, production
  activation, and terminal-idle verification for the temporary A3 procedure.

## Outcome

- Independently verified both supplied evidence hashes and inspected only the
  A3-specific transcript records and relevant temporary source.
- Preserved the 2026-08-25 floating-A0 digital result separately from the
  2026-08-26 failed/corrected/heated A3 evidence.
- Recorded the direct-`i2c_master`/internal-pull-up boundary, nominal-resistance
  limitations, non-monotonic heating detail, terminal-idle remediation, last
  known diagnostic board image, unchanged simulated production composition,
  and all remaining M6B/M9 gates.
- `git diff --check` passed.
- `python3 tools/check_architecture.py` passed.
- `python3 tools/check_traceability.py` passed with 66 rules and 46 referenced
  host tests.
- Repository-wide current-status searches found no contradictory floating-A0,
  production-composition, incomplete-milestone, A0-A2, calibration/curve, or
  last-known-board-image claim.
- Final inventory is this plan plus modifications to `README.md`,
  `docs/ARCHITECTURE.md`, `docs/DECISIONS.md`, `docs/HARDWARE.md`,
  `docs/ROADMAP.md`, and `docs/TRACEABILITY.md`. HEAD remains unchanged, the
  index remains empty, and no file was committed or pushed.
