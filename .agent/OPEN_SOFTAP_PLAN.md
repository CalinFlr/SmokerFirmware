# Open SoftAP Product-Decision Plan

## Goal

Make `Smoker-<MAC6>` an intentionally open commissioning network while keeping
the fixed HTTP login password `smoker257500` and all HTTP authorization,
rate-limit, Host/Origin, parsing, and control-safety boundaries.

## Scope

- M12 ESP-IDF SoftAP configuration and obsolete AP-key storage;
- executable guardrails, documentation, and superseding decision D046;
- host/browser/cross-build validation and KFB003 factory boot.

## Non-goals

- do not remove HTTP authentication;
- do not weaken session cookies, per-peer rate limiting, Host/Origin checks, or
  protected routes;
- do not change STA security support or control/heater behavior.

## Current repository observations

- D043 introduced a unique WPA2 key; D045 later restored the fixed HTTP login;
- the product owner now explicitly requires the AP itself to have no key;
- NVS currently contains no STA credentials and the board is in commissioning
  mode.

## Assumptions

- “open AP” means ESP-IDF `WIFI_AUTH_OPEN`, with an empty AP password field;
- `smoker257500` remains required at the public, data-free HTTP login before
  dashboard/API access;
- the increased local radio exposure is an accepted product tradeoff.

## Steps

1. Remove AP password generation, NVS storage, serial disclosure, and WPA2 AP
   configuration; retain STA WPA2/WPA3 handling.
2. Add D046 and update all current/historical documentation and guardrails.
3. Run host, sanitizer, HTTP fixture, Playwright, ESP-IDF, size, and diff checks.
4. Flash the verified image, erase only NVS, and observe an open AP plus fixed
   HTTP login on a clean target boot.

## Validation commands

```text
tools/verify.sh --host-only
PWCLI=/path/to/playwright_cli.sh tools/check_m12_browser.sh
tools/verify.sh --idf-only
git diff --check
idf.py -B build-verify -p <UART port> flash
esptool ... erase-region 0x9000 0x6000
idf.py -B build-verify -p <UART port> monitor
```

## Risks / unresolved items

- anyone in radio range can associate with the provisioning AP and attempt the
  shared HTTP password; rate limiting reduces guessing but not knowledge of the
  documented default;
- traffic remains plaintext and can be observed or modified by an associated
  attacker; HTTP authentication is not link encryption;
- phone captive opening and a full AP-to-STA provisioning run remain separate
  target scenarios;
- simulated heater OFF evidence is not hardware-safety validation.

## Completion report — 2026-08-17

- removed SoftAP password generation, NVS persistence, serial disclosure, and
  WPA2 AP configuration; STA security behavior is unchanged;
- D046, current documentation, and executable architecture guardrails now
  require `WIFI_AUTH_OPEN` and reject an AP password field or credential
  generator;
- host tests, ASan/UBSan, HTTP fixture, Playwright browser validation,
  architecture/traceability checks, ESP-IDF 6.0.2 build, firmware-size gate,
  and `git diff --check` passed;
- the 973,088-byte image with SHA-256
  `a61ab8bf09ced2ae70f8e46ad44c3b6d0d6eafdffa702d4c650dcfd4a76ea5af`
  was flashed and verified on the KFB003;
- only NVS (`0x9000`, `0x6000`) was erased, and its readback contained zero
  bytes other than `0xFF`;
- the clean target boot reported open `Smoker-[REDACTED_MAC6]`, fixed HTTP credentials
  `admin` / `smoker257500`, DHCP and captive DNS, ControlTask on core 1, network
  work on core 0, and simulated heater `0%`.

The phone captive-opening, completed login, and AP-to-STA provisioning scenario
remain pending manual target validation. No hardware-safety claim is made.
