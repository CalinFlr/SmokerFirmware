# Hardware and Component Inventory

Status: **M6A complete; M6B external hardware incomplete**

Last evidence update: **2026-08-26**

This file is the canonical inventory for every physical component used by the
product. It is written so that a human or an AI agent can distinguish an
observed fact from a build choice, a documented characteristic, or an
assumption. Do not copy a marketing pinout or a configuration value into an
electrical claim without preserving its evidence class.

## Evidence contract

Every material hardware statement must use one of these exact classifications:

- **CONFIRMED FROM HARDWARE** — read from the connected device or exercised on
  the named physical unit. Record the tool, date, port/setup, and relevant
  output. A successful query proves only what that query reports.
- **CONFIRMED FROM CONFIG** — selected by the effective project build
  configuration or present in production source. It describes firmware intent,
  not physical presence or electrical validation.
- **CONFIRMED FROM DOCUMENTATION** — supported by a named datasheet, schematic,
  procurement record, product photograph, or user-confirmed marking. Record
  which kind of source was used; seller artwork is not equivalent to a
  manufacturer schematic.
- **UNCONFIRMED** — unknown, inferred, inconsistent, not yet measured, or not
  supported strongly enough for implementation.

When evidence conflicts, keep the conflict visible. Hardware readback may
resolve storage identity, while an official component datasheet remains the
authority for absolute electrical ratings and restrictions. A fact confirmed
on one unit is not automatically a production-lot guarantee.

### Required record for each component

Add each selected component to the register below and give it a dedicated
section before implementing its adapter or assigning its pins. Record:

1. project component ID and product role;
2. manufacturer, exact part number, module/board revision, and markings;
3. procurement source and whether the examined unit is the final product part;
4. supply and logic voltage, current/power limits, and interface requirements;
5. connector/header pinout, assigned MCU GPIO, shared pins, and boot-state
   behavior;
6. authoritative datasheets/schematics and any conflicting seller information;
7. build configuration and driver/dependency choices;
8. physical validation performed, exact setup/date, and relevant output;
9. safety constraints, failure behavior, and every unresolved fact.

Do not change an existing confirmed record silently when a part or revision
changes. Add the new revision/unit evidence and state which hardware the
firmware currently targets.

Hardware identification is split into M6A for the available controller board
and M6B for external sensing, output, power, and independent-safety hardware.
Availability alone is not validation: exact facts and evidence must be recorded
before either gate is marked complete.

## Known baseline

- Target MCU family: ESP32-S3.
- Development framework: native ESP-IDF.
- Final controller board: SuooTci ESP32-S3, seller code `KFB003` / eMAG
  product `D1T7M22BM`, using the user-reported N16R8 module variant.
- MAX31865 with a three-wire PT100 is selected by the user as the first chamber
  frontend to connect. The supplier page for probe SKU `88056` documents a
  PT100, assembled range -50..+200 C, tolerance `F0.15`, SUS304 4 mm x 100 mm
  sheath, 1 m shielded FEP cable, 1/4 NPT fitting, and approximately 100 ohm at
  0 C / 138.5 ohm at 100 C. Those are supplier-documentation facts, not
  inspection or measurement of the connected unit; the exact MAX31865
  module/revision and fitted reference resistor remain unknown.
- The maintainer reports the final MAX31865 wiring is already soldered to SPI2
  with GPIO12 SCK, GPIO11 MOSI, GPIO13 MISO, and GPIO10 CS. This is a confirmed
  production assignment, not evidence of continuity, power, or SPI response.
- The maintainer reports ESP32 3V3 is connected to module VIN; the module is
  configured for three-wire use, including its separate 2/3-wire jumper; and
  the probe leads are red to F+, red to RTD+, blue to RTD-, with RTD- jumpered
  to F-. These reports do not establish continuity, resistance, module logic
  behavior, or shield termination.
- The first connected diagnostic on 2026-08-24 observed software-SPI MISO
  following its configured pulls (`0xff` up and `0x00` down) and failed before
  complementary configuration or driver transactions. Fallback shutdown
  requested `0x10` but read `0x00`; physical converter shutdown was therefore
  not verified. The ordinary signed simulated firmware was restored.
- A later corrected connected setup produced pull-independent initial
  configuration `0x11`, exact software-SPI readbacks `0x00`, `0x91`, and
  active `0xD1`, exact software terminal `0x11`, and the same driver initial,
  active, and terminal bytes. Ten driver samples were raw 8548/8549 with
  `RRTD/RREF` 0.260864/0.260895, fault zero, no transaction errors, and no
  sensor-fault samples. A separate run observed ten raw-zero samples reporting
  fault `0x40`; that is observed fault reporting, not controlled open/short
  injection.
- A 2026-08-25 signed serial ordinary-runtime activation observed chamber
  25.7 C at cycles 1 and 60 and 25.8 C at cycle 180 over approximately 179
  seconds, while IDLE with no target and simulated heater 0.0%. This completes
  M7's defined functional activation, not longer-duration, calibration,
  controlled-fault, response/noise, heater-interference, or physical-regulation
  qualification.
- Two ADS1115 converters are selected by the user for the external analog/probe
  path. One module is currently installed at 3.3 V with GPIO17 SDA, GPIO18 SCL,
  ADDR to GND (`0x48`), and ALERT/RDY unused. Connected I2C/register/single-shot
  conversion checks passed on 2026-08-25. The maintainer later reported four
  divider/filter networks assembled for four user-identified `NTC100` food
  probes. Only A3 was exercised with an NTC: a failed near-ground run was
  preserved, then corrected room-condition and uncontrolled-heating response
  runs passed on 2026-08-26. A0-A2 remain analog-untested. The exact probe R25,
  Beta/curve, temperature range, connector, calibration, and accuracy remain
  open. The second ADS1115 is deferred and has no address or wiring assignment.
  The SSR interface is not yet identified.
- The project will eventually control an electric smoker heater.
- Real chamber sensing is active with the evidence limits above. Food-probe
  integration, SSR wiring, independent thermal/electrical protection, current
  sensing, fan control, and smoke-source integration remain separate hardware
  milestones.

## Component register

| ID | Product role | Selected component | Current classification | Integration status |
|---|---|---|---|---|
| `CTRL-001` | Main controller | SuooTci `KFB003` / eMAG `D1T7M22BM`; N16R8 variant reported | Carrier identity **CONFIRMED FROM DOCUMENTATION**; SoC/storage/USB **CONFIRMED FROM HARDWARE** | M6A complete; ordinary runtime uses real chamber input with simulated food/heater I/O |
| `CHAMBER-001` | Authoritative chamber sensor/frontend | MAX31865 with supplier-documented PT100 probe SKU `88056`, three-wire; final SPI2/GPIO12/11/13/10 wiring assigned; exact physical module and fitted Rref pending | Probe characteristics and assembly **CONFIRMED FROM DOCUMENTATION**; SPI/configuration/raw/shutdown and short ordinary-runtime behavior **CONFIRMED FROM HARDWARE**; continuity/module identity/Rref/electrical facts **UNCONFIRMED** | M7 software integration and ordinary functional activation complete; calibration, longer-duration behavior, controlled faults/recovery, and remaining M6B/pre-heater qualification pending |
| `PROBES-001` | Food-probe analog acquisition | Two ADS1115 converters selected; one installed at 3.3 V on GPIO17 SDA/GPIO18 SCL with ADDR=GND (`0x48`); four `NTC100` divider/filter networks reported assembled; second module deferred | Selection/wiring/topology **CONFIRMED FROM DOCUMENTATION** — maintainer report; first-module digital response and connected A3 path response **CONFIRMED FROM HARDWARE**; A0-A2 analog behavior, calibration/accuracy, and second module **UNCONFIRMED** | Software adapter implemented/inactive; only A3 analog-response exercised, production still simulated and physical probe qualification pending at M6B/M9 |
| `HEATER-001` | SSR/heater power interface | Not selected | **UNCONFIRMED** | Blocked at M6B; no GPIO assigned |
| `SAFETY-001` | Independent thermal/electrical cutoff | Not designed | **UNCONFIRMED** | Required at M6B |
| `POWER-001` | Product power supply/rails | Not selected | **UNCONFIRMED** | Blocked at M6B |
| `CURRENT-001` | Optional current sensing | Not selected | **UNCONFIRMED** | Deferred; no capability assumed |
| `FAN-001` | Optional fan control | Not selected or designed | **UNCONFIRMED** | Deferred; must not be implemented early |
| `SMOKE-001` | Optional smoke-source control | Not selected or designed | **UNCONFIRMED** | Deferred; must not be implemented early |

