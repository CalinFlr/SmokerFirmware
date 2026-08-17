#!/usr/bin/env python3
"""Serve isolated commissioning (AP) and operational (STA) M12 fixtures."""

from __future__ import annotations

import argparse
import json
import re
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs


ROOT = Path(__file__).resolve().parents[1]
ASSETS = (ROOT / "components/smoker_platform/src/web_assets.hpp").read_text()


def embedded(name: str, delimiter: str) -> bytes:
    match = re.search(
        rf'inline constexpr std::string_view {name} = R"{delimiter}\((.*?)\){delimiter}";',
        ASSETS,
        re.DOTALL,
    )
    if match is None:
        raise RuntimeError(f"embedded asset {name} not found")
    return match.group(1).encode()


INDEX = embedded("index_html", "HTML")
APP_CSS = embedded("app_css", "CSS")
APP_JS = embedded("app_js", "JS")
LOGIN = embedded("login_html", "LOGIN")
LOGIN_ERROR = embedded("login_error_html", "LOGIN")
LOGIN_LIMITED = embedded("login_rate_limited_html", "LOGIN")
LOGIN_CSS = embedded("login_css", "CSS")
LOGIN_JS = embedded("login_js", "JS")
SETUP = embedded("setup_html", "SETUP")
SETUP_CSS = embedded("setup_css", "CSS")
SETUP_JS = embedded("setup_js", "JS")

device_password = "smoker257500"
session_token: str | None = None
session_serial = 1
state_lock = threading.Lock()
login_failures: dict[str, tuple[int, float]] = {}
scan_reads = {"commissioning": 0, "operational": 0}
snapshot_requests = 0
maximum_snapshot_requests = 0
next_command_id = 1
firmware = {
    "state": "IDLE", "current_version": "0.13.0", "available_version": None,
    "progress_percent": 0, "installation_allowed": False, "error": None,
}
firmware_reads = 0

snapshot = {
    "session": {"status": "IDLE", "id": None, "stop_reason": "NONE"},
    "chamber": {"current_celsius": 27.5, "target_celsius": None},
    "heater": {"demand_percent": 0.0, "io": "SIMULATED"},
    "timer": {"started": False, "completed": False, "elapsed_ms": 0},
    "probes": [
        {"id": 1, "name": "Cotlet A", "role": "MEAT", "current_celsius": 24.8,
         "target_celsius": 63.0, "enabled": True, "alarm_enabled": True},
        {"id": 2, "name": "Piept B", "role": "MEAT", "current_celsius": 23.9,
         "target_celsius": 72.0, "enabled": True, "alarm_enabled": False},
    ],
    "alarms": [], "fault": None,
    "limits": {"maximum_chamber_celsius": 150.0},
    "counters": {"application_command_overflow": 0,
                 "transport_command_overflow": 0, "snapshot_publish_dropped": 0},
    "command_results": [], "firmware_update_active": False, "simulated_io": True,
}
network = {
    "hostname": "smoker-a1b2c3", "default_password_warning": True,
    "sta": {"configured": True, "connected": True, "ssid": "Fumuri Acasă",
            "ip": "192.168.1.42", "last_error": None},
    "ap": {"active": False, "ssid": "Smoker-A1B2C3", "ip": "192.168.4.1"},
}


class FixtureServer(ThreadingHTTPServer):
    def __init__(self, address: tuple[str, int], scope: str):
        super().__init__(address, Handler)
        self.scope = scope


