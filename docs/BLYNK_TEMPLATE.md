# Blynk template contract — M15

Status: **Implementation contract — no Blynk device secret is stored here.**

This document freezes the Blynk template surface used by the M15 firmware. The
template has one device for one owner. Blynk is an auxiliary remote client: it
does not own smoker state, does not receive M14 raw-history backfill, and does
not provide a heater command.

The firmware uses Blynk Device MQTT over TLS with a clean session. It publishes
the first 15 status fields below together in one `batch_ds` payload.
`LastCommandResult` is the sixteenth output datastream but is published
separately and is never part of change-throttled status. Together with the nine
controls, the template has exactly 25 datastreams. The names are part of the
wire contract; rename neither a Blynk datastream nor its data type without a
coordinated firmware change.

## Status datastreams

| Datastream name | Type | Normalized value |
|---|---|---|
| `SessionStatus` | String | `IDLE`, `RUNNING`, `STOPPED`, or `FAULT` |
| `SessionElapsedSeconds` | Integer | elapsed session time, rounded down to seconds |
| `ChamberCurrentC` | Double | Celsius, one decimal; `-273.1` means unavailable |
| `ChamberTargetC` | Double | Celsius, one decimal; `-273.1` means monitoring-only |
| `HeaterPercent` | Integer | normalized demand, rounded to the nearest whole percent |
| `TimerState` | String | `NONE`, `WAITING`, `RUNNING`, or `COMPLETED` |
| `TimerElapsedSeconds` | Integer | elapsed timer time, rounded down; `-1` when no timer exists |
| `ProbeSummary` | String | bounded compact summary of up to six configured probes |
| `ActiveAlarmCount` | Integer | unresolved alarm count |
| `FaultCode` | String | fault code or empty string |
| `FirmwareState` | String | existing M13 state name |
| `FirmwareCurrentVersion` | String | current version |
| `FirmwareAvailableVersion` | String | available release tag, `Latest` when checked and current, or empty during other states |
| `FirmwareProgressPercent` | Integer | `0..100` |
| `FirmwareError` | String | bounded M13 error or empty string |
| `LastCommandResult` | String | separate `<correlation-id>:<result>` feedback, not MQTT admission |

`ProbeSummary` is a display-only compact string. Each included probe is encoded
as `id:name:current:target:enabled:alarm`, with temperatures in Celsius to one
decimal and `-` for absent readings/targets. Names are sanitized and the whole
field is capped by the MQTT message budget. The six-probe display cap is a
Blynk-template/UI bound, not a `smoker_core` product capacity rule; omitted
probes remain available to local control and local UI.

The `-273.1` sentinel is outside the accepted domain temperature range. Status
uses it because Blynk numeric datastreams cannot carry JSON `null` in a
`batch_ds` value; command payloads use it to request an absent optional target.
It maps immediately to `std::nullopt` and is never stored as a domain
temperature.

`FirmwareAvailableVersion` is the mobile-friendly firmware display: it contains
the available release tag while `FirmwareState` is `AVAILABLE`, `Latest` while
the state is `UP_TO_DATE`, and an empty string while no completed check result
is available. `FirmwareState` remains the diagnostic state machine value.

## Live control datastreams

All controls are inbound `downlink/ds/<name>` messages and must be configured
with **Sync with latest server value disabled**. They represent a live user
gesture, never Blynk-owned desired state. The firmware must not publish to,
request, or restore any control datastream, including after reconnect.

| Datastream name | Type / accepted payload | Resulting existing operation |
|---|---|---|
| `CmdStartRequest` | String `1` or `1,<target_celsius>` | one atomic `StartSessionCommand`; `1` uses the startup recipe, an explicit target replaces its chamber target, and `-273.1` selects monitoring-only |
| `CmdStop` | Integer `1` | `StopSessionCommand` |
| `CmdChamberTargetC` | Double, valid target or `-273.1` | `SetChamberTargetCommand`; sentinel clears the target |
| `CmdProbeTarget` | String `probe_id,target_celsius` | `SetProbeTargetCommand`; target `-273.1` clears it |
| `CmdProbeEnabled` | String `probe_id,0|1` | `SetProbeEnabledCommand` |
| `CmdProbeAlarmEnabled` | String `probe_id,0|1` | `SetProbeAlarmEnabledCommand` |
| `CmdAcknowledgeAlarm` | Integer alarm ID | `AcknowledgeAlarmCommand` |
| `CmdClearResolvedFault` | Integer `1` | `ClearResolvedFaultCommand` |
| `CmdFirmware` | Integer `1` check, `2` install | existing M13 check/install request only |

`CmdStartRequest` accepts exactly `1` or `1,<target_celsius>`. The target uses
the existing strict decimal parser: optional minus, decimal digits, and at most
one decimal point. Whitespace, plus signs, exponents, NaN/infinity, locale
separators, empty fields, a prefix other than `1`, multiple commas, and extra
fields are malformed. The mapper retains no parameter from one message for a
later Start, and the payload is not JSON.

