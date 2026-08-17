# M13 OTA HTTPS and Rollback Plan

Status: **Complete for the defined M13 software and connected-target scope:
signed USB migration, public GitHub OTA, both-slot boot, rollback, and final
validation passed on KFB003**

## Goal

Add a manual, authenticated GitHub Releases OTA flow with rollback-capable
partitions and bounded post-update validation, while keeping networking and
flash writes outside the critical `ControlTask` dependency chain.

## Scope

- publish application version `0.13.0` from `version.txt`;
- add the approved 16 MiB custom dual-OTA partition table and a 75% build-size
  ceiling for each 3 MiB application slot;
- add application-owned OTA permission commands and the
  `firmware_update_active` snapshot flag without changing `SessionStatus`;
- add one static, low-priority `OtaTask` on core 0 using ESP-IDF HTTPS HTTP and
  native OTA APIs, certificate bundle, SNTP, real image metadata, image
  verification, boot partition selection, and rollback APIs;
- expose fixed-release-source firmware status/check/install routes only on the
  authenticated exact-Origin STA surface;
- add the Romanian dashboard controls and progress presentation;
- add a tag-gated release workflow which validates tag/version equality and
  signs in a tag-restricted environment, verifies against the repository public
  key, and publishes signed `smoker_controller.bin` plus SHA-256;
- update architecture, safety, data model, decisions, roadmap, traceability,
  README, host tests, browser fixture, and executable guardrails.

## Non-goals

- no arbitrary URL, LAN upload, automatic checks, downgrade, or reinstall;
- no hardware Secure Boot, eFuse provisioning, flash encryption, release token
  or private signing key in firmware, or cloud dependency;
- no M6B/M7-M10 sensing, SSR, persistence, or power-recovery implementation;
- no claim that host, browser, cross-build, or simulated-I/O evidence validates
  real sensors, SSR behavior, thermal safety, or independent electrical safety;
- release publication is operational completion evidence, not part of the
  firmware implementation itself.

## Current repository observations

- M12 has one `ControlTask` on core 1, one temporary captive-DNS helper on core
  0, a single-producer HTTP-to-control mailbox, and a preallocated snapshot
  exchange;
- `SmokerApplication::submit()` has exactly one production call site in
  `ControlTask`; HTTP receives semantic results through correlated snapshots;
- target code currently uses the 1500 KiB single-app layout and has no OTA
  component, rollback state, wall-clock synchronization, or firmware routes;
- operational HTTP is already authenticated, exact-Origin, and unavailable
  whenever the commissioning SoftAP is active;
- the final controller has target-confirmed 16 MiB flash, but external I/O is
  still simulated.

## Assumptions

- the public release asset remains available at the fixed approved GitHub URL;
- ESP-IDF's certificate bundle validates TLS, redirects are followed manually
  only while they retain HTTPS, and RSA-3072 signed-update verification chains
  each release to the currently running signed app; physical-flash attackers
  remain outside this software-only trust model;
- the first M12-to-M13 deployment is a complete serial flash because M12 cannot
  migrate its own partition table;
- target rollback and release-download evidence requires a connected KFB003 and
  published release; both were available for the final completion run.

## Steps

1. Add version/partition/configuration files and release/build validation.
2. Add OTA permission commands, application policy, snapshot propagation, and
   host tests for RUNNING rejection, Start blocking, and finish/error release.
3. Add platform-independent semantic-version/descriptor/state helpers and host
   tests for version/project/target admission.
4. Add the ESP-IDF `OtaTask`, fixed HTTPS source, bounded manual redirects,
   total operation/permission deadlines, real chip-ID/descriptor revalidation,
   download/install/reboot, bounded status/error/progress, and pending-image
   five-cycle validation/rollback.
5. Add authenticated STA-only exact-Origin firmware APIs and embedded dashboard
   check/install/progress UI; extend HTTP/browser fixtures.
6. Update executable architecture/partition/size guardrails and repository
   documentation/traceability.
7. Run host, ASan/UBSan, fixture/browser, ESP-IDF 6.0.2 build, partition and
   compile-command checks, and `git diff --check`; record target-only gaps.

## Validation commands

```text
tools/verify.sh --host-only
SMOKER_VERIFY_BUILD_DIR="$PWD/build-m13-final" tools/verify.sh --idf-only
python3 tools/check_architecture.py
python3 tools/check_traceability.py
python3 tools/check_partitions.py partitions.csv
python3 tools/check_target_compile_commands.py build-verify/compile_commands.json
python3 tools/check_firmware_size.py build-verify/smoker_controller.bin --partition-size 3145728 --maximum-used-percent 75
python3 tools/check_m12_http_fixture.py
PWCLI=/path/to/playwright_cli.sh tools/check_m12_browser.sh
git diff --check
```

## Risks / unresolved items

