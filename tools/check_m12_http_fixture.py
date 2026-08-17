#!/usr/bin/env python3
"""HTTP contract checks for commissioning-only AP and password/session STA."""

from __future__ import annotations

import base64
import json
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, request, file_pointer, code, message, headers, new_url):
        return None


def response(request: urllib.request.Request):
    try:
        return urllib.request.build_opener(NoRedirect).open(request, timeout=3)
    except urllib.error.HTTPError as error:
        return error


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def wait_ready(process: subprocess.Popen[bytes], ready: Path) -> tuple[str, str]:
    for _ in range(50):
        if process.poll() is not None:
            raise RuntimeError("M12 browser fixture exited before accepting requests")
        if ready.exists() and ready.stat().st_size:
            ports = json.loads(ready.read_text())
            return (f"http://127.0.0.1:{ports['commissioning']}",
                    f"http://127.0.0.1:{ports['operational']}")
        time.sleep(0.1)
    raise RuntimeError("M12 browser fixture did not become ready")


def json_request(url: str, method: str = "GET", body: bytes | None = None,
                 headers: dict[str, str] | None = None):
    values = dict(headers or {})
    if body is not None:
        values.setdefault("Content-Type", "application/json")
    return response(urllib.request.Request(url, data=body, method=method, headers=values))


