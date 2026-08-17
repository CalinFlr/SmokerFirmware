# Hardware and Component Inventory

Status: **M6A complete; M6B external hardware incomplete**

Last evidence update: **2026-08-17**

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
- External sensor/probe boards and the SSR interface are not yet available.
- The project will eventually control an electric smoker heater.
- Real chamber sensing, food-probe integration, SSR wiring, independent thermal/electrical protection, current sensing, fan control, and smoke-source integration are separate hardware milestones.

## Component register

| ID | Product role | Selected component | Current classification | Integration status |
|---|---|---|---|---|
| `CTRL-001` | Main controller | SuooTci `KFB003` / eMAG `D1T7M22BM`; N16R8 variant reported | Carrier identity **CONFIRMED FROM DOCUMENTATION**; SoC/storage/USB **CONFIRMED FROM HARDWARE** | M6A complete; simulated I/O only |
| `CHAMBER-001` | Authoritative chamber sensor/frontend | Not selected | **UNCONFIRMED** | Blocked at M6B |
| `PROBES-001` | Food-probe frontend | Not selected | **UNCONFIRMED** | Blocked at M6B |
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

Use simulated sensor/probe/heater adapters until the corresponding M6B hardware
is confirmed. M6A may exercise only the controller board and its integrated
capabilities; this does not validate external control hardware.

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

Final sensor/probe/SSR pin assignments require both the M6A pin restrictions
and the corresponding M6B interface facts.

## OTA partition note

Do not create a final custom OTA `partitions.csv` before M13 designs and
validates rollback behavior. M6A has confirmed the 16 MiB target capacity. M12
uses ESP-IDF's built-in 1500 KiB single-app layout to restore firmware growth
margin; this is not an OTA-capable product layout.

The final product partition layout must be OTA/rollback-capable. A provisional
layout used on a development board must be labeled as such and revalidated for
the final product module.