- GitHub reachability, DNS, correct wall time, and CA validation may fail while
  local smoking control must continue normally;
- a complete partition-table migration necessarily needs serial flashing;
- the completed target scenario used a temporary signed `0.12.99` bootstrap
  image and the published `0.13.0` asset to prove both-slot installation and
  rollback;
- Secure Boot and flash encryption remain required for protection against
  physical flash replacement but are intentionally outside M13.
- loss of the private OTA key requires a new serial trust anchor on every
  installed board; disclosure requires the same recovery and makes all
  pre-recovery boards vulnerable to attacker-signed updates.

## Completion report

Changed files cover version/partition/default configuration, application
commands and snapshots, platform OTA policy/service/runtime, authenticated HTTP
routes, embedded UI, release workflow, host/browser fixtures, guardrails, and
all milestone documentation. D050 records the fixed-source/manual-update,
application-permission, pending-validation, and security-boundary decisions.

Completed validation on 2026-08-17:

- `tools/verify.sh --host-only`: PASS, including all seven CTest cases and
  ASan/UBSan;
- `tools/check_m12_browser.sh`: PASS using the Playwright CLI against separate
  commissioning and operational fixture origins;
- fresh ESP-IDF `v6.0.2` build in `build-review-m13-fixed`: PASS with strict C++20 and
  the generated custom dual-OTA table verified from its binary;
- `smoker_controller.bin`: 1,205,632 bytes of a 3,145,728-byte slot (38.3%);
- architecture, traceability, source partition table, generated partition
  binary, HTTP fixture, and `git diff --check`: PASS.

The final OTA review remediation replaces internally followed redirects with a
manual HTTPS-only redirect loop (maximum five hops), bounds the complete check,
install, and application-permission operations, and parses the real ESP image
chip ID before offering or installing the release. Host tests cover image
metadata and monotonic-deadline edge cases; target compile-time assertions bind
the parser offsets and ESP32-S3 chip ID to ESP-IDF 6.0.2 structures.

The P1 OTA review remediation removes the 16 KiB worker stack and TCB from the
heap-owned `FirmwareUpdateService::Impl`. Target ELF evidence places the stack
at `0x3fc9c148` (size `0x4000`) and the TCB at `0x3fca0148` in internal
`.dram0.data`; the remaining `Impl` allocation is `0x128` bytes. This prevents
an external-PSRAM stack from becoming inaccessible while OTA flash operations
suspend cache access.

The second P1 remediation replaces HTTP-mailbox transport of Prepare with a
dedicated one-slot atomic signal. `ControlTask` retries that signal ahead of
mailbox draining and defers Finish until Prepare is admitted, after which the
application FIFO preserves order. The host regression covers a Stop barrier
followed by Prepare and Finish, and architecture guardrails enforce both signal
ordering and internal task-storage placement.

The local-change review remediation also routes runtime-context, ControlTask,
and OtaTask bootstrap failures through immediate pending-image rollback; makes
an unavailable ordinary-boot OTA worker report `FAILED` and reject work;
returns firmware-check admission from fixed storage; and scopes the command
admission guardrail to the real `handle_command()` body instead of the first
OTA mailbox use.

At that implementation-review point no board had been flashed and no tag or
GitHub Release had been created, so no target behavior was claimed by that
review. The later connected-target completion evidence is recorded below.
Real-radio failure isolation, sensors, SSR, and electrical safety remain
separate M12/M6B-M8 evidence and are not claimed by M13.

The signed-OTA review remediation enables ESP-IDF's RSA-3072 signed-app
verification during updates without enabling hardware Secure Boot. Ordinary
and PR builds produce only a secure-padded unsigned image; the release-only
script reconstructs the private key in a permission-restricted temporary
directory, signs the already validated image, verifies it against the
repository public key, applies the signed-size ceiling, and removes the
temporary copy. The locally generated private key is mode `0600` under the
ignored `local-secrets/` directory. Its public half and fingerprint are
versioned under `keys/`.

Fresh remediation validation on 2026-08-17 used
`build-m13-signed-20260817`: ESP-IDF 6.0.2 strict-C++20 build PASS with the
expected signed-update configuration and verification symbols; unsigned image
1,245,184 bytes; signed image 1,249,280 bytes (39.7% of a 3 MiB slot). Both
file-key and GitHub-base64-secret signing paths passed public-key verification.
A different public key and a one-byte-modified signed image were both rejected.
The complete host/ASan/UBSan/HTTP/guardrail suite also passed.

The GitHub `firmware-release` environment contains
`SMOKER_OTA_SIGNING_KEY_B64` and accepts only deployment tags matching
`v*.*.*`. No independent reviewer is claimed while the project has only one
maintainer. An encrypted maintainer-controlled backup is still operationally
required because GitHub Actions secrets cannot be read back. Key loss or
disclosure requires serial installation of a new trust anchor on every board.

