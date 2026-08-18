# M15 Personal Blynk Remote Access Plan

Status: specified; implementation has not started.

## Goal

Give one owner private remote Start/Stop/configuration, live status, Blynk push
events, and user-requested M13 OTA for one home smoker, with no custom backend,
domain, mobile application, or cloud-history synchronization.

## Scope

- Blynk's existing mobile application and Device MQTT API over TLS.
- Official ESP-MQTT component isolated in `smoker_platform`.
- Existing immutable snapshots, bounded command mailbox, semantic command
  results, and M13 OTA service.
- One complete change-driven `batch_ds` status projection: initial on connect,
  then only on normalized change, never more often than once per five seconds.
- Immediate bounded command results and configured critical Blynk events.
- One non-versioned per-device token and the Blynk-provided regional endpoint.

## Non-goals

- A custom backend, domain, database, mobile application, or MQTT broker.
- Multi-user accounts, sharing, fleet management, subscriptions, or product
  provisioning.
- Arbitrary remote heater commands, arbitrary OTA URLs, Blynk.Air packaging, or
  automatic OTA installation.
- Upload/backfill of M14 raw history or cloud-owned runtime state.
- Any real-sensor, SSR, thermal, or independent electrical-safety claim.

## Current repository observations

- M12 already supplies the cross-task command mailbox, immutable snapshot
  exchange, correlated results, Wi-Fi, and local authenticated API/UI.
- M13 already installs the fixed latest public GitHub Release asset with
  application permission, ESP32-S3 signed-image verification, rollback, and
  pending-image validation.
- M14 already owns durable local history and shares flash with OTA through a
  platform coordinator; M15 must not turn that history into a cloud dependency.
- The worktree contains the user's uncommitted M14 implementation and evidence;
  M15 work must preserve it.
- No MQTT component or Blynk credential is currently part of the project.

## Assumptions

- The owner accepts Blynk account authentication in the existing Blynk app.
- Exactly one Blynk device/template is required initially.
- The Blynk-provided regional endpoint and device token will be available during
  implementation; they are configuration/secrets, not source constants.
- Blynk Free-plan availability and quotas are auxiliary and may change without
  changing local controller behavior.

## Steps

1. Freeze a bounded Blynk template/datastream/event mapping for the existing
   external commands, immutable status, firmware status, and five free event
   categories.
2. Add and pin the official ESP-MQTT component, then implement a platform-only
   TLS connection with bounded buffers, reconnect/backoff, and secret-safe logs.
3. Implement the fixed-size normalized remote projection, connect snapshot,
   dirty comparison, five-second minimum publish interval, newest-value
   coalescing, and silence while unchanged.
4. Translate only allowlisted live Blynk controls into the existing mailbox;
   preserve reserved Stop behavior and correlated semantic results. Never sync
   Start or OTA-install controls after reconnect.
5. Publish bounded critical events/results independently from status. Route the
   firmware control into the existing M13 fixed GitHub check/install path.
6. Add host, concurrency, architecture, build, browser/app-contract where
   practical, and connected-KFB003 evidence required by `docs/ROADMAP.md`.
7. Update source-of-truth and traceability from specified to implemented only
   after every corresponding evidence gate passes.

## Validation commands

```text
python3 tools/check_traceability.py
python3 tools/check_architecture.py
tools/verify.sh --host-only
tools/verify.sh --idf-only
```

Connected-target validation additionally observes Blynk message timing and
silence, command semantics, notification delivery, reconnect without command
replay, and Blynk-triggered signed GitHub OTA while not `RUNNING`.

## Risks / unresolved items

- The exact Blynk template/datastream/event names and display precision must be
  fixed before firmware implementation so change equality is deterministic.
- Secret provisioning must remain simple for one device without committing the
  token or exposing it through logs/UI; the exact local mechanism is not yet
  selected.
- Blynk Device MQTT currently uses clean sessions and public-service limits;
  implementation must fail remote access closed and never compensate by
  replaying energizing commands.
- Notification delivery and cloud graphs are best-effort auxiliary behavior,
  not safety evidence or authoritative M14 history.
