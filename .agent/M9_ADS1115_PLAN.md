# Dual ADS1115 dependency preparation plan

## Goal

Select and import a mature ESP-IDF ADS1115 driver for the two user-selected
ADCs without rewriting the register protocol or claiming that their electrical
integration is complete.

## Scope

- research ESP-IDF built-ins, ESP Component Registry, and maintained upstream
  alternatives;
- exact-pin the selected component in the platform manifest and lockfile;
- cross-build the component for ESP32-S3 with ESP-IDF 6.0.2;
- document the dual-device constraints and the remaining M6B facts;
- retain simulated probe inputs until the physical role and frontend are
  documented.

## Non-goals

- no GPIO, I2C port, address-strap, channel, gain, sample-rate, or analog-range
  assignment;
- no assumption that either ADC is already the complete food-probe frontend;
- no real ADS1115 adapter or runtime activation;
- no new task and no change to heater-control behavior.

## Current repository observations

- M6B is in progress; the complete external-hardware record is open.
- M9 real food probes has not started and production uses simulated probes.
- ESP-IDF 6.0.2 supplies I2C master functionality but no ADS1115 device driver.
- The user selected two ADS1115 devices on 2026-08-18 but has not yet supplied
  their modules, address straps, analog frontend, signal roles, or wiring.

## Assumptions

- Both parts are ADS1115-compatible devices; exact module identity remains
  unconfirmed.
- They may share one I2C bus only after two distinct addresses are physically
  selected from `0x48..0x4b`; no address choice is assumed here.
- A successful cross-build proves software compatibility only, not electrical,
  conversion, accuracy, noise, or connected-device behavior.

## Steps

1. Review registry metadata, versioned source/API, license, release identity,
   multi-device example, and dependency closure.
2. Add exact `esp-idf-lib/ads111x ==1.1.14` to `smoker_platform`.
3. Regenerate and inspect `dependencies.lock` under ESP-IDF 6.0.2.
4. Record the selection, component hash, architectural boundary, and remaining
   hardware gate in the source-of-truth documentation.
5. Add reproducibility guardrails for the exact dependency and decision.
6. Run architecture/traceability checks and a fresh ESP32-S3 build.

## Validation commands

```sh
python3 tools/check_architecture.py
python3 tools/check_traceability.py
SMOKER_VERIFY_BUILD_DIR="$PWD/build-ads1115" tools/verify.sh --idf-only
git diff --check
```

## Risks / unresolved items

- exact module manufacturer, revision, schematic, supply, and logic levels;
- each ADDR strap and resulting unique I2C address;
- I2C port, SDA/SCL GPIOs, pull-up values/rail, bus length, and bus speed;
- actual purpose of each ADC and its channel map;
- input protection, bias/excitation, filtering, valid voltage range, gain, data
  rate, and conversion schedule;
- connected-device behavior, timing, accuracy, noise, open/short handling, and
  heater-interference tests.

## Outcome

- Selected and exact-pinned `esp-idf-lib/ads111x` 1.1.14, BSD-3, registry hash
  `fd18497adfb7210d750188986bc7cebc048db36abb64fdbe7216d4536083c4a2`.
- The versioned lockfile resolves `esp-idf-lib/i2cdev` 2.1.2 and
  `esp-idf-lib/esp_idf_lib_helpers` 1.4.0 with their reviewed hashes.
- ESP-IDF 6.0.2 selected the new `i2c_master` implementation and compiled
  `i2cdev.c` plus `ads111x.c` for ESP32-S3.
- Architecture, traceability, partition, effective-config, strict-C++20,
  firmware-size, and unsigned-flash guardrails passed through
  `tools/verify.sh --idf-only` using the fresh `build-ads1115` directory.
- No adapter, address, channel, GPIO, or connected-hardware behavior was added
  or claimed; the M6B/M9 physical gates above remain open.
