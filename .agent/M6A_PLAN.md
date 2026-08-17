# M6A Controller-Board Identification and Validation Plan

Status: **Complete**

## Goal

Identify the connected ESP32-S3 controller board with evidence and validate the
existing simulated firmware runtime on that exact target, without making claims
about unavailable external sensing, heater, or safety hardware.

## Scope

- collect host-visible USB and ESP32-S3 chip/storage evidence;
- record the exact board/module identity and product/development status once
  visually confirmed;
- verify flash and PSRAM configuration against detected hardware;
- document integrated USB/debug and radio capabilities from primary sources;
- inventory exposed GPIO and boot/strapping restrictions from the identified
  board/module documentation;
- build, flash, and observe the current simulation image on the connected board;
- exercise ControlTask scheduling, stack watermark, task-watchdog reset, and
  reset-cause behavior with explicit target-runtime evidence;
- update M6A documentation and traceability without closing unverified items.

## Non-goals

- no external sensor, food-probe, SSR, heater-power, or independent-protection
  identification or validation (M6B);
- no external GPIO assignments;
- no real sensor/heater drivers, Wi-Fi/BLE features, UI, persistence, or OTA;
- no claim that development-board results apply to a future product board;
- no manual edits to generated `sdkconfig`.

## Current repository observations

- M0-M5 are complete as a host-tested simulated application/control slice;
- before download mode, the connected host exposed an application-mode
  Espressif USB device at `/dev/cu.usbmodem1234561` (VID `0x303a`, PID
  `0x4001`), consistent with an Espressif TinyUSB device descriptor but not
  sufficient to identify the development-board model; ROM download mode later
  enumerated separately as USB Serial/JTAG (VID `0x303a`, PID `0x1001`);
- the image already logs the ControlTask stack high-water mark and subscribes
  the single ControlTask to the five-second panic/reset TWDT;
- the user identifies the hardware as `ESP32-S3-N16R8`; Espressif's module
  ordering table maps `ESP32-S3-WROOM-1-N16R8` to 16 MiB Quad SPI flash and
  8 MiB Octal SPI PSRAM;
- the user-provided procurement page and photographs identify the carrier as
  SuooTci code `KFB003`, and the user confirms it is the final controller;
- local M6A/M6B documentation-split changes predate this plan and must be
  preserved.

## Assumptions

- `/dev/cu.usbmodem1234561` is the controller board the user intends to validate;
- read-only chip queries and flashing the repository's existing simulation
  firmware are within M6A scope;
- model/module markings and product intent require visual confirmation and will
  remain explicitly pending if they cannot be established electronically;
- any destructive watchdog diagnostic will be isolated, disclosed, and leave
  the board reflashed with the normal simulation image.

## Steps

1. Query the connected chip, flash, security/revision information, and current
   serial output; separate electronically observed facts from inference.
2. Obtain the exact board and module markings plus product/development status;
   correlate them with the manufacturer's primary documentation.
3. Add only the minimal target diagnostics needed to log reset cause,
   scheduling/core behavior, and reliable stack-watermark evidence.
4. Build and flash the normal v6.0.2 ESP32-S3 image; capture boot, task,
   watchdog-subscription, and stack results.
5. Run an explicit task-watchdog stall diagnostic, capture panic/reset and
   reset-cause evidence, then restore and revalidate the normal image.
6. Update `docs/HARDWARE.md`, roadmap/traceability status, README instructions,
   and decisions only for evidence actually obtained.
7. Run repository guardrails, host tests, target build checks, and patch review.

## Validation commands

```text
esptool.py --port /dev/cu.usbmodem1234561 chip-id
esptool.py --port /dev/cu.usbmodem1234561 flash-id
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/cu.usbmodem1234561 flash monitor
python3 tools/check_architecture.py
python3 tools/check_traceability.py
tools/verify.sh --host-only
python3 tools/check_target_compile_commands.py build/compile_commands.json
git diff --check
```

## Risks / unresolved items

- USB descriptors identify the Espressif interface, not the exact carrier-board
  or module marking;
- flash/PSRAM probing can identify silicon/storage behavior but must still be
  reconciled with the board/module documentation and build configuration;
- watchdog validation intentionally resets the target and must never be
  mistaken for external heater/electrical safety validation;
- an identified development board may not be the final product controller, so
  any partition decision can remain provisional.

## Progress

- target queries confirmed ESP32-S3 revision v0.2, dual cores, Wi-Fi/BT 5 LE,
  16 MiB Quad flash at 3.3 V, 8 MiB PSRAM, and native USB Serial/JTAG;
- the normal N16R8 image was written and hash-verified through native USB;
- after BOOT was released, normal application boot and sustained runtime through
  cycle 420 were captured; ControlTask ran on core 0 at priority 2 with 10,868
  of 12,288 stack bytes minimum free from cycle 60 onward;
- initial runtime exposed and corrected a zero-tick period conversion defect;
- a temporary seven-second stall confirmed the five-second TWDT panic/reset and
  watchdog reset reason, after which the diagnostic was removed and the normal
  image was rebuilt, reflashed, and revalidated;
- the SuooTci product photographs document the two USB roles, CH343P bridge,
  buttons/LEDs, and 44 header positions; their “34 pins” title discrepancy is
  recorded and the photographed silkscreen is reconciled with N16R8 restrictions;
- M6A is complete; external-interface hardware and final functional pin
  assignments remain M6B scope.