## Rules for implementation

Until each board/component fact is confirmed:

- do not invent GPIO assignments;
- do not invent flash size;
- do not invent PSRAM size;
- do not invent display controller or pinout;
- do not invent chamber-sensor electrical characteristics;
- do not assume food-probe protocol/electrical frontend;
- do not assume a current-sensing device exists;
- do not implement fan control;
- do not implement smoke-generator control.

Production must use simulated adapters until the corresponding activation has
documented facts, an explicit decision, and proportionate evidence. MAX31865
is the first activated exception: its functional connected evidence supports
ordinary chamber acquisition, while provisional Rref/standard choices and all
remaining physical limits stay visibly classified. M6A may exercise only the
controller board and its integrated capabilities; this does not validate
external control hardware.

## `CHAMBER-001` — MAX31865 chamber-frontend dossier

### Selection and software boundary

The user selected MAX31865 on 2026-08-18 as the first external device to
connect and as the intended authoritative chamber frontend, then identified
the RTD as PT100 with three leads. This fixes the future driver choices to
`rtd_nominal = 100.0F` and `MAX31865_3WIRE`. The supplier page for probe SKU
`88056` documents the assembly as PT100, -50..+200 C, tolerance `F0.15`, SUS304
4 mm x 100 mm sheath, 1 m shielded FEP cable, 1/4 NPT fitting, and approximately
100 ohm at 0 C / 138.5 ohm at 100 C. It does not identify the purchased
breakout-board manufacturer, revision, fitted reference resistor, or exact RTD
standard, and the connected probe has not been inspected or measured against
those supplier facts.

ESP-IDF 6.0.2 contains the SPI master driver but no built-in MAX31865 device
driver. The project therefore uses the next approved dependency source, ESP
Component Registry:

| Item | Finding | Classification |
|---|---|---|
| Driver | `esp-idf-lib/max31865` 1.0.8, BSD-3 | **CONFIRMED FROM CONFIG** — exact manifest pin |
| Registry identity | component hash `c7a027843a3f9cf4b06e7e216b25b2089115568f288c8682defd84c018a5b80f` | **CONFIRMED FROM CONFIG** — `dependencies.lock` |
| Upstream source | release commit `79566bd59420b03ab999c124f012a93a63f3a7db`; supports `esp32s3`; release includes the ESP-IDF 6 driver split | **CONFIRMED FROM DOCUMENTATION** — registry metadata and upstream release history |
| Driver capabilities | SPI descriptor/configuration, PT100/PT1000 nominal values, 2/3/4-wire configuration, 50/60 Hz filter selection, raw/temperature reads, and MAX31865 fault access | **CONFIRMED FROM DOCUMENTATION** — versioned public header/source |
| Conversion freshness | RTD MSB/LSB POR is `0x00`; maximum first conversion after enabling automatic conversion is 55 ms at 60 Hz and 66 ms at 50 Hz | **CONFIRMED FROM DOCUMENTATION** — Analog Devices MAX31865 datasheet |
| Raw-zero driver behavior | 1.0.8 shifts raw zero to zero resistance and its below-zero polynomial returns approximately -242.02 C with `ESP_OK` when the raw fault bit is clear | **CONFIRMED FROM DOCUMENTATION** — versioned driver source |
| Production SPI assignment | SPI2; GPIO12 SCK, GPIO11 MOSI, GPIO13 MISO, GPIO10 CS, already soldered on the product board | **CONFIRMED FROM DOCUMENTATION** — maintainer report on 2026-08-22; not continuity or transaction evidence |
| Current firmware use | centralized ordinary runtime owns SPI2 bus, checked GPIO13 MISO pull-up, exact-config backend, real monotonic clock, and `Max31865ChamberSensor`; the explicit inclusive -50..+200 C validity policy comes from the supplier's assembled probe range; food probes/heater remain simulated and control deterministic; opt-in diagnostic stays default-OFF and isolated | **CONFIRMED FROM CONFIG** for firmware behavior; range **CONFIRMED FROM DOCUMENTATION** — supplier listing; not calibration |
| First connected diagnostic | exact opt-in image booted on 2026-08-24; software-SPI configuration readback followed the MISO pulls (`0xff` up, `0x00` down), so complementary patterns, the driver stage, and sampling were not entered; fallback shutdown requested `0x10` and observed `0x00`; ordinary signed firmware was then restored and verified | **CONFIRMED FROM HARDWARE** for this failed observation only; converter communication and physical quiescence remain **UNCONFIRMED** |
| Corrected connected diagnostic | pull-independent `0x11`; exact software `0x00`/`0x91`/`0xD1` and terminal `0x11`; driver initial `0x11`, active `0xD1`, ten raw 8548/8549 samples with zero fault/transaction/sensor-fault counts, and terminal `0x11` | **CONFIRMED FROM HARDWARE** — functional SPI/raw/shutdown T-pass; not calibration, accuracy, or controlled fault injection |
| Connected ordinary runtime | signed 1,445,888-byte application, SHA-256 `4b1541202eaa7388b79c48f9f615fd7443959b5ad943f12652e3cfa4ea95ffcc`; cycles 1/60/180 at 25.7/25.7/25.8 C over about 179 seconds; IDLE, no target, simulated heater 0.0%; no chamber/control failure | **CONFIRMED FROM HARDWARE** — M7 functional ordinary-runtime T-pass; not calibration, longer-duration, response/noise, controlled-fault, heater-interference, physical-regulation, or safety qualification |

The active M7 adapter is implemented in `smoker_platform` behind the existing
`IChamberSensor` port and creates no sensor task. A host-safe seam owns only
project result/configuration types; target-only RAII owns the SPI bus and real
1.0.8 descriptor. Production calls init/fault/temperature/free APIs but avoids
the driver's configuration and fault-clear read-modify-write helpers, using
exact checked register writes instead. It selects continuous conversion with
bias. Configuration success is
reported separately from sample readiness. A host-tested monotonic policy
prevents all fault/temperature register reads before 55 ms at 60 Hz or 66 ms at
50 Hz, including after fault clear/reconfiguration. Early reads return explicit
`NotReady` and map to absence. The driver's `max31865_measure()` remains
forbidden because it contains a 70 ms task delay.

The project-owned read path contains no explicit delay, task creation, or heap
allocation. That source fact and ordinary-C++ host allocation observation do
not prove ESP-IDF/driver/SPI allocation behavior or real SPI worst-case blocking.
The datasheet conversion interval also does not determine module-specific
input-network/bias settling; that requirement stays pending until the physical
module and frontend are known.

Production centralizes provisional Rref 430.0 ohm, 50 Hz, ITS-90, and 100 kHz
without treating them as generic adapter defaults. The final target mapping is
SPI2 with GPIO12 SCK, GPIO11 MOSI, GPIO13 MISO, and GPIO10 CS; PT100 nominal
100 ohm and three-wire are also fixed from the confirmed user selection. Any
descriptor/configuration/SPI/conversion error, reported fault, or
invalid/non-finite result or finite value outside the inclusive supplier-
documented -50..+200 C range becomes an absent authoritative reading without
last-value reuse; the existing synchronous safety path then latches fault and
forces heater OFF. A successful setup completes initialization and first-
conversion readiness before `ControlTask` is created. Sensor bootstrap failure
instead starts the ordinary observation runtime with an unavailable chamber,
so its first tick publishes the latched FAULT and OFF state. This is build-
validated activation plus
functional connected evidence, not calibrated accuracy or a fully validated
physical conversion policy.

