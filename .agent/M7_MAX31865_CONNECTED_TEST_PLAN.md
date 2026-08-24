# M7 MAX31865 software-checkpoint plan

Status: **Software checkpoint and connected functional bring-up complete. The
first separately authorized run failed at floating-MISO discrimination and is
preserved below; later corrected runs established pull-independent SPI,
configuration/raw/fault reporting, and exact software/driver shutdown. The
ordinary production activation is build-validated separately. Calibration,
controlled faults, sustained runtime, and remaining M6B/M7 facts are open.**

## Original software-checkpoint goal

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

## Original software-checkpoint non-goals

- no flash, erase-flash, serial monitor, signing, release, provisioning, NVS,
  or physical GPIO action in this software checkpoint;
- no claim that the module is powered, responds, is correctly wired, or is
  electrically safe;
- no temperature calculation while fitted Rref is unconfirmed;
- no MAX31865 activation as the authoritative production chamber source until
  the remaining M6B/M7 facts and connected behavior are validated;
- no PID, ADS1115, recipe, heater, SSR, fan, or smoke-generator work.

## Pre-connected-run repository observations

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
  readback. Exact readback proves the intended transaction result only when it
  is observed. The 2026-08-24 connected run did not reach terminal `0x11`; its
  earlier fallback write/readback failed, so physical quiescence remains
  unverified.
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

## Connected-procedure contract

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

The maintainer separately authorized the first flash/monitor/restoration cycle
on 2026-08-24 and reported the heater, SSR, and mains physically disconnected.
That authorization covered no wiring manipulation or fault/thermal testing.

## First connected execution evidence — 2026-08-24

### Preconditions and software/build evidence

- Git `HEAD` was
  `ab29c2171bcfceb134eed359c13b001a3a49f841`, its parent was
  `3a519dfc6f6276e81d1ab2fe54f6f75f292a10be`, `main` was six commits
  ahead of `origin/main`, and the tracked worktree/index were clean.
- Exactly one expected native-USB `cu.usbmodem` endpoint was present. Its
  unique local identifier is intentionally not versioned here.
- ESP-IDF reported exactly `v6.0.2`. Fresh ordinary and diagnostic builds used
  separate ignored directories; the latter loaded exactly
  `sdkconfig.defaults;diagnostics/max31865/sdkconfig.defaults`.
- Architecture/traceability guardrails, all 12 host groups in ordinary and
  sanitizer builds, effective configuration, strict C++20, both compile
  databases, generated partitions, size limits, and ELF composition isolation
  passed. The ordinary ELF contains `SmokerApplication::tick()`,
  `start_simulation_runtime()`, `DeterministicChamberController`, and
  `SimulatedHeaterOutput`; the diagnostic ELF contains the diagnostic
  entrypoint and none of those runtime/controller/heater symbols.
- The ordinary unsigned application was 1,376,256 bytes with SHA-256
  `9d30feed8ddf39ba64fd80da56e4d2d6b51745f01384b1646f2a6fe7d33d7d00`.
  Its ELF SHA-256 was
  `9a17b10d27251360a1057280e091c619d6bd6ccf6dc4d6d4a084426c86e5d0b2`.
  The independently named 1,380,352-byte signed image was
  `smoker_controller-ordinary-head-ab29c217-signed.bin`, SHA-256
  `d22dd231d5b91ff2be564ce48647353b25f2ee4afedc1df523e90f8e38889cda`.
- The ordinary complete set also contained a 21,168-byte generated bootloader,
  SHA-256
  `8e4d553c927a73e08b7f2ac5840686045f9e77e0feea2a0b24ca38f87b310048`,
  a 3,072-byte partition table, SHA-256
  `fc2d47b7e29632ea559f93af4694854ed158e2fa548dcb09162365f950708432`,
  and 8,192-byte initial OTA metadata, SHA-256
  `7d2c7ac4888bfd75cd5f56e8d61f69595121183afc81556c876732fd3782c62f`.
