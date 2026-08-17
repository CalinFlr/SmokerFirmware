# Fixed Initial HTTP Password Plan

> Superseding link-layer note (2026-08-17): D046 explicitly requires an open
> SoftAP with no Wi-Fi key. WPA2 references below describe the historical D045
> implementation and must not be treated as current requirements.

## Goal

Restore `smoker257500` as the product-required initial HTTP login password while
retaining the separate, unique WPA2 SoftAP password and the existing login,
session, rate-limit, Host, Origin, and safety boundaries.

## Scope

- M12 NVS initialization and migration for the HTTP device password;
- regression guardrails and existing HTTP/browser fixtures;
- README, architecture, decision log, roadmap, and traceability wording;
- ESP-IDF build and target commissioning validation on the connected KFB003.

## Non-goals

- do not make the WPA2 SoftAP password fixed or reopen the AP;
- do not weaken login rate limiting, cookie attributes, Host/Origin checks, or
  protected-route authentication;
- do not change cooking/control, heater, sensor, persistence, or OTA behavior.

## Current repository observations

- M12 currently generates separate random HTTP and SoftAP credentials;
- D043 supersedes the original shared HTTP default from D037;
- the browser/HTTP fixtures already use `smoker257500`, but production NVS
  initialization migrates that value away;
- before this change, the target ran the consumer-hardening build with a unique
  WPA2 AP and a generated initial HTTP password.

## Assumptions

- “parola inițială” means the HTTP form/Basic password, not the WPA2 key;
- the fixed shared HTTP password is an explicit product-owner decision despite
  its documented local-access risk;
- an optional password supplied during provisioning still marks the device
  claimed and replaces the initial value.

## Steps

1. Make empty/invalid or still-unclaimed HTTP credentials resolve to
   `smoker257500`; preserve claimed custom passwords.
2. Retain unique random generation for the WPA2 SoftAP key.
3. Update source guardrails and documentation with a new decision that
   supersedes only D043's generated-HTTP portion.
4. Run host, fixture/browser, ESP-IDF, size, and diff validation.
5. Flash the verified image, erase only NVS for a new-device scenario, and
   confirm the fixed HTTP password plus unique WPA2 AP on serial/HTTP.

## Validation commands

```text
tools/verify.sh --host-only
tools/check_m12_browser.sh
tools/verify.sh --idf-only
git diff --check
idf.py -B build-verify -p <UART port> flash
esptool ... erase-region 0x9000 0x6000
idf.py -B build-verify -p <UART port> monitor
```

## Risks / unresolved items

- every device shares a publicly documented initial HTTP password until it is
  replaced, so a nearby AP client or same-LAN client can authenticate;
- M12 HTTP and NVS remain unencrypted;
- production labeling is still needed for the unique WPA2 SoftAP key;
- physical validation remains simulated-I/O/radio evidence, not hardware-safety
  evidence.

## Completion report — 2026-08-17

- production NVS initialization now uses `smoker257500` for an empty, invalid,
  or still-unclaimed HTTP credential and preserves claimed custom passwords;
- the WPA2 SoftAP key remains separate and uniquely generated;
- D045, README, architecture, roadmap, traceability, historical-plan notes, and
  an executable architecture guardrail record the fixed product requirement;
- host tests, ASan/UBSan, HTTP fixture, Playwright browser flow, traceability,
  strict ESP-IDF 6.0.2/C++20 build, 75% size guard, and diff checks pass;
- the 973,744-byte image was SHA-verified on the KFB003; both preserved-NVS
  migration and an erased-NVS factory boot reported `smoker257500`, WPA2 AP,
  DHCP/captive DNS, `IDLE`, and simulated heater `0%`;
- phone/iPhone captive opening, actual target HTTP login, STA selection, and
  Wi-Fi-loss scenarios remain pending and are not hardware-safety evidence.