def main() -> int:
    temporary = tempfile.TemporaryDirectory()
    ready = Path(temporary.name) / "ready"
    process = subprocess.Popen(
        [sys.executable, str(ROOT / "tools/m12_browser_fixture.py"),
         "--port", "0", "--ready-file", str(ready)],
        cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
    )
    try:
        ap, sta = wait_ready(process, ready)

        ap_root = response(urllib.request.Request(f"{ap}/"))
        ap_html = ap_root.read()
        require(ap_root.status == 200 and b"NUMAI WI-FI" in ap_html,
                "AP root must be the public commissioning page")
        require(b"Camera afum" not in ap_html and b"heater-ring" not in ap_html,
                "AP page must contain no cooking/heater data")
        captive = response(urllib.request.Request(f"{ap}/hotspot-detect.html"))
        require(captive.status == 302 and captive.headers["Location"] == "http://192.168.4.1/",
                "captive probes must canonicalize to the setup root")
        foreign = response(urllib.request.Request(f"{ap}/", headers={"Host": "captive.apple.com"}))
        require(foreign.status == 302 and foreign.headers["Location"] == "http://192.168.4.1/",
                "foreign captive Host must canonicalize to the AP root")
        for forbidden in ("login", "api/v1/snapshot", "api/v1/commands",
                          "api/v1/firmware", "api/v1/firmware/check",
                          "api/v1/firmware/install"):
            blocked = response(urllib.request.Request(
                f"{ap}/{forbidden}", headers={"Cookie": "smoker_session=" + "1" * 64}
            ))
            require(blocked.status == 403, f"AP must reject {forbidden} even with a token")
        require(response(urllib.request.Request(f"{ap}/api/v1/setup/status")).status == 200,
                "AP must expose provisioning status")
        require(response(urllib.request.Request(f"{ap}/api/v1/network/scan")).status == 200,
                "AP must expose public scan results")
        missing_origin = json_request(f"{ap}/api/v1/network/scan", "POST", b"")
        require(missing_origin.status == 403, "AP provisioning writes require Origin")
        scan_start = json_request(f"{ap}/api/v1/network/scan", "POST", b"",
                                  {"Origin": ap})
        require(scan_start.status == 202, "AP scan start accepts exact Origin")
        invalid_setup = json_request(
            f"{ap}/api/v1/setup/network", "PUT",
            b'{"ssid":"Atelier","wifi_password":"parola-wifi","device_password":"bad"}',
            {"Origin": ap},
        )
        require(invalid_setup.status == 400, "setup network schema must reject extra fields")
        open_setup = json_request(
            f"{ap}/api/v1/setup/network", "PUT",
            b'{"ssid":"Open","wifi_password":""}', {"Origin": ap},
        )
        require(open_setup.status == 400, "setup must reject STA OPEN credentials")
        valid_setup = json_request(
            f"{ap}/api/v1/setup/network", "PUT",
            b'{"ssid":"Atelier","wifi_password":"parola-wifi"}', {"Origin": ap},
        )
        require(valid_setup.status == 202, "AP must accept exact WPA2/WPA3 setup schema")

        unauthenticated = response(urllib.request.Request(f"{sta}/api/v1/snapshot"))
        require(unauthenticated.status == 401, "STA API must return JSON 401")
        require("WWW-Authenticate" not in unauthenticated.headers,
                "STA 401 must not advertise HTTP Basic")
        basic = base64.b64encode(b"admin:smoker257500").decode()
        basic_attempt = response(urllib.request.Request(
            f"{sta}/api/v1/snapshot", headers={"Authorization": f"Basic {basic}"}
        ))
        require(basic_attempt.status == 401, "Basic/admin must never grant access")
        bearer_attempt = response(urllib.request.Request(
            f"{sta}/api/v1/snapshot", headers={"Authorization": "Bearer stolen-token"}
        ))
        require(bearer_attempt.status == 401, "Bearer headers must never grant access")

        wrong_login = json_request(
            f"{sta}/api/v1/auth/session", "POST", b'{"password":"wrong-pass"}',
            {"Origin": sta},
        )
        require(wrong_login.status == 401, "wrong password session login must fail")
        login = json_request(
            f"{sta}/api/v1/auth/session", "POST", b'{"password":"smoker257500"}',
            {"Origin": sta},
        )
        require(login.status == 204, "password JSON login must return 204")
        cookie = login.headers["Set-Cookie"]
        require("HttpOnly" in cookie and "SameSite=Lax" in cookie and "Path=/" in cookie,
                "session cookie must retain required flags")
        token_cookie = cookie.split(";", 1)[0]
        dashboard = response(urllib.request.Request(f"{sta}/", headers={"Cookie": token_cookie}))
        require(dashboard.status == 200 and b"Controlul afum" in dashboard.read(),
                "cookie must unlock the STA dashboard")
        require(response(urllib.request.Request(
            f"{sta}/api/v1/snapshot", headers={"Cookie": token_cookie}
        )).status == 200, "cookie must unlock STA snapshot")

        firmware_missing_origin = response(urllib.request.Request(
            f"{sta}/api/v1/firmware", headers={"Cookie": token_cookie}
        ))
        require(firmware_missing_origin.status == 200,
                "same-origin browser-compatible firmware GET may omit Origin")
        firmware_foreign_origin = response(urllib.request.Request(
            f"{sta}/api/v1/firmware",
            headers={"Cookie": token_cookie, "Origin": "http://attacker.invalid"},
        ))
        require(firmware_foreign_origin.status == 403,
                "firmware status rejects a foreign Origin when one is present")
        firmware_status = response(urllib.request.Request(
            f"{sta}/api/v1/firmware",
            headers={"Cookie": token_cookie, "Origin": sta},
        ))
        require(firmware_status.status == 200
                and json.loads(firmware_status.read())["state"] == "IDLE",
                "authenticated exact-Origin STA may read firmware status")
        firmware_ap = json_request(
            f"{ap}/api/v1/firmware/check", "POST", None, {"Origin": ap}
        )
        require(firmware_ap.status == 403, "SoftAP must reject all firmware routes")
        firmware_check_body = json_request(
            f"{sta}/api/v1/firmware/check", "POST", b'{}',
            {"Cookie": token_cookie, "Origin": sta},
        )
        require(firmware_check_body.status == 400,
                "firmware check requires an empty body")
        firmware_check = json_request(
            f"{sta}/api/v1/firmware/check", "POST", None,
            {"Cookie": token_cookie, "Origin": sta},
        )
        require(firmware_check.status == 202,
                "manual firmware check accepts authenticated exact-Origin STA")
        invalid_firmware = json_request(
            f"{sta}/api/v1/firmware/install", "POST",
            b'{"version":"0.13.1","extra":true}',
            {"Cookie": token_cookie, "Origin": sta},
        )
        require(invalid_firmware.status == 400,
                "firmware install rejects unknown JSON fields")
        install_firmware = json_request(
            f"{sta}/api/v1/firmware/install", "POST",
            b'{"version":"0.13.1"}',
            {"Cookie": token_cookie, "Origin": sta},
        )
        require(install_firmware.status == 202,
                "firmware install accepts the exact available version while not RUNNING")

        replacement_login = json_request(
            f"{sta}/api/v1/auth/session", "POST", b'{"password":"smoker257500"}',
            {"Origin": sta},
        )
        replacement_cookie = replacement_login.headers["Set-Cookie"].split(";", 1)[0]
        require(response(urllib.request.Request(
            f"{sta}/api/v1/snapshot", headers={"Cookie": token_cookie}
        )).status == 401, "new login must invalidate the previous token")

        no_origin = json_request(
            f"{sta}/api/v1/network", "PUT",
            b'{"ssid":"Atelier","wifi_password":"parola-wifi"}',
            {"Cookie": replacement_cookie},
        )
        require(no_origin.status == 403, "authenticated writes require exact Origin")
        extra_network = json_request(
            f"{sta}/api/v1/network", "PUT",
            b'{"ssid":"Atelier","wifi_password":"parola-wifi","device_password":"ignored"}',
            {"Cookie": replacement_cookie, "Origin": sta},
        )
        require(extra_network.status == 400, "network update must reject device_password")
        network_update = json_request(
            f"{sta}/api/v1/network", "PUT",
            b'{"ssid":"Atelier","wifi_password":"parola-wifi"}',
            {"Cookie": replacement_cookie, "Origin": sta},
        )
        require(network_update.status == 202, "authenticated STA network update must succeed")

        wrong_current = json_request(
            f"{sta}/api/v1/auth/password", "PUT",
            b'{"current_password":"wrong-pass","new_password":"parola-http-noua"}',
            {"Cookie": replacement_cookie, "Origin": sta},
        )
        require(wrong_current.status == 403, "password change requires current password")
        changed = json_request(
            f"{sta}/api/v1/auth/password", "PUT",
            b'{"current_password":"smoker257500","new_password":"parola-http-noua"}',
            {"Cookie": replacement_cookie, "Origin": sta},
        )
        require(changed.status == 204 and "Max-Age=0" in changed.headers["Set-Cookie"],
                "password change must clear the session cookie")
        require(response(urllib.request.Request(
            f"{sta}/api/v1/snapshot", headers={"Cookie": replacement_cookie}
        )).status == 401, "password change must invalidate the server-side token")

        relogin = json_request(
            f"{sta}/api/v1/auth/session", "POST", b'{"password":"parola-http-noua"}',
            {"Origin": sta},
        )
        new_cookie = relogin.headers["Set-Cookie"].split(";", 1)[0]
        logout = json_request(f"{sta}/api/v1/auth/session", "DELETE", None,
                              {"Cookie": new_cookie, "Origin": sta})
        require(logout.status == 204 and "Max-Age=0" in logout.headers["Set-Cookie"],
                "logout must clear cookie and invalidate token")
        require(response(urllib.request.Request(
            f"{sta}/api/v1/snapshot", headers={"Cookie": new_cookie}
        )).status == 401, "logged-out token must be rejected")

        print("M12/M13 commissioning, session, and firmware HTTP fixture: PASS")
        return 0
    finally:
        process.terminate()
        try: process.wait(timeout=3)
        except subprocess.TimeoutExpired: process.kill()
        temporary.cleanup()


if __name__ == "__main__":
    raise SystemExit(main())
