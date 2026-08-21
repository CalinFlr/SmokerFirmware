# M15 Reconnect Boundary Remediation Plan

Status: implemented and host/sanitizer plus ESP-IDF 6.0.2 cross-build validated;
the deliberate collapsed reconnect remains target-pending.

## Goal

Harden the M15 Blynk disconnect/reconnect boundary so state originating from an
old MQTT connection cannot be processed or published through a later connection,
including when disconnect/error and reconnect callbacks both occur between two
`BlynkTask` polls.

## Scope

- A small platform-neutral connection transition policy used by the ESP-MQTT
  callback, `BlynkTask`, and the Blynk application-mailbox consumer boundary.
- Connection-generation tagging for bounded inbound and translated Blynk
  commands so stale work can be discarded without rejecting a new live command.
- Disconnect cleanup of the pending Start parameter, correlated results,
  already-popped feedback, events, raw inbound commands, and inbound-drop
  observation baseline.
- Focused M15 host regressions plus narrow source/build integration guardrails.
- Minimal architecture/decision/traceability clarification for the strengthened
  M15 reconnect contract.

## Non-goals

- New Blynk features, datastreams, provisioning behavior, or MQTT semantics.
- Changes to local HTTP command behavior, application command semantics, safety,
  heater control, OTA policy, history, GPIO, sensors, SSRs, or hardware scope.
- Flashing, provisioning, monitoring, release publication, or any target/hardware
  validation claim.
- Speculative changes for unreachable provisioning inputs or already-observable
  application-queue rejection.

## Current repository observations

- `main` is clean at `v0.15.0` (`4fa9ea9`) before this remediation.
- `blynk_service.cpp` increments one transition counter for CONNECTED,
  DISCONNECTED, and ERROR, but `BlynkTask` selects cleanup only from the final
  `mqtt_connected_` Boolean. A disconnect followed by a successful connect
  between polls therefore calls only `projection_.connected()` and skips
  `handle_disconnect()`.
- `handle_disconnect()` drains raw inbound commands but does not advance
  `observed_inbound_drops_`. A drop recorded before disconnect is consequently
  queued as a new `RemoteError` by the first post-reconnect `process_inbound()`.
- Raw commands and translated Blynk mailbox entries currently carry no MQTT
  connection identity, so indiscriminate cleanup cannot both reject old work and
  preserve new work received after reconnect.
- `SmokerApplication::submit()` returning `false` is not silent loss: it records
  the correlated semantic rejection, increments the application overflow
  counter, and emits `CommandQueueOverflow` on the next tick. The drain count is
  an attempted/submitted-to-application count, not a semantic-success claim.
- The provisioning parser constructs a SET request only after length, CRC, and
  `valid_blynk_configuration()` checks. The private `persist_configuration()`
  has no other SET caller, so invalid configuration cannot currently reach the
  branch reported as `nvs_write_failed`.

## Assumptions

- ESP-MQTT invokes this service's callback as the single producer of connection
  transitions and raw Blynk messages; `BlynkTask` is their single consumer.
- A command validated and submitted to `SmokerApplication` before a disconnect
  transition is application-owned work. Work still pending in either Blynk
  transport after the transition must carry its originating connection
  generation and be rejected by the consumer boundary.
- Counter wrap remains bounded 32-bit arithmetic; generation zero is reserved
  as the no-connection/default identity.
- Existing clean-session, QoS 0, no-retain, no-sync/get behavior remains
  unchanged.

## Steps

1. Add and host-test a minimal atomic connection-boundary helper that reports
   disconnect cleanup independently of the final connected state, issues a
   nonzero generation per successful connection, and rejects stale generations.
2. Tag raw inbound and translated Blynk mailbox commands with the originating
   generation. Make `BlynkTask` perform cleanup before activating a new
   connection, discard stale raw entries, acknowledge the drop counter, and
   gate every inbound/outbound action on a still-current connection snapshot.
3. Extend the round-robin consumer with a narrow Blynk-generation validator so
   translated commands left in the Blynk mailbox across disconnect are discarded
   before `SmokerApplication::submit()`, without changing HTTP or submit-failure
   semantics.
4. Add focused regressions covering a collapsed disconnect/reconnect, stale
   Start/other input, pending Start target/result/feedback/event cleanup, stale
   drop-count acknowledgement, stale translated-command rejection, and a new
   post-reconnect command remaining processable.
5. Add a narrow target-source guardrail and update M15 architecture, D058, and
   traceability wording only to the validation strength actually achieved.
6. Run all required host, ESP-IDF 6.0.2, diff, and worktree validation gates;
   fix only regressions introduced by this remediation.

## Validation commands

```text
./tools/verify.sh --host-only
./tools/verify.sh --idf-only
git diff --check
git status --short
```

Focused development checks may run the M15 host executable/CTest group and the
architecture guardrail before the full required gates.

## Risks / unresolved items

- Atomic generation checks prove the software handoff policy and target build,
  not ESP-MQTT broker timing on a connected board. Deliberate target
  disconnect/reconnect validation remains pending.
- A transition concurrent with an already-entered ESP-MQTT publish call is
  subject to ESP-MQTT's synchronous API boundary; the policy prevents later
  task work and queued old-generation work from being admitted to a new
  connection.
- No ESP32 is connected for this task. No hardware, sensor, SSR, thermal, or
  independent electrical-safety behavior can be claimed.

## Execution log

- 2026-08-21: Confirmed that final-state-only polling skipped cleanup for a
  DISCONNECTED/ERROR -> CONNECTED sequence between polls. Added independent
  disconnect and successful-connection generations, generation tags through
  both Blynk SPSC stages, stale filtering in `BlynkTask` and `ControlTask`, and
  cleanup-before-reconnect activation.
- 2026-08-21: Confirmed stale inbound-drop replay. Disconnect now captures the
  callback-ordered old-generation drop watermark and cleanup acknowledges it;
  a later drop belonging to the new generation remains observable.
- 2026-08-21: Rejected the `RoundRobinCommandDrain` silent-loss hypothesis:
  `SmokerApplication::submit()` already records a correlated false result,
  increments the observable overflow counter, and emits the overflow event.
  The drain's existing submit-failure semantics were left unchanged.
- 2026-08-21: Rejected the invalid-SET/NVS-error hypothesis: the only private
  SET call is reached through parser length, CRC, and configuration validation.
  Added a parser regression proving invalid configuration yields no request;
  persistence reporting was left unchanged.
- 2026-08-21: `./tools/verify.sh --host-only` passed all 9 normal and all 9
  ASan/UBSan test groups. `./tools/verify.sh --idf-only` passed with ESP-IDF
  exactly 6.0.2, strict C++20 across 24 project sources, all guardrails, and a
  1,376,256 / 3,145,728-byte image (43.8%). No flash, monitor, provisioning,
  release, connected-board, or hardware test was performed.
