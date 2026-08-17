# M12 Local Review Findings Remediation Plan — D049

Status: **Code remediation complete; physical failure injection remains pending**

## Goal

Resolve the four findings from the current local M12 code review without
changing the approved D045 fixed initial HTTP password or D046 open SoftAP
product decisions.

## Scope

- fail closed when the commissioning AP overlaps a connected STA;
- make legacy authentication migration reject NVS read/corruption failures
  instead of replacing a claimed password with the fixed initial password;
- ensure no fallible JSON construction occurs after a control command is
  admitted to the cross-task mailbox;
- make coalesced application Stop correlation IDs inherit the processed Stop's
  actual semantic result;
- add host tests, source guardrails, documentation, and ESP-IDF validation.

## Non-goals

- changing the intentionally open commissioning AP or `smoker257500` default;
- real sensor, SSR, electrical, thermal, recovery, OTA, or cloud work;
- claiming physical radio or hardware-safety validation.

## Current repository observations

- M12 is implemented for simulated I/O; physical radio/iPhone scenarios remain
  pending.
- The worktree contains the user's uncommitted M12 implementation and earlier
  plans; remediation must extend it in place without discarding those changes.
- The pre-remediation host, sanitizer, guardrail, fixture, and ESP-IDF v6.0.2
  build gates pass.
- ESP-IDF's lwIP accepts a packet on another configured local netif when its
  destination does not match the ingress netif. Local socket address alone is
  therefore not an ingress-interface proof while AP and connected STA overlap.

## Assumptions

- Operational HTTP may be temporarily unavailable while SoftAP is still marked
  active; fail-closed behavior is preferred to exposing control through the
  open commissioning network.
- A truly fresh/unclaimed legacy NVS state may initialize the approved fixed
  password; read errors, corrupt values, and a claimed marker without a valid
  password must fail connectivity initialization.
- Network send failure after mailbox admission remains inherently ambiguous,
  but local allocation/serialization failure after admission is avoidable.
- Correlated direct application Stop coalescing remains bounded by the existing
  16-result snapshot history; the newest coalesced IDs are retained.

## Steps

1. Extend the pure HTTP-scope policy with AP-active state and deny operational
   scope while commissioning is active.
2. Add a pure legacy-authentication migration decision and use it to reject
   invalid/error states before writing the new authoritative blob.
3. Build the command-admission JSON in fixed storage before mailbox admission
   and send it without cJSON allocation afterward.
4. Retain bounded coalesced Stop IDs on the queued command and resolve them with
   the original Stop's semantic result during `tick()`.
5. Add host regression tests and executable source guardrails; update D049,
   architecture, data-model, roadmap, and traceability evidence.
6. Run host/sanitizer, browser fixture, strict ESP-IDF 6.0.2 build/size checks,
   diff checks, and a finding-by-finding completion audit.

## Validation commands

```text
./tools/verify.sh --host-only
./tools/check_m12_browser.sh
./tools/verify.sh --idf-only
git diff --check
```

## Risks / unresolved items

- The AP/STA overlap regression is host-policy/source validated; final-board
  failure injection for a failed AP-disable transition remains physical M12
  validation work.
- Forced NVS read/write failure injection remains target-pending.
- No result here is evidence of real sensor, SSR, thermal, electrical, or
  independent hardware-safety behavior.

## Completion report — 2026-08-17

- operational scope now rejects every request while the commissioning AP is
  active, including a request addressed to the current STA IPv4;
- legacy authentication migration preserves valid claimed state, initializes
  only missing/unclaimed state, and rejects read/corruption or claimed-without-
  password state before writing the authoritative blob;
- the fixed command-admission JSON is built without allocation before mailbox
  publication, removing the local post-admission cJSON `503` path;
- correlated coalesced application Stops inherit the original Stop's processed
  accepted/rejected result;
- host tests, ASan/UBSan, HTTP fixture, Playwright AP/STA flow, architecture and
  traceability guardrails, strict ESP-IDF 6.0.2 C++20 build, and the firmware
  size gate pass;
- the resulting image is 985,408 bytes of the 1,536,000-byte app partition
  (64.2%, 550,592 bytes free);
- final-board AP-disable and NVS failure injection remain target-pending, and no
  sensor/SSR/electrical/thermal safety claim is made.