Every accepted transport request receives a locally generated nonzero 32-bit
correlation ID. `LastCommandResult` is updated only when the immutable
application snapshot reports its semantic result. A full mailbox, malformed
payload, or unsupported value is reported as bounded remote feedback; it is
not reported as a successful smoker operation. `CmdStartRequest` and
`CmdFirmware=2` are never replayed from a stored/synchronized value after
reconnect. Parsing stays single-sourced in the mapper, so a malformed
`CmdStartRequest` may consume an internal session ID; session IDs may therefore
contain gaps, but no malformed command or correlation reaches the application.

## Deprecated Start protocol

`CmdStart` and `CmdStartTargetC` are recognized only for explicit fail-closed
rejection. Neither belongs to the active template contract. Neither can create
or parameterize a command, their former two-message sequence cannot start a
session, and no legacy target is retained. Each received legacy message emits a
bounded `smoker_remote_error` describing the deprecated protocol; it never
produces semantic-success feedback or enters the application mailbox.

## Manual Blynk Console migration

This repository change does not modify Blynk Console. The owner must migrate
the template manually in this safe order:

1. Create `CmdStartRequest` with Blynk type String.
2. Set **Sync with latest server value disabled** for the new datastream.
3. Configure the Start UI action to send `1` or `1,<target_celsius>` as one
   message.
4. Remove or disable the widgets for `CmdStart` and `CmdStartTargetC`.
5. Only then install firmware containing the atomic protocol.

This order is fail-closed across a version mismatch: old firmware ignores the
new `CmdStartRequest`, while new firmware explicitly rejects both old Start
datastreams. Neither mismatch can accidentally start a session. All nine active
controls must retain **Sync with latest server value disabled**; do not add a
retained publish, `get/ds`, synchronization, or replay during migration.

## Events

Create these five Blynk event codes. Their descriptions are bounded and contain
no credential, endpoint, URL, or raw-history data.

| Event code | Emitted for |
|---|---|
| `smoker_fault` | a newly observed active fault |
| `smoker_alarm` | a newly observed unresolved alarm |
| `smoker_session_done` | a newly observed stopped/faulted session terminal state |
| `smoker_ota` | M13 check/install/validation failure or completion transition |
| `smoker_remote_error` | malformed command, mailbox saturation, or remote publish failure summary |

Command results remain datastream feedback rather than notifications, avoiding
one push notification for every ordinary setting adjustment.

## Local secret provisioning

The regional endpoint, Blynk template ID, and device auth token are stored in a
versioned, CRC-protected blob in the dedicated `fumuri_blynk` NVS namespace.
Absent or invalid data disables only Blynk. The blob is intentionally
unencrypted in M15 and can be extracted by an attacker with physical flash
access; flash/NVS encryption and eFuse provisioning remain out of scope.

Provision through the confirmed KFB003 USB-to-UART0 link. `set` prompts for the
device token without echo and never accepts it as a process argument. `status`
reports only `token=present` or `token=absent`, and `clear` erases the blob:

```sh
python3 tools/provision_blynk.py --port /dev/cu.usbserial-XXXX set \
  --endpoint fra1.blynk.cloud --template-id TMPLxxxx
python3 tools/provision_blynk.py --port /dev/cu.usbserial-XXXX status
python3 tools/provision_blynk.py --port /dev/cu.usbserial-XXXX clear
```

The bounded wire format is `FUMURI-BLYNK/1`; firmware never echoes or logs the
token and restarts only the MQTT client after a successful update. No token is
stored in source, GitHub Secrets, build inputs, release artifacts, browser
state, screenshots, snapshots, HTTP responses, logs, events, or documentation
evidence.

## Minimal mobile dashboard

The one-owner Blynk mobile dashboard shows session/fault state, chamber current
and target, heater demand, timer state/elapsed time, probe summary, alarm count,
firmware state/progress, and the latest command result. It exposes the nine live
controls above for Start/Stop, configuration, alarm/fault actions, and M13 OTA
check/install. Template/device creation, widgets, event notifications, and
datastream **Sync with latest server value disabled** are console-side setup;
they require the owner's temporary Blynk credentials and are not repository
artifacts.

## Service constraints

Blynk's Device MQTT API documentation specifies TLS on port 8883, username
`device`, token-as-password, a clean session, a recommended 45-second
keepalive, and a 1,024-byte MQTT message limit. M15 uses the owner-selected
regional endpoint directly, validates the `*.blynk.cloud` host form, and does
not implement redirect discovery. MQTT publishes use QoS 0 without retain, the
only subscription is `downlink/ds/#`, and the firmware never requests `get/ds`,
sync/replay, Blynk.Air, or `downlink/ota`. The fixed serialized `batch_ds`
payload and all independent feedback messages remain within that limit.

Official references: [Blynk Device MQTT authentication](https://docs.blynk.io/en/blynk.cloud-mqtt-api/device-mqtt-api/authentication), [Blynk datastreams](https://docs.blynk.io/en/blynk.cloud-mqtt-api/device-mqtt-api/datastreams), and [Blynk events](https://docs.blynk.io/en/blynk.cloud-mqtt-api/device-mqtt-api/events).
