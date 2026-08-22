# M7 MAX31865 software-checkpoint plan

## Goal

Audit and finish a build-only, explicit-opt-in MAX31865 diagnostic for the
final soldered SPI assignment without executing any connected-board action.
Every communicating path must leave the converter normally off, unbiased, and
without a running command before releasing descriptor, bus, CS, or GPIO
ownership. The checkpoint prepares a later physical procedure; it does not
complete M6B or M7 and does not activate real chamber sensing in the ordinary
image.

## Scope

- keep the exact-pinned `esp-idf-lib/max31865` 1.0.8 dependency;
- record the maintainer-confirmed final production assignment in target code:
  SPI2, GPIO12 SCK, GPIO11 MOSI, GPIO13 MISO, and GPIO10 CS;
- use that one production assignment from the diagnostic rather than copying
  diagnostic-only pin values;
- provide one target-only Kconfig path whose default is OFF;
- prove at build/source level that the diagnostic composition excludes the
  normal application/control runtime and every heater output;
- log raw RTD code, the dimensionless `RRTD/RREF` ratio, configuration
  readback, and fault bits only;
- require checked exact converter quiescence on the normal success path and
  bounded best-effort RAII quiescence on every early return after SPI access;
- preserve descriptor-removal-before-bus-release ordering even when shutdown
  fails, and keep sensor fault samples distinct from transaction/shutdown
  failure;
- keep all changes uncommitted for review.

## Non-goals

- no flash, erase-flash, serial monitor, signing, release, provisioning, NVS,
  or physical GPIO action in this software checkpoint;
- no claim that the module is powered, responds, is correctly wired, or is
  electrically safe;
- no temperature calculation while fitted Rref is unconfirmed;
- no MAX31865 activation as the authoritative production chamber source until
  the remaining M6B/M7 facts and connected behavior are validated;
- no PID, ADS1115, recipe, heater, SSR, fan, or smoke-generator work.

## Current repository observations

- M6B and M7 remain incomplete; the ordinary image still composes
  `SimulatedChamberSensor`.
- The maintainer reported on 2026-08-22 that the SPI2/GPIO12/11/13/10 mapping
  is the final assignment already soldered on the product board. This records
  production wiring intent but is not a continuity, power, or transaction test.
- Breakout revision, supply/logic behavior, fitted Rref, connector/terminal
  order, RTD construction, and observed connected behavior remain unconfirmed.
- The inactive M7 adapter and exact-pinned target backend already have host and
  ESP-IDF API-cross-build evidence for freshness and fail-absent behavior.
- The initial diagnostic implementation left its software-SPI stage at `0xD1`
  and its driver stage in AUTO with VBIAS enabled, then released only software
  resource ownership. A later bus/device initialization failure could therefore
  release pins while the physical converter remained active.

## Source-audit findings

- Analog Devices specifies SPI modes 1 and 3 with CPHA=1. For mode 1, SCLK is
  idle low, data shifts/changes on the rising edge, and is latched/sampled on
  the falling edge. The previous software-SPI routine changed MOSI immediately
  before the falling edge and therefore did not provide the required setup.
- The datasheet requires at least 400 ns from CS low to SCLK and 100 ns from
  the final SCLK edge to CS high. The bounded software path uses 10 us margins.
- Only configuration bits D7, D6, D4, and D0 are persistent settings. D5,
  D3:D2, and D1 are command/self-clearing fields. Readback patterns therefore
  compare only mask `0xD1`, first leave automatic mode without changing its
  filter, and change filter only while conversions are normally off.
- Driver 1.0.8 `max31865_set_config()` reads the complete configuration byte,
  clears only D7/D6/D4/D0, and writes the result back. It can therefore preserve
  D5, D3:D2, or D1 if read while those command bits are set. Shutdown must use
  an exact raw configuration write rather than this setter. The driver's
  `max31865_detect_fault_auto()` is excluded because its loop is unbounded.
