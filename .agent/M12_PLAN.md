# M12 Wi-Fi and Local API/UI Plan

> Credential note (2026-08-17): D045 supersedes this plan's generated initial
> HTTP credential with the fixed product default `smoker257500`. D046 then
> supersedes its generated WPA2 credential with an open SoftAP. All remaining
> HTTP authentication controls stay in force.

Status: **Implementation complete; final iPhone/radio scenarios pending**

## Goal

Add local Wi-Fi provisioning, automatic captive-portal discovery, 2.4 GHz
network scanning, and an authenticated Romanian Fumuri web/API interface to the
existing simulated controller without making connectivity part of the critical
heater-control dependency chain.

## Scope

- keep the chamber, food probe, and heater adapters simulated;
- boot in `IDLE` with heater OFF and require an explicit API/UI Start;
- add STA with persisted credentials, generated WPA2 SoftAP fallback, and mDNS;
- add an authenticated HTTP API and embedded responsive UI;
- adapt the visual language and canonical color tokens from the read-only
  `/Users/floreacalin/Developer/Fumuri` reference without importing its runtime;
- scan visible 2.4 GHz networks asynchronously, deduplicate/sanitize/limit the
  result, and keep permanent manual SSID entry;
- advertise `http://192.168.4.1/` through DHCP option 114 and redirect captive
  probes to an absolute, data-free `/login` form through a wildcard DNS
  responder active only with SoftAP;
- transport commands to the sole-owner `ControlTask` through a bounded SPSC
  mailbox with reserved Stop admission;
- publish preallocated snapshots through a non-blocking triple exchange;
- pin `ControlTask` to core 1 and connectivity/HTTP work to core 0;
- update M11/M12 roadmap status, architecture, decisions, safety evidence, and
  traceability.

## Non-goals

- no M7-M10 real sensing, SSR, food-probe, session persistence, or recovery;
- no display implementation (M11 is postponed because no display was
  purchased);
- no OTA partitions or update behavior (M13);
- no React, Next.js, Tailwind, npm runtime, WebSocket, SSE, cloud, CORS, CDN, or
  future V0 features;
- no claim that browser/build/simulation checks validate electrical or thermal
  safety.

## Current repository observations

- M0-M5 are complete as a simulated slice and M6A is complete for the final
  SuooTci KFB003 N16R8 board;
- M6A documentation and target-diagnostic changes are present as uncommitted
  user work and must be preserved;
- `SmokerApplication::submit()` and mutable runtime state are owned by the one
  `ControlTask`; its queue already provides a 15+reserved-Stop contract;
- the current target runtime automatically queues Start, runs `ControlTask`
  unpinned, and has no network/component-registry dependencies;
- M6B and real external hardware remain unavailable and outside M12.
- the original M12 UI and manual SSID form existed, but Wi-Fi scan APIs,
  captive DNS/DHCP discovery, and the full Fumuri visual adaptation did not.

## Assumptions

- the ESP-IDF HTTP server has one request-task producer, allowing an SPSC
  command mailbox;
- a missing saved STA configuration should expose the provisioning SoftAP
  immediately, while a failed/disconnected configured STA gets the specified
  30-second grace period;
- the built-in single-stage recipe has no timer and receives its optional
  chamber target from each explicit Start request;
- form/session and HTTP Basic without TLS plus unencrypted dedicated-NVS
  credential storage are accepted M12 local-network tradeoffs and will be
  disclosed prominently;
- SoftAP credentials are generated and persisted; D045 fixes the initial HTTP
  password at `smoker257500` while preserving owner replacement;
- physical target and Wi-Fi validation require the connected final board and
  user-provided local STA credentials.
- browsers cannot enumerate iPhone Wi-Fi networks or saved passwords; all
  network choices come from ESP32-S3 2.4 GHz scan results.

## Steps

1. Add allocation-free snapshot views, the bounded SPSC mailbox, and a
   preallocated triple snapshot exchange with host tests.
2. Integrate mailbox draining and snapshot publication into `ControlTask`,
   remove automatic Start, and pin it to core 1.
3. Add dedicated-NVS network configuration, STA/WPA2-SoftAP fallback, mDNS,
   password-only CNA-compatible form/session authentication with a show/hide
   control plus Basic API compatibility,
   strict JSON routes, and embedded UI on core 0.