- The diagnostic unsigned application was 262,144 bytes with SHA-256
  `c7a6e8a6d3a7c6579b40f7c9b4753f85ecfe79920383615cedd638c64e646120`.
  Its ELF SHA-256 was
  `bc97fd672bea3dec2599c4a692e5ceaad1304039ea331cd224e70e4b20cc4962`.
  The independently named 266,240-byte signed image was
  `smoker_controller-max31865-diagnostic-head-ab29c217-signed.bin`,
  SHA-256
  `9b224a18e51cd10a82a0b0aa30e39d4a2e446fba99953194a2556b343fa7900b`.
- The diagnostic complete set contained a 21,168-byte generated bootloader,
  SHA-256
  `6183b634441a32e91d827a350a05dbdf179b4a348bf8def20e173e61ca4f912e`.
  Its 3,072-byte partition table and 8,192-byte initial OTA metadata matched
  the ordinary hashes recorded above.
- Both complete signed sets independently passed
  `tools/flash_signed_firmware.py --check-only` with explicit build and signed
  image paths before the first serial write. Private-key contents were not
  printed or added to the repository.

### Direct connected-target observation

The signed helper wrote and hash-verified only the generated bootloader at
`0x0`, partition table at `0x8000`, initial OTA metadata at `0xf000`, and signed
diagnostic application at `0x20000`. It did not write NVS or history and did not
perform a whole-chip erase. One bounded monitor-attached reset then captured
the complete diagnostic path:

| Evidence category | Direct observation | Result |
|---|---|---|
| Boot/image identity | ESP32-S3 revision 0.2 booted `smoker_controller` `0.15.0`, ESP-IDF `v6.0.2`, from `ota_0`; diagnostic ELF SHA-256 began `bc97fd672` and matched the fresh diagnostic ELF | Diagnostic image identified |
| Diagnostic composition | Log stated application/control runtime and heater output were absent and that no temperature would be calculated | Expected isolation observed |
| Pin/config intent | Log stated SPI2, GPIO12 SCK, GPIO11 MOSI, GPIO13 MISO, GPIO10 CS, 100 kHz, then began bounded mode-1 software SPI | Expected configuration observed |
| Pull discrimination | Configuration read with pull-up was `0xff`; with pull-down it was `0x00` | **FAIL** — SDO/GPIO13 followed the internal pulls and was classified as floating/not consistently driven |
| Complementary software-SPI patterns | Initial quiescence plus patterns A/B and active sampling were not attempted after the pull-discrimination failure | Skipped by fail-fast design |
| Software-SPI cleanup | Destructor fallback restored its frame boundary, read `0x00`, requested first-stage quiescence `0x10`, read back `0x00`, and reported exact-readback mismatch/fallback failure | **FAIL** — physical shutdown not verified |
| Exact software terminal `0x11` | Not attempted because first-stage `0x10` readback failed | Not observed |
| Pinned-driver initialization/readback | Driver stage was not entered | Not observed |
| Raw RTD observations | No raw codes and no `RRTD/RREF` ratios were produced | Not observed; no temperature conversion is permitted |
| RTD fault observations | No raw fault bit or fault-status register was read | Not observed |
| SPI/transaction classification | Software GPIO transactions returned far enough to produce pull-dependent bytes, but the response was rejected. No pinned-driver SPI transaction was attempted. Cleanup had an exact-readback mismatch. | Register response failed; no driver transaction evidence |
| Driver shutdown and terminal `0x11` | Driver stage was not entered, so driver shutdown was not attempted and `0x11` was not read back | Not observed |
| Final diagnostic result | `Connected sensor diagnostic failed; heater remains absent/OFF` | Expected fail-closed result |

This directly proves only the pull-dependent input observation and the failed
cleanup readback on this setup. It does not distinguish an unpowered module,
open/incorrect SDO path, module/connector issue, wrong breakout behavior, or
another physical cause. Because no valid device response was established, it
also provides no continuity, conversion, Rref, RTD-standard, sensor-health,
accuracy, calibration, settling, noise, or physical-quiescence evidence.