The serial activation used all-`0xff` initial OTA metadata in the no-factory
layout. ESP-IDF 6.0.2 selected `ota_0` directly as `ESP_OTA_IMG_VALID`, so no
`PENDING_VERIFY` state or five-cycle message was expected. That criterion is
waived for this serial activation only; OTA-005 still requires five consecutive
safe cycles for actual OTA-installed pending images. No pending state was
created or forced.

The default-OFF diagnostic temporarily enables VBIAS and automatic conversion
only inside its bounded register/sample procedure. Before software-SPI pins or
the driver descriptor/bus are released, checked normal cleanup writes and
reads back exact configuration `0x11`: normally off, VBIAS off, three-wire,
50 Hz, and all one-shot/fault-cycle/fault-clear command bits zero. Early returns
retain bounded best-effort RAII cleanup; software-SPI first restores a CS-high
frame boundary, and descriptor cleanup still attempts removal before bus
release. This is **CONFIRMED FROM CONFIG** for the intended source
sequence. The first connected run did not reach either normal terminal `0x11`;
the later corrected run observed exact software and driver terminal `0x11`.
Those readbacks are **CONFIRMED FROM HARDWARE** transaction evidence, not an
independent electrical measurement of physical quiescence.

### First connected diagnostic result — 2026-08-24

With the heater, SSR, and mains connection physically disconnected, the
separately signed diagnostic composition was installed through the signed
serial helper and monitored for one bounded run. It reported the centralized
SPI2/GPIO12 SCK/GPIO11 MOSI/GPIO13 MISO/GPIO10 CS assignment and 100 kHz
mode-1 software SPI. Configuration readback was `0xff` with the MISO pull-up
and `0x00` with the pull-down. The diagnostic correctly classified GPIO13 as
possibly floating and stopped before trying complementary configuration
patterns or starting the pinned driver stage.

The software-SPI destructor fallback restored a CS-high frame boundary, then
requested configuration `0x10` and observed `0x00`. That exact readback
mismatch made fallback quiescence fail. Neither the software-SPI nor driver
terminal `0x11` was attempted or observed. There were no driver initialization
or register transactions, raw RTD codes, `RRTD/RREF` ratios, or RTD fault-bit
or fault-register samples. This is direct evidence of a failed response and a
failed software fallback readback; it is not proof that the converter accepted
a shutdown command or became physically quiescent.

The ordinary signed image was restored with the same helper immediately after
the run. A bounded monitor showed the ordinary `ControlTask`, simulated chamber
at 25.0 C, no target, simulated heater at 0.0%, and no diagnostic log. No
source or production configuration was changed by the diagnostic exercise.

### Corrected connected functional result — 2026-08-24

Preserved log `idf_py_stdout_output_47381` has SHA-256
`5979dcb174bdc49661c77cb26a67a2ca7db16f3bc42c7daf45a5dbd516d3916d`.
It records SPI2/GPIO12/11/13/10 at 100 kHz, pull-independent initial
configuration `0x11`, exact software quiescent `0x00`, idle-bias
three-wire/50 Hz `0x91`, active `0xD1`, and terminal `0x11`. The pinned-driver
path then observed initial `0x11`, active `0xD1`, ten raw 8548/8549 samples
with ratios 0.260864/0.260895 and fault zero, zero transaction errors, zero
sensor-fault samples, exact terminal `0x11`, and successful descriptor/bus
shutdown.

With provisional 430.0-ohm Rref and ITS-90, independent conversion
corroboration gives 112.171631/112.184753 ohm and 31.287679/31.321568 C for
raw 8548/8549. This checks software interpretation only. It does not identify
or measure fitted Rref, establish its tolerance, inspect the physical probe,
or claim calibration/accuracy.

Preserved log `idf_py_stdout_output_46058` has SHA-256
`f448d05c0dfc35be8dff0aa7e392ec8449b17a369f1ad14e3070f19649a727c3`.
It records the same successful configuration/shutdown paths but ten raw-zero
samples with fault status `0x40`, zero transaction errors, and ten sensor-fault
samples. Because the physical stimulus was not controlled or identified, this
is evidence that the diagnostic reported a fault, not open/short validation.

### Physical facts and validation still required after runtime activation

