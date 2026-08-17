# Password-only LAN authentication and commissioning-only SoftAP

## Goal

Make the open `Smoker-<MAC6>` SoftAP a Wi-Fi-commissioning-only surface and
make the STA/LAN operational surface use device-password-to-session-cookie
authentication without HTTP Basic, users, or STA OPEN support.

## Scope

- Classify every HTTP request from the socket's local IPv4 address before Host,
  authentication, or routing.
- Serve a separate public commissioning UI and provisioning APIs on
  `192.168.4.1` only.
- Serve password-only login, the full dashboard, snapshots, commands, network
  configuration, logout, and device-password change on the current STA address
  only.
- Keep one random 256-bit `HttpOnly`, `SameSite=Lax` session with a 30-minute
  idle timeout; every login, logout, and password change invalidates the prior
  token.
- Accept only WPA2/WPA3 Personal STA credentials with an 8..63 byte password.
- Update embedded UI, host/fixture/browser checks, architecture guardrails, and
  the M12 documentation/traceability decision record.

## Non-goals

- TLS, Bearer tokens, JavaScript token storage, users, or roles.
- Any control-loop, sensor, heater, SSR, OTA, or M6B-M10 work.
- Hardware-safety or physical-radio validation claims.
- Changing the intentionally open SoftAP or the initial device password
  `smoker257500`.

## Current repository observations

- Current milestone is M12: implemented for simulated I/O and host/browser/
  cross-build validated, with physical radio scenarios still pending.
- M12 is an uncommitted working slice; those existing changes are preserved.
- HTTP routing, NVS credentials, Wi-Fi mode changes, sessions, and embedded
  assets are concentrated in `smoker_platform`.
- The current code permits dashboard/control on AP, HTTP Basic `admin`, an
  optional `device_password` field in network provisioning, and STA OPEN.
- `SmokerApplication` ownership and the bounded mailbox/snapshot exchange do
  not need modification.

## Assumptions

- ESP-IDF HTTP sockets expose the accepted socket through
  `httpd_req_to_sockfd()` and `getsockname()` returns the netif-local IPv4.
- The default SoftAP address remains the ESP-IDF-configured `192.168.4.1`.
- The current STA IPv4 stored from `IP_EVENT_STA_GOT_IP` is authoritative for
  operational request classification.
- HTTP remains plaintext; WPA protects the STA radio link but is not end-to-end
  transport encryption.

## Steps

1. Add host-testable request-scope/session policy helpers and tests for AP,
   STA, unknown address, WPA2/WPA3-only selection, idle expiry, replacement,
   logout, and password-change invalidation.
2. Refactor the ESP HTTP router to enforce scope before authentication, add
   commissioning-only routes/assets, add JSON session/logout/password APIs,
   remove Basic/admin, and require exact Origin for every state-changing route.
3. Split Wi-Fi and device-password persistence/update flows, enforce exact
   schemas and 8..63-byte WPA2/WPA3 credentials, and remove STA OPEN mode.
4. Update the HTTP/browser fixtures and embedded UI for distinct AP and STA
   experiences, including logout and password change.
5. Record D047 and align architecture, roadmap, traceability, guardrails, and
   fixture checks with the new accepted boundary.
6. Run host tests, ASan/UBSan, HTTP fixture, Playwright browser validation,
   architecture/traceability guardrails, diff checks, ESP-IDF 6.0.2 build, and
   firmware-size validation.

## Validation commands

```text
cmake -S . -B build-host && cmake --build build-host && ctest --test-dir build-host --output-on-failure
cmake -S . -B build-sanitize -DSMOKER_ENABLE_SANITIZERS=ON && cmake --build build-sanitize && ctest --test-dir build-sanitize --output-on-failure
python3 tools/check_m12_http_fixture.py
bash tools/check_m12_browser.sh
python3 tools/check_architecture.py
python3 tools/check_traceability.py
git diff --check
tools/verify.sh
python3 tools/check_firmware_size.py build-verify/smoker-controller.bin
```

## Risks / unresolved items

- Captive assistants vary; browser/fixture validation cannot prove iPhone CNA
  behavior on the final board.
- A SoftAP client can still observe or alter plaintext commissioning traffic;
  the AP is intentionally open and contains only Wi-Fi setup data.
- Physical AP/STA disappearance, WPA2/WPA3 association, mDNS, and Wi-Fi-loss
  autonomy remain target tests. Simulation/build tests are not SSR, thermal,
  electrical, or independent-safety evidence.