class Handler(BaseHTTPRequestHandler):
    server: FixtureServer

    def log_message(self, format: str, *args: object) -> None:
        return

    def send_bytes(self, content: bytes, content_type: str, status: int = 200,
                   headers: dict[str, str] | None = None) -> None:
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("Referrer-Policy", "no-referrer")
        self.send_header("X-Frame-Options", "DENY")
        self.send_header("Permissions-Policy", "camera=(), microphone=(), geolocation=()")
        self.send_header(
            "Content-Security-Policy",
            "default-src 'self'; script-src 'self'; style-src 'self'; connect-src 'self'; "
            "img-src 'self' data:; object-src 'none'; base-uri 'none'; "
            "frame-ancestors 'none'; form-action 'self'",
        )
        for name, value in (headers or {}).items():
            self.send_header(name, value)
        self.end_headers()
        self.wfile.write(content)

    def send_json(self, value: object, status: int = 200,
                  headers: dict[str, str] | None = None) -> None:
        self.send_bytes(json.dumps(value).encode(), "application/json", status, headers)

    def content_type_matches(self, expected: str) -> bool:
        media, separator, parameters = self.headers.get("Content-Type", "").partition(";")
        return media.strip().lower() == expected and (not separator or bool(parameters.strip()))

    def host_allowed(self) -> bool:
        host = self.headers.get("Host", "").split(":", 1)[0].lower()
        return host in {"127.0.0.1", "localhost"}

    def origin_allowed(self) -> bool:
        if self.command not in {"POST", "PUT", "DELETE"}:
            return True
        return self.headers.get("Origin") == f"http://{self.headers.get('Host')}"

    def login_permitted(self) -> bool:
        with state_lock:
            _, blocked_until = login_failures.get(self.client_address[0], (0, 0.0))
        return time.monotonic() >= blocked_until

    def record_login_failure(self) -> None:
        with state_lock:
            failures, _ = login_failures.get(self.client_address[0], (0, 0.0))
            failures += 1
            login_failures[self.client_address[0]] = (
                failures, time.monotonic() + 30.0 if failures >= 5 else 0.0
            )

    def clear_login_failures(self) -> None:
        with state_lock:
            login_failures.pop(self.client_address[0], None)

    def authenticated(self) -> bool:
        with state_lock:
            current = session_token
        return current is not None and f"smoker_session={current}" in self.headers.get("Cookie", "")

    def require_operational_auth(self) -> bool:
        if self.authenticated():
            return True
        if self.path.startswith("/api/"):
            self.send_json({"error": "autentificare necesară"}, 401)
        else:
            self.send_bytes(b"Autentificare necesara.", "text/plain", 303,
                            {"Location": "/login"})
        return False

    def read_json(self) -> dict[str, object] | None:
        if not self.content_type_matches("application/json"):
            return None
        try:
            value = json.loads(self.rfile.read(int(self.headers.get("Content-Length", "0"))))
        except (json.JSONDecodeError, UnicodeDecodeError):
            return None
        return value if isinstance(value, dict) else None

    def reject_wrong_scope(self) -> None:
        self.send_json({"error": "ruta nu este disponibilă prin SoftAP"}, 403)

    def do_GET(self) -> None:
        global snapshot_requests, maximum_snapshot_requests, firmware_reads
        if not self.host_allowed():
            if self.server.scope == "commissioning" and not self.path.startswith("/api/"):
                self.send_bytes(b"Configurare Wi-Fi Fumuri.", "text/plain", 302,
                                {"Location": "http://192.168.4.1/"})
            else:
                self.send_json({"error": "gazdă HTTP respinsă"}, 421)
            return
        if self.server.scope == "commissioning":
            if self.path == "/": self.send_bytes(SETUP, "text/html; charset=utf-8")
            elif self.path == "/setup.css": self.send_bytes(SETUP_CSS, "text/css; charset=utf-8")
            elif self.path == "/setup.js": self.send_bytes(SETUP_JS, "application/javascript")
            elif self.path == "/api/v1/setup/status":
                self.send_json({"mode": "commissioning", "ap_ssid": "Smoker-A1B2C3",
                                "hostname": "smoker-a1b2c3", "sta_configured": True,
                                "sta_connected": False, "last_error": None})
            elif self.path == "/api/v1/network/scan": self.send_scan()
            elif self.path in {"/login", "/login.css", "/login.js", "/app.css", "/app.js"}:
                self.reject_wrong_scope()
            elif self.path.startswith("/api/"): self.reject_wrong_scope()
            else:
                self.send_bytes(b"Configurare Wi-Fi Fumuri.", "text/plain", 302,
                                {"Location": "http://192.168.4.1/"})
            return

        if self.path == "/login":
            if not self.login_permitted():
                self.send_bytes(LOGIN_LIMITED, "text/html; charset=utf-8", 429,
                                {"Retry-After": "30"})
            else:
                self.send_bytes(LOGIN, "text/html; charset=utf-8")
            return
        if self.path == "/login.css": self.send_bytes(LOGIN_CSS, "text/css; charset=utf-8"); return
        if self.path == "/login.js": self.send_bytes(LOGIN_JS, "application/javascript"); return
        if not self.require_operational_auth(): return
        if self.path == "/": self.send_bytes(INDEX, "text/html; charset=utf-8")
        elif self.path == "/app.css": self.send_bytes(APP_CSS, "text/css; charset=utf-8")
        elif self.path == "/app.js": self.send_bytes(APP_JS, "application/javascript")
        elif self.path == "/api/v1/snapshot":
            with state_lock:
                snapshot_requests += 1
                maximum_snapshot_requests = max(maximum_snapshot_requests, snapshot_requests)
            try:
                time.sleep(1.4)
                with state_lock:
                    snapshot["probes"][0]["current_celsius"] += 0.1
                    value = json.loads(json.dumps(snapshot))
                self.send_json(value)
            finally:
                with state_lock: snapshot_requests -= 1
        elif self.path == "/api/v1/network": self.send_json(network)
        elif self.path == "/api/v1/network/scan": self.send_scan()
        elif self.path == "/api/v1/firmware":
            if self.headers.get("Origin") not in {
                    None, f"http://{self.headers.get('Host')}"}:
                self.send_json({"error": "origine firmware respinsă"}, 403)
            else:
                with state_lock:
                    if firmware["state"] == "INSTALLING":
                        firmware_reads += 1
                        if firmware_reads >= 4:
                            firmware.update({"state": "FAILED", "progress_percent": 0,
                                             "installation_allowed": False,
                                             "error": "fixture_download_failed"})
                            snapshot["firmware_update_active"] = False
                    value = dict(firmware)
                    value["installation_allowed"] = value["state"] == "AVAILABLE" \
                        and snapshot["session"]["status"] != "RUNNING"
                self.send_json(value)
        elif self.path == "/fixture/metrics":
            with state_lock: self.send_json({"maximum_snapshot_requests": maximum_snapshot_requests})
        else: self.send_bytes(b"Autentificare necesara.", "text/plain", 303, {"Location": "/login"})

    def send_scan(self) -> None:
        scan_reads[self.server.scope] += 1
        if scan_reads[self.server.scope] < 2:
            self.send_json({"state": "scanning", "networks": [], "truncated": False, "error": None})
            return
        self.send_json({"state": "complete", "networks": [
            {"ssid": "Fumuri Acasă", "rssi_dbm": -42, "channel": 6,
             "security": "WPA2", "supported": True},
            {"ssid": "Atelier", "rssi_dbm": -68, "channel": 11,
             "security": "WPA3", "supported": True},
            {"ssid": "Oaspeți", "rssi_dbm": -79, "channel": 1,
             "security": "OPEN", "supported": False},
            {"ssid": "Legacy WEP", "rssi_dbm": -80, "channel": 3,
             "security": "WEP", "supported": False},
        ], "truncated": False, "error": None})

    def do_POST(self) -> None:
        global session_token, session_serial, scan_reads, next_command_id, firmware_reads
        if not self.host_allowed(): self.send_json({"error": "gazdă HTTP respinsă"}, 421); return
        if self.server.scope == "commissioning":
            if not self.origin_allowed(): self.send_json({"error": "origine respinsă"}, 403); return
            if self.path == "/api/v1/network/scan":
                scan_reads["commissioning"] = 0
                self.send_json({"status": "accepted", "combined": False}, 202)
            else: self.reject_wrong_scope()
            return
        if self.path == "/login":
            if not self.content_type_matches("application/x-www-form-urlencoded"):
                self.send_bytes(LOGIN_ERROR, "text/html; charset=utf-8", 401); return
            try: values = parse_qs(self.rfile.read(int(self.headers.get("Content-Length", "0"))).decode(), strict_parsing=True)
            except ValueError: values = {}
            if values == {"password": [device_password]}:
                self.clear_login_failures()
                session_serial += 1
                session_token = f"{session_serial:064x}"
                self.send_bytes(b"Autentificare reusita.", "text/plain", 303,
                                {"Location": "/", "Set-Cookie": f"smoker_session={session_token}; Path=/; HttpOnly; SameSite=Lax"})
            else:
                self.record_login_failure(); self.send_bytes(LOGIN_ERROR, "text/html; charset=utf-8", 401)
            return
        if not self.origin_allowed(): self.send_json({"error": "origine respinsă"}, 403); return
        if self.path == "/api/v1/auth/session":
            if not self.login_permitted():
                self.send_json({"error": "autentificare limitată temporar"}, 429, {"Retry-After": "30"}); return
            body = self.read_json()
            if body is None or set(body) != {"password"} or body["password"] != device_password:
                self.record_login_failure(); self.send_json({"error": "parolă invalidă"}, 401); return
            self.clear_login_failures(); session_serial += 1; session_token = f"{session_serial:064x}"
            self.send_bytes(b"", "application/json", 204,
                            {"Set-Cookie": f"smoker_session={session_token}; Path=/; HttpOnly; SameSite=Lax"})
            return
        if not self.require_operational_auth(): return
        if self.path == "/api/v1/firmware/check":
            if int(self.headers.get("Content-Length", "0")) != 0:
                self.send_json({"error": "verificarea nu acceptă body"}, 400); return
            with state_lock:
                firmware.update({"state": "AVAILABLE", "available_version": "0.13.1",
                                 "progress_percent": 0, "installation_allowed": True,
                                 "error": None})
            self.send_json({"status": "accepted"}, 202); return
        if self.path == "/api/v1/firmware/install":
            body = self.read_json()
            if body is None or set(body) != {"version"} \
                    or not isinstance(body["version"], str) \
                    or re.fullmatch(r"(?:0|[1-9]\d*)\.(?:0|[1-9]\d*)\.(?:0|[1-9]\d*)", body["version"]) is None:
                self.send_json({"error": "schemă firmware invalidă"}, 400); return
            with state_lock:
                if snapshot["session"]["status"] == "RUNNING":
                    self.send_json({"error": "opriți sesiunea înainte de instalare"}, 409); return
                if firmware["state"] != "AVAILABLE" or body["version"] != firmware["available_version"]:
                    self.send_json({"error": "instalarea nu este disponibilă"}, 409); return
                command_id = next_command_id; next_command_id += 1
                firmware.update({"state": "INSTALLING", "progress_percent": 42,
                                 "installation_allowed": False})
                firmware_reads = 0
                snapshot["firmware_update_active"] = True
            self.send_json({"status": "accepted", "coalesced_stop": False,
                            "command_id": command_id}, 202); return
        if self.path == "/api/v1/network/scan":
            scan_reads["operational"] = 0; self.send_json({"status": "accepted", "combined": False}, 202); return
        if self.path != "/api/v1/commands": self.send_json({"error": "not_found"}, 404); return
        body = self.read_json()
        if body is None or "type" not in body: self.send_json({"error": "schemă JSON invalidă"}, 400); return
        command_id = next_command_id; next_command_id += 1; accepted = True
        with state_lock:
            kind = body["type"]
            if kind == "start_session":
                accepted = snapshot["session"]["status"] not in {"RUNNING", "FAULT"}
                if accepted:
                    snapshot["session"] = {"status": "RUNNING", "id": 1, "stop_reason": "NONE"}
                    snapshot["chamber"]["target_celsius"] = body["target_celsius"]
                    snapshot["heater"]["demand_percent"] = 100.0
            elif kind == "stop_session":
                accepted = snapshot["session"]["status"] == "RUNNING"
                if accepted:
                    snapshot["session"] = {"status": "STOPPED", "id": 1, "stop_reason": "USER"}
                    snapshot["heater"]["demand_percent"] = 0.0
            elif kind == "set_chamber_target":
                accepted = snapshot["session"]["status"] == "RUNNING"
                if accepted: snapshot["chamber"]["target_celsius"] = body["target_celsius"]
            elif kind.startswith("set_probe_"):
                probe = next((value for value in snapshot["probes"] if value["id"] == body["probe_id"]), None)
                accepted = probe is not None and snapshot["session"]["status"] == "RUNNING"
                if accepted:
                    if kind == "set_probe_target": probe["target_celsius"] = body["target_celsius"]
                    elif kind == "set_probe_enabled": probe["enabled"] = body["enabled"]
                    elif kind == "set_probe_alarm_enabled": probe["alarm_enabled"] = body["enabled"]
            snapshot["command_results"].append({"id": command_id, "semantic_accepted": accepted})
            snapshot["command_results"] = snapshot["command_results"][-16:]
        self.send_json({"status": "accepted", "coalesced_stop": False, "command_id": command_id}, 202)

    def do_PUT(self) -> None:
        global device_password, session_token
        if not self.host_allowed(): self.send_json({"error": "gazdă HTTP respinsă"}, 421); return
        if not self.origin_allowed(): self.send_json({"error": "origine respinsă"}, 403); return
        if self.server.scope == "commissioning":
            if self.path != "/api/v1/setup/network": self.reject_wrong_scope(); return
            body = self.read_json()
            if not valid_network_body(body): self.send_json({"error": "schemă JSON invalidă"}, 400); return
            self.send_json({"status": "accepted", "reconnecting": True}, 202); return
        if not self.require_operational_auth(): return
        body = self.read_json()
        if self.path == "/api/v1/network":
            if not valid_network_body(body): self.send_json({"error": "schemă JSON invalidă"}, 400); return
            network["sta"].update({"configured": True, "connected": True,
                                   "ssid": body["ssid"], "ip": "192.168.1.42", "last_error": None})
            self.send_json({"status": "accepted", "reconnecting": True}, 202); return
        if self.path == "/api/v1/auth/password":
            if body is None or set(body) != {"current_password", "new_password"}:
                self.send_json({"error": "schemă JSON invalidă"}, 400); return
            replacement = body["new_password"]
            if body["current_password"] != device_password:
                self.send_json({"error": "parola curentă este invalidă"}, 403); return
            if not isinstance(replacement, str) or not 8 <= len(replacement) <= 63:
                self.send_json({"error": "lungime parolă invalidă"}, 400); return
            device_password = replacement; session_token = None; network["default_password_warning"] = False
            self.send_bytes(b"", "application/json", 204,
                            {"Set-Cookie": "smoker_session=; Path=/; HttpOnly; SameSite=Lax; Max-Age=0"}); return
        self.send_json({"error": "not_found"}, 404)

    def do_DELETE(self) -> None:
        global session_token
        if self.server.scope != "operational": self.reject_wrong_scope(); return
        if not self.host_allowed(): self.send_json({"error": "gazdă HTTP respinsă"}, 421); return
        if not self.require_operational_auth(): return
        if not self.origin_allowed(): self.send_json({"error": "origine respinsă"}, 403); return
        if self.path != "/api/v1/auth/session": self.send_json({"error": "not_found"}, 404); return
        session_token = None
        self.send_bytes(b"", "application/json", 204,
                        {"Set-Cookie": "smoker_session=; Path=/; HttpOnly; SameSite=Lax; Max-Age=0"})


def valid_network_body(body: dict[str, object] | None) -> bool:
    return body is not None and set(body) == {"ssid", "wifi_password"} \
        and isinstance(body["ssid"], str) and 1 <= len(body["ssid"]) <= 32 \
        and isinstance(body["wifi_password"], str) and 8 <= len(body["wifi_password"]) <= 63


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=0)
    parser.add_argument("--ready-file", type=Path)
    arguments = parser.parse_args()
    ap_server = FixtureServer(("127.0.0.1", arguments.port), "commissioning")
    sta_server = FixtureServer(("127.0.0.1", 0), "operational")
    threading.Thread(target=sta_server.serve_forever, daemon=True).start()
    if arguments.ready_file is not None:
        arguments.ready_file.write_text(json.dumps({
            "commissioning": ap_server.server_address[1],
            "operational": sta_server.server_address[1],
        }))
    try:
        ap_server.serve_forever()
    finally:
        sta_server.shutdown()
