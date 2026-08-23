# Hardware and Component Inventory

Status: **M6A complete; M6B external hardware incomplete**

Last evidence update: **2026-08-22**

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
  frontend to connect; the exact physical module/revision and remaining RTD
  assembly facts are not yet documented.
- The maintainer reports the final MAX31865 wiring is already soldered to SPI2
  with GPIO12 SCK, GPIO11 MOSI, GPIO13 MISO, and GPIO10 CS. This is a confirmed
  production assignment, not evidence of continuity, power, or SPI response.
- Two ADS1115 converters are selected by the user for the external analog/probe
  path; their exact modules, addresses, channel roles, probe circuits, and
  wiring are not yet documented. The SSR interface is not yet identified.
- The project will eventually control an electric smoker heater.
- Real chamber sensing, food-probe integration, SSR wiring, independent thermal/electrical protection, current sensing, fan control, and smoke-source integration are separate hardware milestones.

## Component register

| ID | Product role | Selected component | Current classification | Integration status |
|---|---|---|---|---|
| `CTRL-001` | Main controller | SuooTci `KFB003` / eMAG `D1T7M22BM`; N16R8 variant reported | Carrier identity **CONFIRMED FROM DOCUMENTATION**; SoC/storage/USB **CONFIRMED FROM HARDWARE** | M6A complete; simulated I/O only |
| `CHAMBER-001` | Authoritative chamber sensor/frontend | MAX31865 with PT100, three-wire; final SPI2/GPIO12/11/13/10 wiring assigned; exact physical module and remaining RTD facts pending | Converter/RTD/wire and soldered pin assignment **CONFIRMED FROM DOCUMENTATION** — maintainer report; continuity/module/electrical facts **UNCONFIRMED** | Assignment is in target firmware; adapter remains inactive and connected validation is blocked at M6B/M7 |
| `PROBES-001` | Food-probe analog acquisition | Two ADS1115 converters selected; complete probe frontend and channel map pending | Converter quantity/type **CONFIRMED FROM DOCUMENTATION** — user selection; modules/electrical design **UNCONFIRMED** | Software adapter implemented/inactive; production, wiring, and connected validation blocked at M6B/M9 |
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

Production must use simulated sensor/probe/heater adapters until the
corresponding M6B hardware is confirmed. An inactive external-adapter software
boundary may be host-tested and cross-built when every unknown hardware value
remains required configuration and no bus, pin, or runtime activation is
invented. M6A may exercise only the controller board and its integrated
capabilities; this does not validate external control hardware.

## `CHAMBER-001` — MAX31865 chamber-frontend dossier

### Selection and software boundary

The user selected MAX31865 on 2026-08-18 as the first external device to
connect and as the intended authoritative chamber frontend, then identified
the RTD as PT100 with three leads. This fixes the future driver choices to
`rtd_nominal = 100.0F` and `MAX31865_3WIRE`. It does not identify the purchased
breakout-board manufacturer, revision, fitted reference resistor, PT100
accuracy/range/construction, or connector pinout.

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
| Current firmware use | final board pins are centralized in target platform code; inactive adapter and real API backend compile; the opt-in diagnostic has checked command-zero converter quiescence before resource release; ordinary composition still uses `SimulatedChamberSensor` | **CONFIRMED FROM CONFIG** — source, host tests, and ESP-IDF 6.0.2 cross-build only; physical shutdown is unexecuted |

The inactive M7 adapter is implemented in `smoker_platform` behind the existing
`IChamberSensor` port and creates no sensor task. A host-safe seam owns only
project result/configuration types; the target-only RAII backend calls the real
1.0.8 `max31865_init_desc()`, `max31865_set_config()`,
`max31865_get_fault_status()`, `max31865_read_temperature()`,
`max31865_clear_fault_status()`, and `max31865_free_desc()` APIs. It
provisionally selects continuous conversion with bias. Configuration success is
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

Reference resistance, filter, RTD standard, and SPI clock remain explicit
required configuration with no project defaults. The final target mapping is
SPI2 with GPIO12 SCK, GPIO11 MOSI, GPIO13 MISO, and GPIO10 CS; PT100 nominal
100 ohm and three-wire are also fixed from the confirmed user selection. Any
descriptor/configuration/SPI/conversion error, reported fault, or
invalid/non-finite result becomes an absent authoritative reading without
last-value reuse; the existing synchronous safety path then latches fault and
forces heater OFF. This is host behavior plus target API cross-build evidence,
not a physically validated conversion policy.

The default-OFF diagnostic temporarily enables VBIAS and automatic conversion
only inside its bounded register/sample procedure. Before software-SPI pins or
the driver descriptor/bus are released, checked normal cleanup writes and
reads back exact configuration `0x11`: normally off, VBIAS off, three-wire,
50 Hz, and all one-shot/fault-cycle/fault-clear command bits zero. Early returns
retain bounded best-effort RAII cleanup; software-SPI first restores a CS-high
frame boundary, and descriptor cleanup still attempts removal before bus
release. This is **CONFIRMED FROM CONFIG** for the intended source
sequence only. No connected command has been run, so module acceptance of the
write and physical quiescence remain **UNCONFIRMED**.

### Physical facts still required before runtime activation