| Item | Current finding | Classification |
|---|---|---|
| Breakout manufacturer/product/revision/markings | Not recorded | **UNCONFIRMED** |
| Procurement source and final-product status | Probe supplier page is [Sigmanortec SKU 88056](https://sigmanortec.ro/sonda-de-temperatura-pt100-tip-ac-inox-sus304-cablu-fep-ecranat-50200c-clasa-f015); whether the connected unit was inspected against that listing is not recorded. MAX31865 breakout source is not recorded | Supplier listing **CONFIRMED FROM DOCUMENTATION**; physical-unit identity and breakout source **UNCONFIRMED** |
| Module supply and logic-voltage behavior | Maintainer reports ESP32 3V3 to module VIN; chip-level ratings and this connection report do not prove breakout regulation, level shifting, or actual voltage at the module | Connection **CONFIRMED FROM DOCUMENTATION** — maintainer report; electrical behavior **UNCONFIRMED** |
| RTD element | Supplier page documents PT100, -50..+200 C assembled range, `F0.15` tolerance, SUS304 4 mm x 100 mm sheath, 1 m shielded FEP cable, 1/4 NPT, and approximately 100 ohm at 0 C / 138.5 ohm at 100 C; exact standard and connected-unit measurements are absent | Listed assembly facts **CONFIRMED FROM DOCUMENTATION** — supplier page; exact standard and physical verification **UNCONFIRMED** |
| Lead configuration and placement | Maintainer reports three-wire configuration including separate 2/3-wire jumper; red to F+, red to RTD+, blue to RTD-, and RTD- jumpered to F- | **CONFIRMED FROM DOCUMENTATION** — maintainer report; continuity and resistance **UNCONFIRMED** |
| Fitted reference resistor | Nominal value and tolerance not recorded | **UNCONFIRMED** |
| SPI and connector pinout | MCU-side assignment is SPI2/GPIO12 SCK/GPIO11 MOSI/GPIO13 MISO/GPIO10 CS; first diagnostic followed pulls, later corrected setup produced pull-independent configuration/raw exchanges; module-side continuity is not independently measured | Assignment **CONFIRMED FROM DOCUMENTATION**; functional transactions **CONFIRMED FROM HARDWARE**; continuity **UNCONFIRMED** |
| ESP32-S3 GPIO assignment | Final soldered production assignment is centralized in `max31865_board_pins.hpp`; the pins do not conflict with recorded N16R8 PSRAM, strapping, native-USB, UART0, or JTAG restrictions; corrected diagnostic exchanged data on that assignment | Firmware intent **CONFIRMED FROM CONFIG**, assignment **CONFIRMED FROM DOCUMENTATION**, transactions **CONFIRMED FROM HARDWARE** |
| Boot-state behavior and safe power sequencing | Corrected diagnostic booted and both paths read exact terminal `0x11`; ordinary-runtime bootstrap ordering is build validated | Transaction shutdown **CONFIRMED FROM HARDWARE**; safe power sequencing and independent physical quiescence measurement **UNCONFIRMED** |
| Cable shield termination, continuity, and resistance | Shield termination is not recorded; lead/module continuity and resistance have not been measured | **UNCONFIRMED** |
| Bias/input-network settling | Module-specific RC/input network is unknown; no extra settling interval has been selected or tested | **UNCONFIRMED** |
| Accuracy/noise/fault behavior | A three-reading 179-second ordinary ambient observation passed; no calibrated reference-temperature, controlled open/short and recovery, longer-duration, response/noise, or heater-interference test exists | Short functional observation **CONFIRMED FROM HARDWARE**; qualification **UNCONFIRMED** |

Primary software references:

- [ESP Component Registry: esp-idf-lib/max31865 1.0.8](https://components.espressif.com/components/esp-idf-lib/max31865/versions/1.0.8/readme)
- [Versioned upstream source](https://github.com/esp-idf-lib/max31865/tree/79566bd59420b03ab999c124f012a93a63f3a7db)
- [Analog Devices MAX31865 datasheet](https://www.analog.com/media/en/technical-documentation/data-sheets/max31865.pdf) — chip-level authority only; not a schematic for the unknown breakout

## `PROBES-001` — dual ADS1115 acquisition dossier

### Selection and software boundary

The user selected two ADS1115 converters on 2026-08-18. One physical module is
currently installed for four food probes; the second remains selected for the
eventual six-probe product capacity but is deferred, not mounted, and not
assigned. This does not identify either module revision or complete the
food-probe frontend. The maintainer identifies the intended probes as
`NTC100`, which is recorded as a probe label rather than silently interpreted
as an R25 value or a complete resistance-temperature curve. Exact R25,
Beta/Steinhart-Hart coefficients, temperature range, connector pinout, input
protection, gain, rate, and voltage-to-temperature calibration remain required.

ESP-IDF 6.0.2 supplies the I2C master driver but no ADS1115 device driver. The
project therefore uses ESP Component Registry rather than copying a register
implementation:

| Item | Finding | Classification |
|---|---|---|
| Driver | `esp-idf-lib/ads111x` 1.1.14, BSD-3 | **CONFIRMED FROM CONFIG** — exact manifest pin |
| Registry identity | component hash `fd18497adfb7210d750188986bc7cebc048db36abb64fdbe7216d4536083c4a2` | **CONFIRMED FROM CONFIG** — `dependencies.lock` |
| Upstream source | release commit `9eb6f607662518f1bdd3a3b88629db720b765b8e`; explicitly targets `esp32s3` | **CONFIRMED FROM DOCUMENTATION** — registry metadata and versioned source |
| Locked support components | `esp-idf-lib/i2cdev` 2.1.2 (`ad8981cc64533dcaced5107d72e42bcebe79345e194e82795792af531b300ce3`) and `esp-idf-lib/esp_idf_lib_helpers` 1.4.0 (`689853bb8993434f9556af0f2816e808bf77b5d22100144b21f3519993daf237`) | **CONFIRMED FROM CONFIG** — resolved lockfile closure |
| Driver capabilities | ADS1115 input mux, programmable gain, 8..860 SPS data rate, single-shot/continuous modes, explicit start/busy/value operations, thresholds, and comparator configuration | **CONFIRMED FROM DOCUMENTATION** — versioned public header/source |
| Two-device support | Upstream versioned example constructs two descriptors on one I2C bus with distinct ADDR straps | **CONFIRMED FROM DOCUMENTATION** — example; its GND/VCC choices are not project pin assignments |
| ESP-IDF 6 build path | locked `i2cdev` selects ESP-IDF 6's new `i2c_master` driver | **CONFIRMED FROM CONFIG** — fresh ESP32-S3 configure/build output |
| Conversion timing | Conversion time is `1 / DR`; nominal data rates vary by +/-10%; single-shot power-up is approximately 25 us | **CONFIRMED FROM DOCUMENTATION** — TI ADS1115 Rev. E sections 5.5, 7.3.6, and 7.4.2.1 |
| Descriptor initialization | `ads111x_init_desc()` writes a driver-owned 1 MHz clock into `i2c_dev_t` before creating its mutex | **CONFIRMED FROM DOCUMENTATION** — pinned 1.1.14 source |
| Configuration/start boundary | mode/mux/gain/rate setters are read-modify-write operations; non-OS `write_conf_bits()` writes explicitly clear OS, while only the OS setter starts a single-shot conversion | **CONFIRMED FROM DOCUMENTATION** — exact pinned 1.1.14 source |
| Locked I2C behavior | first I/O lazily creates port/device state; mutex and I/O calls use `CONFIG_I2CDEV_TIMEOUT`; retry paths contain `vTaskDelay()` and may recreate device handles | **CONFIRMED FROM DOCUMENTATION** — pinned `i2cdev` 2.1.2 source |
| Current firmware use | inactive adapter and real API backend compile; production continues to compose `SimulatedFoodProbeSource` | **CONFIRMED FROM CONFIG** — focused host tests and ESP-IDF 6.0.2 cross-build only |
| Installed module wiring | ESP 3V3 to V, GND to G, GPIO17 to SDA, GPIO18 to SCL, ADDR to GND, ALERT/RDY disconnected; A0..A3 were floating during the 2026-08-25 digital test | **CONFIRMED FROM DOCUMENTATION** — maintainer report; not continuity or rail measurement |
| Installed module digital response | scan at 100 kHz found `0x48`; ADS1115 reset values were config `0x8583`, low threshold `0x8000`, high threshold `0x7fff`; 16-bit configuration word `0xC383` wrote/read exactly, one A0 single-shot conversion completed, and terminal `0x8583` was restored/read back | **CONFIRMED FROM HARDWARE** — connected target diagnostic on 2026-08-25; A0 was floating, so the raw result is not known-voltage, resistance, temperature, accuracy, or calibration evidence |
| Reported four-channel topology | Four networks assembled as 3V3 -> nominal 100 kOhm 0.1% -> AINx, with `NTC100` and 100 nF capacitor each from AINx to GND | **CONFIRMED FROM DOCUMENTATION** — maintainer assembly report on 2026-08-26; actual rail/resistor values and A0-A2 analog behavior are unmeasured |
| Connected A3 response | failed near-ground run preserved; after wiring correction, 20 room-condition and 20 uncontrolled soldering-tool-heating samples showed connected-path response | **CONFIRMED FROM HARDWARE** — maintainer-provided, hash-verified local session transcript; response evidence only, not curve, temperature, calibration, or accuracy |

Two devices can share a bus only when their physical ADDR straps select two
different addresses from the ADS1115 set `0x48`, `0x49`, `0x4a`, and `0x4b`.
The installed module now uses GPIO17/GPIO18 at 100 kHz and responds at
`0x48`; the logical ESP-IDF I2C port and production driver configuration are
still unselected. The second device address is deliberately not assigned until
it is mounted. Pull-up rail/value, channel gain, data rate, conversion timeout,
cache age, and calibration are not established by the connected tests.

The inactive M9 adapter remains in `smoker_platform` behind the existing
`IFoodProbeSource` port and creates no sensor task. One owner sequences both
descriptors and all mapped channels, with independent synchronization/
quarantine state per physical ADC. Each device first requires a successful
`busy=false` observation, which discards any conversion result surviving an
MCU-only reset without configuring or restarting in that service step. An
unknown/busy device is skipped so the other converter can progress.

A later service step configures explicit mux/gain/rate, starts one single-shot
conversion, and only then establishes its deadline. Active polling observes
busy first: ready is read even at/after the deadline, while still busy at/after
the deadline, a busy-read error, or a failed start quarantines that ADC. The
abandoned result is discarded rather than read or reassigned. A value-read
failure after ready, configuration failure on a synchronized idle device, and
calibration/validity rejection remain probe-local. `read(probe_id)` performs no
I2C and returns only an independently timestamped cache entry before its
configured maximum age expires.

Raw codes enter a mandatory injected calibration/validity policy. The adapter
contains no physical conversion defaults. Configuration requires the I2C port,
SDA/SCL, clock, pull-up policy, addresses, channel map, mux/gain/rate, timeout,
and maximum age. Same-bus devices must agree on bus configuration and use
different addresses; distinct non-overlapping buses may reuse an address. The
target backend overrides the driver-owned 1 MHz descriptor value before its
first I2C transaction and uses the real pinned init/free, mode, mux, gain, rate,
start, busy, and raw-value APIs.

Project-owned service/read code contains no explicit delay, poll loop, task
creation, or steady-state allocation. That does not remove locked `i2cdev`'s
mutex waits, transaction timeouts, lazy setup, internal retry delays, or
possible driver/ESP-IDF allocation. The inactive adapter is therefore not yet
approved for ControlTask placement. Production neither calls `i2cdev_init()`
nor constructs it. Failures clear only the affected monitoring sample; only
ambiguous device state adds quarantine, and BR-005/SF-008 keep chamber control
and heater demand unchanged.

### Installed module and connected digital result — 2026-08-25

The first module is physically wired as follows. GPIO17 and GPIO18 are exposed
ordinary header pins on `CTRL-001` and do not overlap the recorded MAX31865,
Octal-PSRAM, strapping, native-USB, UART0, JTAG, or RGB assignments.

| ESP32-S3 `CTRL-001` | ADS1115 module | Purpose/status |
|---|---|---|
| 3V3 | `V` | 3.3 V supply, maintainer-reported connection |
| GND | `G` | common ground, maintainer-reported connection |
| GPIO17 | `SDA` | I2C data; connected response passed |
| GPIO18 | `SCL` | I2C clock; 100 kHz diagnostic passed |
| GND | `ADDR` | address `0x48`; connected response passed |
| not connected | `ALERT` / `RDY` | unused in the staged single-shot design |
| not connected during test | `A0..A3` | four future NTC divider inputs; floating conversion has no physical meaning |

An official ESP-IDF 6.0.2 `i2c_tools` diagnostic configured SDA GPIO17, SCL
GPIO18, and 100 kHz. It found `0x48` and also an ACK at general-call address
`0x00`; the latter is compatible with ADS1115 General Call and is not recorded
as a second device. Register reads matched ADS1115 reset values: configuration
`0x8583`, low threshold `0x8000`, and high threshold `0x7fff`. The diagnostic
wrote the 16-bit configuration word `0xC383` for one single-ended A0
single-shot conversion at the +/-4.096 V range and 128 SPS, read the same word
back, obtained raw `0x5C71`, then restored and verified terminal `0x8583`.
Because A0 was floating, neither `0x5C71` nor the test gain/rate is a probe
reading or a production analog setting.

This closes first-module digital communication only. It does not activate the
inactive M9 adapter, establish ControlTask timing, prove module identity or
pull-up values, or validate any voltage, NTC, temperature, disconnect, short,
noise, accuracy, heater-interference, or sustained-run behavior.

### Connected A3 response evidence — 2026-08-26

The authoritative evidence is the maintainer-provided local session transcript
with independently verified SHA-256
`dcfa52e2a352519735c28716afb0eb2c34ed53b1fdd0239664a39b06357c695f`.
The surviving temporary diagnostic source independently matches SHA-256
`4cc3eaf477a160aa5e3ebef44a760ef798cbe08fb78e8eee2bd67b5c424a9a68`.
No transcript, source, log, binary, or temporary artifact is copied into the
repository.

This was a temporary signed ESP-IDF 6.0.2 application using direct
`i2c_master` APIs, not `i2c_tools` and not the project backend. It configured
GPIO17 SDA, GPIO18 SCL, address `0x48`, 100 kHz, direct single-ended A3
one-shot reads, and internal SDA/SCL pull-ups. It did not exercise
`Ads1115TargetBackend`, pinned `i2cdev`, the M9 sequencer, production
composition, ControlTask timing, or sustained acquisition.

The initial 20-sample run began with configuration word `0xF383`. Raw values
were approximately -4 through -2 and calculated voltage approximately
-0.0005 through -0.0003 V. This failed observation is consistent with A3 being
effectively grounded or lacking the high-side divider branch. It is preserved
as a wiring failure, not hidden as successful NTC response. The maintainer then
corrected the wiring before the next run.

The corrected room-condition run began with `0x8583` and produced:

| Sample boundary | Raw code | Calculated voltage | Approximate resistance |
|---|---:|---:|---:|
| 1 of 20 | 18329 | 2.2911 V | 227.10 kOhm |
| 20 of 20 | 18040 | 2.2550 V | 215.79 kOhm |

This proves response through the connected A3 divider/jack/probe path only. It
does not establish probe temperature, R25, Beta, curve identity, calibration,
or accuracy.

For the next run the maintainer heated the probe using a soldering tool
(`pistolul de lipit`), not a calibrated bath. It also began with `0x8583`:

| Sample boundary | Raw code | Calculated voltage | Approximate resistance |
|---|---:|---:|---:|
| 1 of 20 | 8300 | 1.0375 V | 45.86 kOhm |
| 20 of 20 | 1174 | 0.1468 V | 4.65 kOhm |

The series is a strong overall decrease consistent with NTC behavior. It is
not strictly monotonic: raw sample 12 was 1992 and sample 13 was 2029 before
the overall decrease continued. No temperature, B3950, R25, Beta,
Steinhart-Hart coefficient, calibration, or accuracy may be inferred.

All resistance values above used nominal values only:

```text
R_NTC ~= 100 kOhm * Vnode / (3.3 V - Vnode)
```

Neither the actual 3V3 rail nor the individual 100 kOhm reference resistor was
measured. Derived resistance is therefore approximate hardware-response
evidence only.

At the end of each A3 run, the diagnostic wrote `0x8583` and immediately read
`0x0583`. That result is consistent with OS/busy after starting a one-shot
conversion; the procedure did not subsequently prove terminal idle readback of
`0x8583`. Before relying on this A3 procedure again, it must wait/poll for idle
and verify terminal `0x8583`. This limitation must not be merged with the
separate 2026-08-25 digital test, which did report verified restoration and
readback of `0x8583`.

The last known image on the connected board at the end of this separate
session was the temporary signed A3 diagnostic, not the ordinary production
firmware. The board is unavailable for this documentation remediation, so no
serial access, reset, flash, monitor, or restoration is performed. Its last
known image does not change repository production composition, which still
uses `SimulatedFoodProbeSource`.

### Reported assembled four-channel `NTC100` analog frontend

The maintainer reports four 100 kOhm, 0.1% fixed resistors and four 100 nF
ceramic capacitors assembled in the intended per-channel topology. This is an
assembly report, not measured component/rail evidence or a validated production
conversion:

```text
3V3 --- 100 kOhm, 0.1% ---+--- ADS1115 AINx
                           |
                           +--- 100 nF ceramic --- GND
                           |
                           +--- NTC through jack --- GND
```

The reported assembly repeats the same independent network for A0, A1, A2,
and A3:

| Channel | Fixed resistor | Filter capacitor | Intended probe |
|---|---:|---:|---|
| A0 | 100 kOhm, 0.1%, from 3V3 to A0 | 100 nF ceramic, A0 to GND | `NTC100` probe 1 |
| A1 | 100 kOhm, 0.1%, from 3V3 to A1 | 100 nF ceramic, A1 to GND | `NTC100` probe 2 |
| A2 | 100 kOhm, 0.1%, from 3V3 to A2 | 100 nF ceramic, A2 to GND | `NTC100` probe 3 |
| A3 | 100 kOhm, 0.1%, from 3V3 to A3 | 100 nF ceramic, A3 to GND | `NTC100` probe 4 |

The fixed resistor and NTC form the resistance-to-voltage divider. The 100 nF
capacitor is non-polarized and filters noise at the ADC node; it is not a
series component and does not replace the fixed resistor. Place each fixed
resistor and capacitor near the ADS1115, keep each AIN node short, and route
that node plus a ground return to the remote jack. The jack may be mounted on
the opposite side of the perfboard, but its contact assignment and cable
shield/ground policy must be documented before production use.

For this reported high-side-resistor topology:

```text
V_AIN = 3.3 V * R_NTC / (100 kOhm + R_NTC)
R_NTC = 100 kOhm * V_AIN / (3.3 V - V_AIN)
```

An open/disconnected probe tends toward 3.3 V and a shorted probe tends toward
0 V, but validated thresholds need measured rail, ADC error, wiring leakage,
and connected tests. If later measurement proves `R_NTC = 100 kOhm` at the
chosen reference temperature, the ideal midpoint is about 1.65 V and the
midpoint Thevenin resistance is about 50 kOhm. That relatively high source
impedance and the nominal 5 ms midpoint RC time constant must be included in
ADS1115 gain/rate/settling/error validation. The resistor value is therefore
provisional until the probe's actual R25 and curve are measured or documented.

### Physical facts still required before runtime activation

| Item | Current finding | Classification |
|---|---|---|
| Module manufacturer/product/revision/markings | Installed purple module is connected but its manufacturer, exact ADS1115-vs-ADS1015 marking, product, and revision are not recorded; second module is not mounted | **UNCONFIRMED** |
| Procurement source and final-product status | Not recorded | **UNCONFIRMED** |
| Supply and logic-voltage behavior | Maintainer reports installed module V to ESP 3V3 and G to common ground; no rail measurement, breakout schematic, regulator/level-shifter identity, or current measurement exists | Connection **CONFIRMED FROM DOCUMENTATION**; electrical behavior **UNCONFIRMED** |
| ADDR straps / I2C addresses | Installed module ADDR is reported tied to GND and responds at `0x48`; second module requires a distinct future address | Installed address/response **CONFIRMED FROM HARDWARE**; physical strap from maintainer report; second address **UNCONFIRMED** |
| I2C bus | Installed module passed at GPIO17 SDA/GPIO18 SCL and 100 kHz; the A3 diagnostic additionally enabled internal pulls. Logical production port, external pull-up rail/value, bus length/capacitance, and real-service timing remain open | Pins/clock/response **CONFIRMED FROM HARDWARE**; internal-pull policy **CONFIRMED FROM CONFIG** of the temporary source; remaining electrical/runtime facts **UNCONFIRMED** |
| Signal/probe channel map | Installed-module A0..A3 are reserved in order for probes 1..4; only A3 has analog-response evidence. Final logical IDs/purposes and second-module mapping remain open | Initial reservation **CONFIRMED FROM DOCUMENTATION** — maintainer choice; A0-A2 analog behavior **UNCONFIRMED** |
| Analog frontend | Four networks are reported assembled as 3V3 -> nominal 100 kOhm 0.1% -> AINx, with `NTC100` and 100 nF ceramic each from AINx to GND. Neither actual 3V3 nor any individual reference resistor was measured; no input-protection design exists | Assembly/topology **CONFIRMED FROM DOCUMENTATION**; connected A3 response **CONFIRMED FROM HARDWARE**; component values, A0-A2 electrical behavior, and calibration **UNCONFIRMED** |
| Conversion policy | Single-ended/differential mode, gain, data rate, scheduling, and calibration not selected | **UNCONFIRMED** |
| ESP32-S3 GPIO assignment | Installed bus uses GPIO17 SDA and GPIO18 SCL and does not conflict with recorded `CTRL-001` restrictions or MAX31865 GPIO10..13; second module is unassigned | Installed assignment **CONFIRMED FROM DOCUMENTATION** and functional response **CONFIRMED FROM HARDWARE**; second assignment **UNCONFIRMED** |
| Connected behavior | Installed module passed the separate 2026-08-25 digital checks. On 2026-08-26 the initial A3 run failed near ground; after correction, room and uncontrolled-heating runs showed A3 path response with nominal resistance decreasing overall | Digital and A3 response **CONFIRMED FROM HARDWARE**; A0-A2, known resistance/voltage, disconnect/open/short, settling/noise, accuracy/repeatability, sustained-run, and heater-interference behavior **UNCONFIRMED** |
| Diagnostic terminal state | 2026-08-25 digital test restored/read `0x8583`. The later A3 diagnostic immediately read `0x0583` after writing `0x8583`, so terminal idle was not proved and needs an idle poll/readback before procedure reuse | Separate digital terminal readback **CONFIRMED FROM HARDWARE**; A3 terminal-idle state **UNCONFIRMED** |
| Calibration/reference | No ice-bath or simultaneous co-located calibration run occurred. PT100/MAX31865 is only a possible future reference after its own accuracy/reference checks | **UNCONFIRMED** |

Primary software references:

- [ESP Component Registry: esp-idf-lib/ads111x 1.1.14](https://components.espressif.com/components/esp-idf-lib/ads111x/versions/1.1.14/readme)
- [Versioned upstream source](https://github.com/esp-idf-lib/ads111x/tree/9eb6f607662518f1bdd3a3b88629db720b765b8e)
- [TI ADS1115 datasheet](https://www.ti.com/lit/ds/symlink/ads1115.pdf) — chip-level authority only; not a schematic for either unknown module or probe frontend

## `CTRL-001` — SuooTci controller-board dossier

### Build/platform identity

| Item | Finding | Classification |
|---|---|---|
| Framework | Native ESP-IDF, exactly v6.0.2 | **CONFIRMED FROM CONFIG** — root `CMakeLists.txt` version gate |
| Build target | Generic `esp32s3`; `main` also restricts `REQUIRED_IDF_TARGETS` to `esp32s3` | **CONFIRMED FROM CONFIG** |
| Board definition | No SuooTci, DevKitM, DevKitC, Arduino, or PlatformIO board definition exists; the build does not encode a carrier identity | **CONFIRMED FROM CONFIG** |
| Project storage selection | 16 MB flash; PSRAM enabled in Octal mode at 80 MHz | **CONFIRMED FROM CONFIG** — `sdkconfig.defaults` |
| Console | UART0 at 115200 baud is primary; native USB Serial/JTAG is secondary | **CONFIRMED FROM CONFIG** — effective `sdkconfig` |
| External application GPIO | Production assigns MAX31865 to GPIO10..13; the installed, still-production-inactive ADS1115 uses GPIO17 SDA/GPIO18 SCL. No SSR/heater, fan, smoke-source, display, or touch GPIO is assigned | MAX31865 **CONFIRMED FROM CONFIG**; ADS1115 assignment **CONFIRMED FROM DOCUMENTATION** with connected digital/A3 response; remaining interfaces **UNCONFIRMED** |
| Generated PSRAM I/O values | `CONFIG_SPIRAM_CLK_IO=30`, `CONFIG_SPIRAM_CS_IO=26` | **CONFIRMED FROM CONFIG** — internal memory-interface configuration, not smoker peripheral assignments |

### Direct electronic identity

The same physical unit was queried on 2026-08-17 with Espressif `esptool
v5.3.1` through both USB paths. The identical MAC ties both paths to the same
ESP32-S3. Queries reset the device into the ROM loader but did not write flash.

| Item | Finding | Classification |
|---|---|---|
| SoC | ESP32-S3, QFN56, revision v0.2 | **CONFIRMED FROM HARDWARE** |
| Processing/features | Dual Core + LP Core, 240 MHz; 40 MHz crystal | **CONFIRMED FROM HARDWARE** |
| Radio silicon features | Wi-Fi and Bluetooth 5 LE | **CONFIRMED FROM HARDWARE** — traffic/coverage is a separate test |
| Base MAC | `[REDACTED_UNIQUE_DEVICE_ID]` | **CONFIRMED FROM HARDWARE** — exact value retained only in the private evidence archive |
| Unique chip ID | ESP32-S3 exposes no separate unique chip ID through `esptool chip-id`; the tool reads MAC instead | **CONFIRMED FROM HARDWARE** |
| Flash | 16 MB; manufacturer ID `0x68`, device ID `0x4018`; Quad data mode; 3.3 V eFuse selection | **CONFIRMED FROM HARDWARE** |
| PSRAM | Embedded 8 MB, reported as `AP_3v3` | **CONFIRMED FROM HARDWARE** |
| Security state | Secure Boot disabled; flash encryption disabled; `SPI_BOOT_CRYPT_CNT=0` | **CONFIRMED FROM HARDWARE** — bring-up state, not final production policy |
| Exact shield/module marking | User reports N16R8 and photographs show a WROOM-1-style module, but the printed marking was not independently transcribed during this query | **UNCONFIRMED** by direct inspection; procurement/user/photo evidence only |

Relevant direct output:

```text
Chip type:          ESP32-S3 (QFN56) (revision v0.2)
Features:           Wi-Fi, BT 5 (LE), Dual Core + LP Core, 240MHz,
                    Embedded PSRAM 8MB (AP_3v3)
Crystal frequency:  40MHz
MAC:                [REDACTED_UNIQUE_DEVICE_ID]

Flash Memory Information:
Manufacturer: 68
Device: 4018
Detected flash size: 16MB
Flash type set in eFuse: quad (4 data lines)
Flash voltage set by eFuse: 3.3V
```

### USB and serial paths

With the USB connector openings facing the observer, product photographs place
native ESP32-S3 USB on the left and the USB-to-UART path on the right. Connector
position and the exact CH343P marking are **CONFIRMED FROM DOCUMENTATION**;
enumeration and communication results below are direct observations.

| Path | 2026-08-17 observed identity | Validation | Classification |
|---|---|---|---|
| Native ESP32-S3 USB | macOS port and unique USB serial redacted; Espressif `USB JTAG/serial debug unit`; VID:PID `303a:1001`; USB Full Speed 12 Mbit/s | `esptool chip-id` and `flash-id` succeeded; tool reported `USB mode: USB-Serial/JTAG` | **CONFIRMED FROM HARDWARE** — exact identifiers retained only in the private evidence archive |
| WCH USB-to-UART | macOS port and unique USB serial redacted; `USB Single Serial`; VID:PID `1a86:55d3` | automatic ROM-loader reset plus `esptool chip-id`, `flash-id`, and security query succeeded over UART0 | **CONFIRMED FROM HARDWARE** — exact identifiers retained only in the private evidence archive |
| Native USB OTG application behavior | No host/device OTG peripheral scenario was exercised | None | **UNCONFIRMED** |
| Integrated USB JTAG | Native device enumerated as USB JTAG/serial and `esptool` identified USB-Serial/JTAG mode | Enumeration/query | **CONFIRMED FROM HARDWARE** |
| Exact USB-to-UART IC | Listing photograph identifies CH343P; USB IDs establish WCH but do not alone prove the exact package/part | Photograph plus enumeration | CH343P **CONFIRMED FROM DOCUMENTATION**; WCH path **CONFIRMED FROM HARDWARE** |

### Carrier identity boundary

The connected silicon cannot electronically report the carrier brand or seller
SKU. Therefore SuooTci `KFB003` / eMAG `D1T7M22BM` is **CONFIRMED FROM
DOCUMENTATION** (procurement identity, user confirmation, and product
photographs), not from `esptool`. The observed 16 MB flash plus 8 MB PSRAM agrees
with the reported N16R8 storage variant but does not substitute for reading the
module's printed marking.

## M6A — Controller-board checklist

Record with evidence:

- exact development-board model;
- ESP32-S3 module marking;
- whether it is the intended product controller or a development-only board;
- flash size;
- PSRAM presence/size;
- USB/JTAG/UART capabilities;
- integrated Wi-Fi/Bluetooth LE capability;
- onboard display/touch controller if present;
- exposed/usable GPIO;
- boot/strapping restrictions;
- current simulated image boot/runtime result on the exact board;
- `ControlTask` scheduling and stack high-water result;
- task-watchdog timeout/reset and observed reset-cause result.

### Historical M6A runtime evidence collected on 2026-08-16

| Item | Current evidence | Classification |
|---|---|---|
| Controller/module identity | The user identified the purchased board as SuooTci ESP32-S3, seller code `KFB003` / eMAG product `D1T7M22BM`, and confirmed it is the final product controller. Listing photographs show the S3-WROOM-1-style carrier layout; the user reports the module variant as N16R8. `esptool` independently identifies ESP32-S3 QFN56 revision v0.2, dual cores, and embedded 8 MB PSRAM. | Carrier/product role **CONFIRMED FROM DOCUMENTATION**; SoC/PSRAM **CONFIRMED FROM HARDWARE**; exact printed module marking **UNCONFIRMED** |
| Flash and PSRAM | Espressif `ESP32-S3-WROOM-1/1U` datasheet v1.8, Table 1-1, lists `ESP32-S3-WROOM-1-N16R8` with 16 MB Quad SPI flash and 8 MB Octal SPI PSRAM. Target queries detected 16 MB flash, Quad data mode, 3.3 V flash voltage, and 8 MB PSRAM; `sdkconfig.defaults` selects the flash size and PSRAM mode/speed. | Capacity/mode readback **CONFIRMED FROM HARDWARE**; build selection **CONFIRMED FROM CONFIG**; N16R8 characteristics **CONFIRMED FROM DOCUMENTATION** |
| Integrated radio | Espressif ESP32-S3 datasheet v2.2 specifies 2.4 GHz 802.11b/g/n Wi-Fi and Bluetooth 5 LE; `esptool` reports Wi-Fi and BT 5 LE silicon features. | Silicon features **CONFIRMED FROM HARDWARE** and **CONFIRMED FROM DOCUMENTATION**; radio scenarios not covered here |
| USB/JTAG/UART | Listing photographs identify two USB-C connectors: with the openings facing the observer, the left connector is native ESP32-S3 USB/OTG and the right connector uses a CH343P USB-to-UART bridge. The left connector enumerated in ROM as Espressif USB Serial/JTAG, VID `0x303a`, PID `0x1001`, and was used successfully for queries and flashing. The right connector enumerated as WCH `USB Single Serial`, VID `0x1a86`, PID `0x55d3`, and was used successfully for automatic reset, flashing, flash verification, NVS erase/readback, and UART0 boot/runtime monitoring during the D045/D046 checks. | Connector placement/CH343P **CONFIRMED FROM DOCUMENTATION**; both communication paths **CONFIRMED FROM HARDWARE**; OTG application behavior **UNCONFIRMED** |
| Exposed/restricted pins | Product photographs show 44 header positions exposing GPIO0-21 and GPIO35-48 plus power/reset/ground positions. N16R8 Octal PSRAM reserves GPIO35-37 despite their carrier silkscreen positions. GPIO0, GPIO3, GPIO45, and GPIO46 are strapping pins; GPIO19/20 serve native USB; GPIO39-42 serve external JTAG; GPIO43/44 serve UART0; GPIO48 is shared with the onboard RGB LED. | Silkscreen and SoC/module restrictions **CONFIRMED FROM DOCUMENTATION**; individual carrier continuity **UNCONFIRMED** |
| Onboard components | Photographs show BOOT and RST buttons, CH343P USB-to-UART, PWR/TX/RX indicators, and an RGB LED on GPIO48. No onboard display or touch controller is present. | **CONFIRMED FROM DOCUMENTATION** — product photographs |
| Product status | The user confirms this SuooTci board is the final smoker-controller board. | **CONFIRMED FROM DOCUMENTATION** — project procurement/selection record |
| Chip security state | Target query reports secure boot disabled and flash encryption disabled. This is bring-up evidence, not the final product security policy. | **CONFIRMED FROM HARDWARE**; production policy **UNCONFIRMED** |
| Simulation image flash | ESP-IDF v6.0.2 wrote and hash-verified the 16 MB-configured bootloader, partition table, and application through native USB Serial/JTAG. | **CONFIRMED FROM HARDWARE** |
| Simulation runtime | With BOOT released, the normal image booted from flash. Initial target execution exposed a `1s` chrono-period conversion that produced zero FreeRTOS ticks and an assert; the conversion was corrected and the restored normal image then ran through at least 420 one-second control cycles without a reset. | **CONFIRMED FROM HARDWARE** — simulated I/O image only |
| ControlTask scheduling/stack | The task ran on core 0 at priority 2. At cycle 1 its minimum free stack was 11,028 of 12,288 bytes; from cycle 60 through cycle 420 it stabilized at 10,868 bytes minimum free. | **CONFIRMED FROM HARDWARE** for that image/build |
| TWDT/reset | A temporary, disclosed seven-second `ControlTask` delay intentionally exceeded the configured five-second TWDT window. At about 6.755 s ESP-IDF named `ControlTask (CPU 0)` as overdue, panicked, and reset. The next boot reported `reset_reason=6`. The diagnostic was removed, and the normal image was rebuilt, hash-verified, reflashed, and observed stable through cycle 420. | **CONFIRMED FROM HARDWARE**; normal image restored |

### Carrier header inventory

With the module/antenna at the top and USB connectors at the bottom, the product
photographs show these front-silkscreen header positions from top to bottom:

- left: `3V3, 3V3, RST, 4, 5, 6, 7, 15, 16, 17, 18, 8, 3, 46, 9, 10, 11, 12, 13, 14, 5Vin, GND`;
- right: `GND, TX, RX, 1, 2, 42, 41, 40, 39, 38, 37, 36, 35, 0, 45, 48, 47, 21, 20, 19, GND, GND`.

`TX` is UART0 GPIO43 and `RX` is UART0 GPIO44. This inventory records
exposure, not permission to assign a function. GPIO35-37 are unavailable on the
N16R8 variant, and the other shared/strapping restrictions above must be applied
before M6B pin assignment.

The eMAG title says “34 pins”, while its front/back and dimensional photographs
show 44 header positions. A generic marketing slide also says 8 MB flash, while
the product title says 16 MB and target readback confirms 16 MB. The photographed
carrier silkscreen plus target readback and official N16R8 restrictions are
authoritative for project planning; inconsistent seller artwork is not treated
as electrical or storage evidence.

### Boundary versus official Espressif ESP32-S3-DevKitM-1

The generic `S3-DevKitM-1` label present in seller pinout artwork is not a board
identity. The official Espressif guide establishes the following comparison;
all official-board facts in this table are **CONFIRMED FROM DOCUMENTATION**.

| Item | SuooTci `CTRL-001` | Official ESP32-S3-DevKitM-1 | Result |
|---|---|---|---|
| Module family | WROOM-1-style carrier/module in product photographs; N16R8 reported | ESP32-S3-MINI-1 or MINI-1U | Different; SuooTci exact printed module marking remains unconfirmed by direct transcription |
| Flash | 16 MB **CONFIRMED FROM HARDWARE** | Guide identifies ESP32-S3FN8 with 8 MB flash | Different |
| PSRAM | 8 MB **CONFIRMED FROM HARDWARE** | The consulted DevKitM-1 guide does not provide a sufficiently clear capacity statement for an exact PSRAM comparison | Official-board PSRAM comparison **UNCONFIRMED** |
| USB connectors | Two USB-C connectors **CONFIRMED FROM DOCUMENTATION**; both electrical paths exercised | Official guide specifies Micro-USB for the USB-to-UART connection and depicts the official carrier | Different physical carrier/connectors |
| Header positions | 44 positions in product photographs, despite the seller title saying 34 | Two 22-position blocks, J1 and J3 | Same count does not imply the same pinout |
| Header signals/order | SuooTci inventory in this file | Official J1/J3 order differs and additionally exposes GPIO26, GPIO33, and GPIO34 | Not pin-compatible |
| RGB LED | GPIO48 | GPIO48 | Shared feature only; not proof of carrier identity |
| Product status | Selected final controller for this project | Official DevKitM-1 is end-of-life | Different product identity/status |

No evidence confirms equivalent schematics, regulator limits, auto-reset
circuits, USB bridge IC, mechanical dimensions, or header continuity. Never use
the official DevKitM-1 schematic as the SuooTci carrier schematic.

Primary references:

- [ESP32-S3-WROOM-1/1U datasheet v1.8](https://documentation.espressif.com/esp32-s3-wroom-1_wroom-1u_datasheet_en.pdf)
- [ESP32-S3 Series datasheet v2.2](https://documentation.espressif.com/esp32-s3_datasheet_en.pdf)
- [SuooTci KFB003 eMAG product page](https://www.emag.ro/placa-dezvoltare-suootci-esp32-s3-dual-wi-fi-bluetooth-16mb-34-pini-usb-c-kfb003/pd/D1T7M22BM/) — procurement identity and carrier photographs; seller title-count discrepancy noted above
- [Official ESP32-S3-DevKitM-1 user guide](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s3/esp32-s3-devkitm-1/user_guide.html) — official-board comparison only; not a schematic for the SuooTci carrier
- [ESP32-S3-DevKitC-1 documentation](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s3/esp32-s3-devkitc-1/index.html) — layout comparison only; the SuooTci carrier is not claimed to be an official Espressif board

M6A observes target reset behavior for bring-up evidence. Persisted reset-reason
recording and recovery policy remain M10 work.

Do not turn a cross-build, module datasheet, or development-board result into a
claim about an untested final product board.

## Inactive M8 software-selection note

The exact-pinned `espressif/pid_ctrl` 0.3.1 float backend is a
**CONFIRMED FROM CONFIG** software choice only. It is compiled behind an
application port but is not composed in production; the deterministic 100/0
controller and `SimulatedHeaterOutput` remain active. No PID form, physical
gain, call period, positional accumulated-error bound, common output limit,
derivative treatment, autotuning method, SSR window, GPIO, or heater interface
has been selected. Positional form accumulates/clamps error; incremental form
instead retains/clamps output and has no project-visible integral-bound promise.
Both differentiate error without filtering or derivative-on-measurement. The
component has no `dt` or autotuning API, so real cadence binding, setpoint-kick
assessment, and any tuning approach remain separate M8 gates.

This software evidence does not update `HEATER-001`, `SAFETY-001`, or
`POWER-001`: all remain **UNCONFIRMED**. Automatic tuning cannot be evaluated
safely before the real chamber sensor, SSR/heater, smoker thermal plant, and
independent cutoff are available and validated. Simulated coefficients/results
must not be treated as production recommendations.

## M6B — External-hardware checklist

Record with component identifiers, interface requirements, and electrical
design evidence:

- actual chamber sensor/frontend;
- food-probe frontend/protocol;
- SSR model/input requirements;
- heater power;
- independent thermal/electrical cutout design;
- power-supply rails;
- optional current-sensing hardware.

The final MAX31865 assignment is now recorded as SPI2/GPIO12/11/13/10 from the
maintainer-reported soldered board and has been checked against the recorded
M6A restrictions. The first ADS1115 bus assignment GPIO17 SDA/GPIO18 SCL,
3.3 V, ADDR=GND/`0x48` is also recorded and has passed connected digital and
A3-path response checks, with the limitations above; A0-A2, the second ADS1115,
and SSR assignments still require their corresponding M6B interface facts.
Supplier probe characteristics and the maintainer-reported
VIN/jumper/lead placement are recorded, but the first connected run failed at
pull-following MISO, while the later corrected run established the functional
response recorded above. The assignment and those documented facts do not
complete the remaining module-identity, electrical-behavior, fitted-Rref/tolerance,
continuity, shield-termination, calibrated-accuracy, controlled-fault/recovery,
longer-duration, response/noise, heater-interference, or independent electrical/
thermal-safety gates. Those M6B/pre-real-heater and release gates do not reopen
the completed M7 functional activation and do not block beginning ADS1115
integration.

## OTA/history partition note

M6A confirmed 16 MiB flash. M13 target-validated rollback with the preserved
24 KiB NVS range, `otadata`, `phy_init`, and two 3 MiB OTA slots. M14 retains
those offsets and assigns the next 4 MiB to the raw `history` log, leaving
`0x5e0000` bytes unallocated.

Changing either the former M12 single-app table or the M13 dual-slot table
requires the signed full-serial helper; application OTA cannot migrate a
partition table. M14 connected-target persistence/flash validation remains a
separate gate and is not sensor, SSR, thermal, or electrical-safety evidence.
