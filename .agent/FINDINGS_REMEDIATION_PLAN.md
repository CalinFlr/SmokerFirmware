# M12 Deep-Review Findings Remediation Plan

> Credential note (2026-08-17): D045 later reinstates `smoker257500` as the
> explicit product-required initial HTTP password. D046 later reinstates an
> open SoftAP with no key. The remaining HTTP hardening below stays in force.
> This historical plan is not authority to regenerate either credential.

Status: **Code remediation complete; physical radio/commissioning validation pending**

## Goal

Resolve the M12 deep-review findings covering SoftAP-to-STA provisioning,
authentication, control-path isolation, HTTP/JSON correctness, embedded UI
behavior, test fidelity, and firmware-size headroom.

## Scope

- Keep the current M12 simulated controller and approved three-layer design.
- Harden first-boot/local credentials without adding cloud or external services.
- Make Wi-Fi scanning recover within a bounded time and expose actionable STA
  failures.
- Keep the HTTP task outside the critical control loop while making command
  semantic results observable by the UI.
- Fix UI polling, focused probe rendering, input validation, CSP, and asset
  maintainability.
- Expand host/fixture/browser checks and re-run the ESP-IDF 6.0.2 cross-build.
- Increase the non-OTA factory-app partition headroom on the confirmed 16 MiB
  target without scaffolding M13 OTA.

## Non-goals

- Real sensors, SSR output, external safety hardware, or thermal validation.
- TLS PKI, cloud identity, BLE provisioning, display provisioning, or OTA.
- Claiming completion of the pending physical iPhone/radio/Wi-Fi-loss scenarios.

## Current repository observations

- Current milestone is M12 implemented for simulated I/O, with physical radio
  validation still pending.
- The worktree already contains the user's uncommitted M6A/M12 implementation;
  remediation must preserve and extend it in place.
- Host tests, sanitizers, browser fixture, and the v6.0.2 cross-build pass before
  remediation, but the browser fixture does not execute the ESP HTTP handler.
- The current binary is 956,176 bytes in a 1 MiB factory app partition, leaving
  92,400 bytes.

## Assumptions

- The KFB003 N16R8 target and 16 MiB flash readback documented by M6A remain
  authoritative.
- This historical remediation generated both credentials. D045 later fixes the
  initial HTTP password at `smoker257500`; only the generated WPA2 credential
  still uses physical serial commissioning and needs product labeling.
- WPA2/WPA3 Personal and open hidden networks are the supported M12 STA shapes;
  WEP, WPA1, and enterprise networks must be presented as unsupported.

## Steps

1. Replace 64-bit mailbox sequence atomics with target-native 32-bit atomics and
   add correlated semantic command results to immutable snapshots.
2. Add a bounded scan timeout, actionable disconnect status, supported-security
   policy, and resilient AP/STA transitions.
3. Historically replaced the shared documented default with generated
   credentials. D045 later reinstates only the fixed HTTP default; WPA2,
   throttling, exact-Origin enforcement, and JSON hardening remain unchanged.
4. Fix focused probe updates, serialize polling, validate inputs, surface
   semantic command rejection, align CSP/favicon behavior, and format the JS for
   maintainability.
5. Align the real policy helpers, HTTP fixture, host tests, and Playwright
   scenarios; select ESP-IDF's built-in 1500 KiB non-OTA factory partition and
   add a size gate.
6. Update architecture/decision/roadmap/traceability documentation and run a
   finding-by-finding completion audit.

## Validation commands

```text
./tools/verify.sh --host-only
./tools/check_m12_browser.sh
./tools/verify.sh --idf-only
idf.py size
git diff --check
```

A clean temporary-sdkconfig build will additionally verify that the committed
`sdkconfig.defaults` selects ESP-IDF's built-in large single-app table.

## Risks / unresolved items

- Captive portal HTTP cannot provide browser-trusted TLS without a separate
  certificate/provisioning design; WPA2 SoftAP protection removes the open-link
  exposure but LAN HTTP remains an explicitly documented M12 limitation.
- The generated WPA2 credential needs a product manufacturing/label workflow
  before consumer shipment; D045 explicitly accepts the shared HTTP default.
- ESP-IDF Wi-Fi event ordering and captive behavior still require the pending
  final-board scenarios after the host/build/browser gates pass.

## Completion audit

1. Open SoftAP: replaced by a generated WPA2 credential. The historical
   generated HTTP credential is superseded by D045's fixed `smoker257500`;
   claimed-password preservation and form/Basic throttling remain.
2. Wedged scan: bounded by a 15-second ESP timer and the ordinary failure
   recovery/reconnect path.
3. Unsupported Wi-Fi/security ambiguity: scan responses mark support; UI
   disables WEP/WPA1/Enterprise/OWE/DPP and validates OPEN versus Personal
   password input; disconnect status is actionable.
4. 64-bit mailbox atomics: read/write sequences are wrapping 32-bit counters;
   target archive audit finds no `__atomic_load_8` or `__atomic_store_8`.
5. Missing Origin with cookies: protected cookie writes now require an explicit
   exact Origin; Basic clients may omit Origin but never supply a foreign one.
6. Fixture drift: production-used pure policies have host tests; fixture now
   matches auth-before-Origin, CSP, media types, rate limiting, command IDs,
   Wi-Fi support fields, and delayed snapshot behavior.
7. Focused probe freeze: keyed rows update live readings without replacing the
   focused target input.
8. Overlapping polling: completion-scheduled polling plus client request
   timeout; Playwright observes one maximum delayed snapshot request in flight.
9. HTTP `202` ambiguity: correlation IDs and bounded immutable semantic results
   distinguish admission from application acceptance/rejection.
10. Firmware margin: ESP-IDF's built-in 1500 KiB single-app slot plus a 75%
    build gate; latest verified image is 973,088 bytes (63.4%).
11. CSP/favicon: dashboard uses a data favicon and no inline style attribute;
    browser console validation is clean under the production CSP.
12. Partial cJSON success: every required container/item allocation is checked
    and any construction failure returns `503` instead of a partial `200`.