The P1/P2 review remediation adds an explicit signed serial-flash helper and an
effective generated-Kconfig gate. Ordinary M13 builds remain unsigned and must
not be passed to `idf.py flash`: the helper verifies the signed image against
the repository public key, proves it is derived from the selected build,
validates the generated partition table, and substitutes it into ESP-IDF's own
generated flash map. `tools/verify.sh --idf-only` now rejects stale build
directories whose generated `sdkconfig` does not actually enable the M13
signed-update and dual-OTA settings; the release signing path uses the same
check.

D051 records the remaining P3 release supply-chain condition. The missing
independent reviewer is accepted only for the current single-maintainer access
model. If any additional person or automation gains repository or tag-write
access, the signing boundary must be reviewed before another release. This is
documentation of an operational condition, not proof that GitHub permissions
cannot change.

Post-remediation validation on 2026-08-17 passed the complete host,
ASan/UBSan, HTTP-fixture, architecture, and traceability suite. An incremental
ESP-IDF 6.0.2 verification in the fresh M13 build directory passed the effective
configuration, strict-C++20, generated-partition, and 39.6% image-size gates.
The previously stale `build-verify` directory was rejected for its effective
single-app partition setting, reproducing and closing the P2 false-green path.
Serial `--check-only` passed for the existing 1,249,280-byte RSA-verified image
and matching `build-m13-signed-20260817` artifacts; an unsigned image, a signed
image from a different build, and omission of `--yes` were rejected. No serial
write was attempted and no board behavior is claimed by this evidence.

The final local-review P2 remediation makes every ordinary ESP-IDF target that
could install the deliberately unsigned application or a partial M13 boot
layout fail before serial access. Target verification exercises all five
blocked targets against `/dev/null` and requires the explicit diagnostic that
directs operators to the signed-image helper. `erase-flash` remains available
only for deliberate recovery and is not used by the migration procedure.

Connected-target validation on 2026-08-17 identified the native USB device as
ESP32-S3 revision 0.2 with 16 MiB flash and 8 MiB PSRAM, backed up the exact
24 KiB NVS range, and used only the signed helper to write bootloader, partition
table, initial OTA metadata, and the RSA-verified `0.13.0` application. Every
write hash verified. Boot evidence showed `ota_0`, the dual-slot layout,
OtaTask/core 0, ControlTask/core 1, and simulated heater `0%`. The logical NVS
entries and value CRCs matched before/after and the saved STA reconnected. A
real authenticated firmware check validated GitHub's TLS certificate and
received HTTP `404` because no release exists yet.

The private-repository CI was rejected before runner assignment by account
billing. D052 replaces that distribution boundary with the public canonical
repository: a sanitized working-tree snapshot becomes its single root commit,
the historical repository remains a private archive, and the device needs no
GitHub token. At D052 adoption, publication, `ota_1`, five-cycle mark-valid,
forced pending-image reset, rollback, and final reinstall remained open gates.

Final connected-target and release validation on 2026-08-18 closed those M13
gates:

- public CI passed host guardrails and the ESP-IDF `v6.0.2` cross-build before
  protected-main integration;
- the tag-restricted release workflow built and RSA-signed `v0.13.0`; anonymous
  `latest/download` returned a 1,249,280-byte image with SHA-256
  `b511934ec354392ba6ee20e4b687d6e3e765e9722a0e2c3cdf5fafc7f559e91b`,
  valid against `keys/smoker_ota_signing_public.pem`, with ESP32-S3 chip ID 9,
  application version `0.13.0`, and ESP-IDF `v6.0.2` metadata;
- the first target check exposed an ESP-IDF request-TX exhaustion after GitHub
  redirected to a 923-character signed asset URL. The observed GET request line
  required 893 bytes while the default TX buffer was 512 bytes. The reviewed
  fix uses a guarded 4,096-byte TX buffer and the repaired `0.12.99` target then
  reported the public `0.13.0` release as `AVAILABLE`;
- the signed USB helper installed the repaired `0.12.99` bootstrap in `ota_0`
  without writing NVS, and every serial write hash verified;
- OTA installed `0.13.0` into `ota_1`. A reset immediately after its first boot
  and before the five-cycle validation completed caused the next boot to return
  to `0.12.99` in `ota_0`, with simulated heater command `0.0%`;
- a second OTA installation booted `0.13.0`, logged successful validation after
  five safe control cycles, and a later controlled reboot remained on `ota_1`
  with simulated heater command `0.0%`;
- the authenticated firmware API subsequently reported `IDLE`, current version
  `0.13.0`, no available version, and no error.

SSID, station IP, MAC address, and native-USB identifiers are deliberately
excluded from repository evidence. This completion proves the defined M13
software/release/target path only; it does not prove real sensing, SSR output,
thermal behavior, or independent electrical safety.
