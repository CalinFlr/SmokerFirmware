# M12 Local Code-Review Findings Fix Plan

Status: **Complete — verified and redeployed with NVS preserved**

## Goal

Resolve the three local-review findings in M12 and redeploy the verified image
to the connected KFB003 while preserving the existing NVS configuration.

## Scope

- normalize IPv4-mapped peers before per-client login throttling;
- make every HTTP error response allocation-free, including cJSON OOM paths;
- persist Wi-Fi credentials and device-authentication state as separate atomic,
  versioned blobs, migrating the current legacy keys without erasing NVS;
- add focused host/source regression coverage;
- run host/sanitizer and ESP-IDF 6.0.2 validation, flash, and inspect boot logs.

## Non-goals

- no changes to D045's fixed initial HTTP password or D046's open SoftAP;
- no M6B-M10 hardware, sensing, heater, recovery, or OTA work;
- no NVS erase and no hardware-safety claim.

## Current repository observations

- M12 is implemented in the existing uncommitted worktree;
- the pinned target configuration enables IPv6 and ESP HTTPD therefore uses a
  dual-stack IPv6 listener;
- ESP-IDF 6.0.2 writes values during `nvs_set_*`; `nvs_commit()` is a no-op, so
  sequential keys do not form a transaction;
- the target build disables C++ exceptions, so failed throwing allocation
  aborts/reboots;
- the previous host/sanitizer/target build gates pass before remediation.

## Assumptions

- legacy `sta_ssid`, `sta_pass`, `dev_pass`, and `dev_claimed` values may exist
  on the connected target and must migrate automatically;
- one NVS blob per independent persistence concern provides the required atomic
  update without coupling Wi-Fi and HTTP credential lifecycles;
- the deployment port will be discovered from the connected USB device.

## Steps

1. Add peer-address and allocation-free HTTP error-body helpers plus host tests.
2. Use those helpers in the ESP HTTP handler.
3. Add versioned Wi-Fi/auth blobs and legacy migration; update each concern with
   one `nvs_set_blob` operation.
4. Extend executable guardrails for the target-only integration points.
5. Run host/sanitizer, target build/size checks, flash without erasing NVS, and
   inspect boot/connectivity/control logs.

## Validation commands

```text
tools/verify.sh --host-only
tools/verify.sh --idf-only
git diff --check
idf.py -B build-verify -p <detected-port> flash monitor
```

## Risks / unresolved items

- migration and target boot can prove compatibility with the attached board's
  current NVS, but forced flash-write failure injection is not part of deploy;
- redeploy validates simulated I/O and connectivity startup only, not real SSR,
  sensor, thermal, or independent electrical safety.

## Completion report — 2026-08-17

- normalized native and mapped IPv4 HTTP peers before the fixed per-peer login
  limiter;
- replaced allocating error-envelope construction with a tested, bounded,
  allocation-free builder used by cJSON OOM paths;
- replaced multi-key writes with separate versioned Wi-Fi/auth NVS blobs and
  retained legacy-key boot migration without erase;
- added D048, executable source guardrails, host allocation coverage, and
  documentation/traceability evidence;
- complete host, ASan/UBSan, HTTP fixture, architecture/traceability, strict
  ESP-IDF 6.0.2 C++20 build, and 75% image-size gates passed;
- flashed and hash-verified the 983,440-byte image on
  `[REDACTED_UART_PORT]` without erasing NVS;
- target boot retained `[REDACTED_STA_SSID]`, reached `[REDACTED_STA_IP]`, remained
  `IDLE`/heater `0%`, served `/login` as `200`, rejected an unauthenticated
  snapshot as `401`, and exposed both new blob keys in a read-only NVS audit.