| Item | Current finding | Classification |
|---|---|---|
| Breakout manufacturer/product/revision/markings | Not recorded | **UNCONFIRMED** |
| Procurement source and final-product status | Not recorded | **UNCONFIRMED** |
| Module supply and logic-voltage behavior | Not recorded; chip-level ratings do not prove breakout behavior | **UNCONFIRMED** |
| RTD element | PT100; nominal driver value 100 Ω at 0 °C. Accuracy class, range, sheath, and cable are not recorded | PT100 choice **CONFIRMED FROM DOCUMENTATION** — user; remaining construction facts **UNCONFIRMED** |
| Lead configuration | Three-wire | **CONFIRMED FROM DOCUMENTATION** — user selection |
| Fitted reference resistor | Nominal value and tolerance not recorded | **UNCONFIRMED** |
| SPI and connector pinout | MCU-side assignment is SPI2/GPIO12 SCK/GPIO11 MOSI/GPIO13 MISO/GPIO10 CS; module-side connector order and continuity are not recorded | Assignment **CONFIRMED FROM DOCUMENTATION** — maintainer report; module-side facts **UNCONFIRMED** |
| ESP32-S3 GPIO assignment | Final soldered production assignment is centralized in `max31865_board_pins.hpp`; the pins do not conflict with the recorded N16R8 PSRAM, strapping, native-USB, UART0, or JTAG restrictions | **CONFIRMED FROM CONFIG** for firmware intent and **CONFIRMED FROM DOCUMENTATION** for the maintainer-reported soldered assignment; no connected exercise |
| Boot-state behavior and safe power sequencing | Not tested | **UNCONFIRMED** |
| Bias/input-network settling | Module-specific RC/input network is unknown; no extra settling interval has been selected or tested | **UNCONFIRMED** |
| Accuracy/noise/fault behavior | No connected open/short, ambient, reference-temperature, sustained-run, or heater-noise test | **UNCONFIRMED** |

Primary software references:

- [ESP Component Registry: esp-idf-lib/max31865 1.0.8](https://components.espressif.com/components/esp-idf-lib/max31865/versions/1.0.8/readme)
- [Versioned upstream source](https://github.com/esp-idf-lib/max31865/tree/79566bd59420b03ab999c124f012a93a63f3a7db)
- [Analog Devices MAX31865 datasheet](https://www.analog.com/media/en/technical-documentation/data-sheets/max31865.pdf) — chip-level authority only; not a schematic for the unknown breakout

## `PROBES-001` — dual ADS1115 acquisition dossier

### Selection and software boundary

The user selected two ADS1115 converters on 2026-08-18. This fixes the ADC
type and quantity but does not identify either physical module or complete the
food-probe frontend. In particular, it does not define which probe/signal uses
each channel, the probe resistance curve, excitation/bias network, input
protection/filtering, gain, rate, or voltage-to-temperature calibration.

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

Two devices can share a bus only when their physical ADDR straps select two
different addresses from the ADS1115 set `0x48`, `0x49`, `0x4a`, and `0x4b`.
No address, I2C port, SDA/SCL GPIO, pull-up, speed, channel, gain, or sampling
choice is made by importing the driver.

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

### Physical facts still required before runtime activation or pin assignment

| Item | Current finding | Classification |
|---|---|---|
| Module manufacturer/product/revision/markings | Neither module is recorded | **UNCONFIRMED** |
| Procurement source and final-product status | Not recorded | **UNCONFIRMED** |
| Supply and logic-voltage behavior | Not recorded; chip ratings do not prove breakout behavior | **UNCONFIRMED** |
| ADDR straps / I2C addresses | Two distinct addresses required; neither physical strap is recorded | **UNCONFIRMED** |
| I2C bus | Port, SDA/SCL, pull-up rail/values, bus length, capacitance, and speed not selected | **UNCONFIRMED** |
| Signal/probe channel map | Purpose of each ADC and AIN0..AIN3 assignment not recorded | **UNCONFIRMED** |
| Analog frontend | Probe types, bias/excitation, source impedance, filtering, protection, and valid voltage range not recorded | **UNCONFIRMED** |
| Conversion policy | Single-ended/differential mode, gain, data rate, scheduling, and calibration not selected | **UNCONFIRMED** |
| ESP32-S3 GPIO assignment | None; must be checked against `CTRL-001` restrictions | **UNCONFIRMED** |
| Connected behavior | No address scan, known-voltage, accuracy, noise, disconnect/short, sustained-run, or heater-interference test | **UNCONFIRMED** |

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
| External application GPIO | No sensor, food-probe, SSR/heater, fan, smoke-source, display, or touch GPIO is assigned; production code has no `gpio_*`/`GPIO_NUM_*` use | **CONFIRMED FROM CONFIG** |
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
M6A restrictions. Probe and SSR assignments still require their corresponding
M6B interface facts. The MAX31865 assignment does not complete its remaining
module, supply, Rref, continuity, or connected-behavior gates.

## OTA/history partition note

M6A confirmed 16 MiB flash. M13 target-validated rollback with the preserved
24 KiB NVS range, `otadata`, `phy_init`, and two 3 MiB OTA slots. M14 retains
those offsets and assigns the next 4 MiB to the raw `history` log, leaving
`0x5e0000` bytes unallocated.

Changing either the former M12 single-app table or the M13 dual-slot table
requires the signed full-serial helper; application OTA cannot migrate a
partition table. M14 connected-target persistence/flash validation remains a
separate gate and is not sensor, SSR, thermal, or electrical-safety evidence.