- Exact terminal `0x11` means VBIAS=0, AUTO=0, 1-shot=0, three-wire,
  fault-cycle=00, fault-clear=0, and 50 Hz. Cleanup first preserves the current
  filter while exiting AUTO, then selects 50 Hz only after normally-off
  readback. Exact readback proves the intended transaction result in software;
  it is not evidence that an unexecuted connected module accepted it.
- MAX31865 SDO is high impedance until read data is shifted. Both software and
  pinned-driver checks read configuration under MISO pull-up and pull-down
  before accepting complementary configuration write/readback. A floating
  MISO which follows the pulls is rejected.
- Driver 1.0.8 uses ESP-IDF mode 1 and accesses raw RTD/configuration/fault
  registers without Rref. Its temperature API does use Rref and is not called.
- The first automatic 50 Hz conversion has a 66 ms documented maximum; the
  diagnostic waits 100 ms before a finite ten-sample observation.

## Steps

1. Centralize the final SPI host/GPIO assignment in target-only platform code.
2. Correct and bound the software-SPI and pinned-driver register-response
   checks, including checked/idempotent exact quiescence, destructor fallbacks,
   failed-frame recovery, failure logging, descriptor removal, bus ordering,
   and pin release.
3. Compile the diagnostic source only when its Kconfig option is enabled and
   compile exactly one `app_main` composition branch.
4. Extend executable guardrails for default-OFF isolation, final pin ownership,
   absence of temperature/application/heater paths, bounded sampling, and
   separate configuration output.
5. Run fresh ordinary and diagnostic ESP-IDF 6.0.2 builds in separate output
   directories, then run the complete required verification set.

## Exact build-only commands

Run from the repository root. These checkpoint paths are separate from the
ordinary generated `sdkconfig` and from each other.

```sh
export IDF_TOOLS_PATH="$PWD/.tools/espressif"
. "$PWD/.tools/esp-idf-v6.0.2/export.sh"
idf.py --version

idf.py -C "$PWD" \
  -B "$PWD/build-m7-normal-software-checkpoint" \
  -D "SDKCONFIG=$PWD/build-m7-normal-software-checkpoint/sdkconfig" \
  build

idf.py -C "$PWD" \
  -B "$PWD/build-m7-diagnostic-software-checkpoint" \
  -D "SDKCONFIG=$PWD/build-m7-diagnostic-software-checkpoint/sdkconfig" \
  -D "SDKCONFIG_DEFAULTS=$PWD/sdkconfig.defaults;$PWD/diagnostics/max31865/sdkconfig.defaults" \
  build
```

The full validation commands are:

```sh
python3 tools/check_architecture.py
python3 tools/check_traceability.py
./tools/verify.sh --host-only
./tools/verify.sh --idf-only
python3 tools/check_target_compile_commands.py \
  build-m7-normal-software-checkpoint/compile_commands.json
python3 tools/check_target_compile_commands.py \
  build-m7-diagnostic-software-checkpoint/compile_commands.json
git diff --check
```

Inspect both ELF/map outputs with `nm`/`rg`: the ordinary image must retain
`SmokerApplication::tick()` and `start_simulation_runtime()` while excluding
the diagnostic entrypoint; the diagnostic image must contain the diagnostic
entrypoint while excluding `SmokerApplication::tick()`,
`start_simulation_runtime()`, `ControlTask`, and heater implementations. Record
both application sizes from the fresh build output.

No command in this plan authorizes `flash`, `erase-flash`, `monitor`, signing,
provisioning, or any other board-facing action.

## Pre-remediation software-checkpoint evidence (2026-08-22)

- ESP-IDF reported `v6.0.2` from the repository-pinned toolchain.
- The fresh ordinary build completed with
  `CONFIG_SMOKER_MAX31865_CONNECTED_DIAGNOSTIC` unset. Its compile database and
  link map contain no diagnostic source; its ELF contains
  `SmokerApplication::tick()` and `start_simulation_runtime()` and no
  `run_max31865_connected_diagnostic()` symbol.