### Mandatory ordinary-firmware restoration

Without another diagnostic attempt or any wiring change, the already validated
ordinary signed set was installed with the same helper. Every written range was
hash-verified. A bounded ordinary-image monitor observed app `0.15.0`, ESP-IDF
`v6.0.2`, ELF SHA-256 prefix `9a17b10d2`, `ControlTask` on core 1, simulated
chamber `25.0 C`, no active chamber target, and simulated heater `0.0%`. No
MAX31865 diagnostic log appeared. The board was therefore left on the ordinary
signed firmware with the simulated production composition restored.

The repository-root ignored `smoker_controller.bin` retained SHA-256
`9f945da577d218482bc9fb02ceac0f778fa5f433c9671140650ff52fe1dc91de`.
No source code, NVS, history, provisioning state, heater/SSR/mains connection,
release, tag, push, or production adapter activation was part of this run.

## Later corrected connected evidence — 2026-08-24

The preserved successful log is
`build-m7-connected-diagnostic-20260824/log/idf_py_stdout_output_47381`,
SHA-256
`5979dcb174bdc49661c77cb26a67a2ca7db16f3bc42c7daf45a5dbd516d3916d`.
It records:

- SPI2, GPIO12 SCK, GPIO11 MOSI, GPIO13 MISO, GPIO10 CS, and 100 kHz;
- pull-independent initial configuration `0x11` with both internal pulls;
- software-SPI exact initial `0x11`, quiescent `0x00`, idle-bias
  three-wire/50 Hz `0x91`, active `0xD1`, and terminal `0x11`;
- driver exact initial `0x11`, active `0xD1`, and terminal `0x11`;
- ten raw samples consisting of 8548/8549, ratios 0.260864/0.260895, and
  zero fault status;
- `transaction_errors=0`, `sensor_fault_samples=0`, successful transaction
  path, and successful checked shutdown.

Independent conversion corroboration with the deliberately provisional
430.0-ohm Rref and ITS-90 gives:

| Raw | `RRTD/RREF` | Derived RRTD | Derived temperature |
|---:|---:|---:|---:|
| 8548 | 0.260864258 | 112.171631 ohm | 31.287679 C |
| 8549 | 0.260894775 | 112.184753 ohm | 31.321568 C |

This corroborates the software conversion choice only. It is not a physical
measurement of Rref, calibration, probe accuracy, reference-temperature
comparison, or exact RTD-standard identification.

A second preserved log,
`build-m7-connected-diagnostic-20260824/log/idf_py_stdout_output_46058`, has
SHA-256
`f448d05c0dfc35be8dff0aa7e392ec8449b17a369f1ad14e3070f19649a727c3`.
It records the same successful SPI/configuration/shutdown sequence, but ten
raw-zero samples reported fault status `0x40`, with
`transaction_errors=0` and `sensor_fault_samples=10`. Because no controlled or
identified physical stimulus is recorded, this is evidence that fault
reporting was observed, not proof of an open/short scenario or recovery.

These later facts supersede the first run only for functional communication,
configuration, raw sampling, and checked transaction shutdown. The first
failure remains valid chronology and diagnostic fail-closed evidence.

## Physical prerequisites and unresolved items after functional bring-up

- exact breakout manufacturer, revision, front/back markings, and schematic;
- permitted supply input, logic levels, and common-ground arrangement;
- fitted reference-resistor value/tolerance and actual RTD terminal order;
- continuity from the soldered SPI2/GPIO12/11/13/10 assignment to the module;
- independent continuity/resistance measurement and shield termination;
- module input-filter/bias settling and safe power sequencing;
- the supplier-documented PT100 assembly facts still require physical-unit
  identity/inspection where noted in `docs/HARDWARE.md`;
- independent heater-power protection, which is outside this sensor checkpoint.

Functional connected success cannot close those physical facts. M6B and M7
remain incomplete until the remaining evidence classes are satisfied.