4. Add asynchronous/coalesced Wi-Fi scan routes and preallocated result
   curation; serialize scan with STA reconnect and resume it afterward.
5. Add DHCP option 114, captive-probe redirects, and the ESP-IDF-example-derived
   static 4 KiB DNS responder pinned to core 0.
6. Replace the embedded page with the mobile-first Fumuri dashboard and
   provisioning-first layout, system/dark/light themes, accessible focus and
   iPhone-safe 16 px inputs.
7. Add component pins/config defaults and update documentation/guardrails for
   the M12 ownership, portal, UI, and security contracts.
8. Run host tests/sanitizers, repository guardrails, strict target build, real
   browser UI checks, and any available target/radio validation.

## Validation commands

```text
tools/verify.sh --host-only
tools/verify.sh --idf-only
python3 tools/check_architecture.py
python3 tools/check_traceability.py
python3 tools/check_target_compile_commands.py build/compile_commands.json
python3 tools/check_m12_http_fixture.py
PWCLI=/path/to/playwright_cli.sh tools/check_m12_browser.sh
git diff --check
playwright-cli (through the repository-independent Playwright skill workflow)
idf.py -p /dev/cu.usbmodem1234561 flash monitor
```

## Validation results — 2026-08-17

- complete host and sanitizer suite: pass;
- architecture and traceability guardrails: pass;
- ESP-IDF 6.0.2 target build and strict project C++20 check: pass;
- Playwright desktop/iPhone-width checks: pass for system/light/dark theme,
  persistence, provisioning priority, automatic/refresh/error scan flows,
  selected/manual SSID, IDLE/RUNNING/STOPPED, 16 px inputs, 44 px targets,
  zoom-preserving viewport, and same-origin-only requests;
- versioned HTTP fixture checks: pass for absolute CNA redirect, password-only
  HTML login failure, CNA-Origin login, random-session contract, Basic
  compatibility, protected-route same-origin rejection,
  password-change invalidation, and replacement password;
- versioned Playwright checks: pass for password-only login rendering,
  Arată/Ascunde behavior, HttpOnly/SameSite=Lax cookie flags and a real
  cross-site CNA-style form redirect, no external resources,
  and focused chamber/probe drafts retained through multiple polling intervals;
- host transition coverage proves repeated STA failures do not restart the
  30-second recovery deadline and a `GOT_IP` interleaving revokes fallback AP
  activation; Wi-Fi mode operations are serialized and retry STA-only;
- flashed `[REDACTED_NATIVE_USB_PORT]` with SHA verification, preserving NVS;
- target boot observed `ControlTask` on core 1 in `IDLE` with heater `0.0%`,
  Wi-Fi task on core 0, open `Smoker-[REDACTED_MAC6]` at `192.168.4.1`, form/session plus
  Basic HTTP startup, and captive DNS active on SoftAP core 0;
- a later preserved-NVS boot and live authenticated API read observed saved STA
  `[REDACTED_STA_SSID]` at `[REDACTED_STA_IP]` with SoftAP inactive;
- iPhone captive opening, real network selection/provisioning, hidden SSID,
  wrong-password retry, mDNS persistence, and Wi-Fi-loss isolation during
  RUNNING remain manual final-board scenarios.

## Risks / unresolved items

- form/Basic credentials and the bearer session cookie are observable on the
  local link because M12 has no TLS; NVS storage is also unencrypted;
- radio provisioning, STA reconnect/fallback timing, mDNS, core affinity,
  watermark, TWDT, and a ten-minute run need physical target evidence;
- changing the device password affects HTTP authentication only and does not
  silently rotate the separately persisted SoftAP credential;
- semantic command validation remains asynchronous in `SmokerApplication`;
  HTTP `202` proves transport admission, while correlated snapshot results let
  the UI observe the eventual accept/reject outcome;
- snapshot publication intentionally drops an update rather than waiting when
  both non-current buffers are leased.
- the ESP-IDF Wi-Fi driver retains its documented internal scan allocation;
  firmware-owned raw and curated result buffers are fixed/preallocated;
- captive DNS/DHCP failure degrades to authenticated manual access at
  `192.168.4.1` and is not a control-loop failure.