- The fresh diagnostic build completed with the explicit overlay enabled. Its
  compile database and link map contain the diagnostic source; its ELF contains
  `run_max31865_connected_diagnostic()` and no `SmokerApplication::tick()` or
  `start_simulation_runtime()` symbol. This build predated checked converter
  quiescence and is not completion evidence for the remediated boundary.
- The ordinary binary is 1,376,256 / 3,145,728 bytes (43.8% used). The
  diagnostic binary is 262,144 / 3,145,728 bytes (8.3% used).
- `./tools/verify.sh --host-only` passed all 11 test groups in ordinary and
  sanitizer builds. `./tools/verify.sh --idf-only` passed the target build,
  strict-C++20 check, size guard, and unsigned-flash-target rejection.
- The strict-C++20 checker evaluated 28 project sources in the default build
  and 29 in the diagnostic build; the only conditional source is tied to the
  effective diagnostic Kconfig symbol.
- These are historical source, build, and link-isolation results only. They do
  not prove the remediated shutdown sequence, and no board-facing command was
  run or connected MAX31865 observation made.

## Quiescence-remediation evidence

Fresh validation on 2026-08-22 passed:

- architecture and traceability guardrails, `git diff --check`, and all 11 host
  groups in ordinary and sanitizer builds;
- `./tools/verify.sh --idf-only`, including ESP-IDF 6.0.2, effective config,
  unsigned-flash rejection, and the ordinary size guard;
- fresh separate `build-m7-quiescence-normal` and
  `build-m7-quiescence-diagnostic-final` builds, with the diagnostic overlay
  loaded from `sdkconfig.defaults;diagnostics/max31865/sdkconfig.defaults`;
- strict C++20 for 28 ordinary and 29 diagnostic project sources; only the
  diagnostic database contains `max31865_connected_diagnostic.cpp`;
- ordinary ELF contains `SmokerApplication::tick()` and
  `start_simulation_runtime()` and excludes the diagnostic entrypoint;
- diagnostic ELF contains `run_max31865_connected_diagnostic()` and excludes
  `SmokerApplication::tick()`, `start_simulation_runtime()`, `ControlTask`, and
  heater implementations;
- ordinary application size is 1,376,256 / 3,145,728 bytes (43.8%); diagnostic
  application size is 262,144 / 3,145,728 bytes (8.3%).

This evidence class remains source/build proof of the intended exact
write/readback and cleanup ordering. Physical quiescence stays unverified until
a separately authorized connected procedure actually observes successful
shutdown readback.

## Proposed later connected procedure

Only after separate authorization and the physical prerequisites below are
recorded, a reviewer may build the same opt-in image, install it through the
project's approved signed full-image path, observe one bounded run, and restore
the ordinary signed image. Evidence must keep these independent:

1. pull discrimination plus complementary configuration readback supports an
   SPI register-response observation;
2. raw RTD code, `RRTD/RREF`, RTD fault flag, and decoded fault register are
   sensor observations, not temperature or accuracy evidence;
3. connected/open/short/reference-temperature/sustained/heater-interference
   scenarios are separate physical tests and may fail independently.

## Physical prerequisites and unresolved items

- exact breakout manufacturer, revision, front/back markings, and schematic;
- permitted supply input, logic levels, and common-ground arrangement;
- fitted reference-resistor value/tolerance and actual RTD terminal order;
- continuity from the soldered SPI2/GPIO12/11/13/10 assignment to the module;
- PT100 lead identification, accuracy class, range, cable, and connector;
- module input-filter/bias settling and safe power sequencing;
- independent heater-power protection, which is outside this sensor checkpoint.

Source/build success cannot close any of those physical facts. M6B and M7 must
remain incomplete until the corresponding connected evidence exists.
