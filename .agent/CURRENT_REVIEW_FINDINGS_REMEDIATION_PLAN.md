# M12 Current Review Findings Remediation Plan

Status: **Completed**

## Goal

Resolve the three findings from the current local review: converge after a
transient STA configuration-application failure, discard UI target drafts when
the application rejects them, and log the authoritative active chamber target.

## Scope

- retry the complete saved STA configuration before reconnecting when driver
  mode/configuration application failed;
- preserve newer user edits while removing only the rejected submitted draft;
- derive runtime chamber/target/heater diagnostics from one application
  snapshot;
- add focused host/browser/source regression coverage.

## Non-goals

- no changes to the open commissioning AP or fixed initial password decisions;
- no M6B-M10 hardware, sensor, SSR, recovery, or OTA work;
- no claim of physical Wi-Fi, electrical, or thermal validation.

## Current repository observations

- M12 is implemented locally for simulated I/O and the existing host/build
  gates pass;
- the STA retry timer currently calls only `esp_wifi_connect()`, even when the
  preceding `esp_wifi_set_mode()` or `esp_wifi_set_config()` failed;
- rejected chamber/probe target commands leave the corresponding browser draft
  marked dirty;
- the runtime diagnostic target is an initialization-only scalar rather than
  the application's active session target.

## Assumptions

- the existing SoftAP recovery remains available while STA application is
  retried;
- only the current submitted draft may be cleared by its semantic result; a
  newer edit must be retained;
- snapshot-based logging is simulation evidence only.

## Steps

1. Track whether the full STA driver configuration must be retried and route
   the retry callback to configuration application or connect accordingly.
2. Add host/source coverage for the retry-state contract.
3. Make chamber and probe target submissions retain identity and clear only a
   rejected current draft; extend the browser scenario.
4. Log chamber, target, and demand from the same post-tick snapshot and include
   target changes in the log trigger.
5. Run host, sanitizer, browser, ESP-IDF, and diff validation.

## Validation commands

```text
./tools/verify.sh --host-only
./tools/check_m12_browser.sh
./tools/verify.sh --idf-only
git diff --check
```

## Risks / unresolved items

- ESP-IDF Wi-Fi failure injection remains a physical target scenario; host
  coverage validates the retry state and source integration, while the target
  build validates API compatibility;
- no validation here exercises real sensing, SSR output, or independent safety
  hardware.

## Completion report

- STA retries now distinguish a connect failure from a driver
  mode/configuration-application failure; the latter reapplies the complete
  saved configuration before reconnecting.
- Rejected chamber/probe target submissions discard only the submitted draft,
  while preserving any newer edit and allowing snapshots to restore the
  authoritative value.
- runtime diagnostics publish and log from one post-tick application snapshot
  and emit again when the authoritative chamber target changes.
- host tests, ASan/UBSan, the real-browser M12 contract, the ESP-IDF 6.0.2
  ESP32-S3 build, architecture/traceability gates, firmware-size gate, and
  \`git diff --check\` pass.
