#!/usr/bin/env python3
"""Explicit-runtime/portable-package validation using ONLY owned loopback fixtures.
All profiles, destinations, credentials and evidence remain under build/qt-service.
"""
import argparse
import base64
import hashlib
import http.server
import json
import os
from pathlib import Path
import socket
import signal
import shutil
import struct
import ssl
import time
import subprocess
import tempfile
import threading
import urllib.error
import urllib.request
import runtime
from smoke import Client, wait_for


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=Path)
    parser.add_argument("--native-ui", action="store_true", help="Launch/close only the owned private-state native Qt child; not manual acceptance")
    args = parser.parse_args()
    os.umask(0o077)
    os.environ["JAVA_HOME"] = "/usr/lib/jvm/java-25-openjdk"
    base = runtime.REPO / "build/qt-service"
    base.mkdir(parents=True, exist_ok=True)
    evidence = Path(tempfile.mkdtemp(prefix="runtime-smoke-", dir=base))
    destination = evidence / "downloads"; destination.mkdir()
    profile = evidence / "profile"
    policy_file = runtime.initialize(profile, destination)
    policy = json.loads(policy_file.read_text())
    # Never the real browser port 15151: an installed app or the user's extension may own it.
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0)); api_port = probe.getsockname()[1]
    settings_file = profile / "app-data/config/appSettings.json"
    settings_file.parent.mkdir(mode=0o700, parents=True)
    settings_file.write_text(json.dumps({"apiEnabled": True, "apiPort": api_port}))
    payload = hashlib.sha256(b"explicit runtime fixture").digest() * 32768
    cross_origin_headers = []
    basic = "Basic " + base64.b64encode(b"fixture-user:fixture-password").decode()
    proxy_auth = "Basic " + base64.b64encode(b"proxy-user:proxy-password").decode()
    proxy_requests = []
    checks = []
    # Disposable CA material, never installed into the system/JVM trust store.
    for name in ("trusted", "untrusted"):
        subprocess.run(["openssl", "req", "-x509", "-nodes", "-newkey", "rsa:2048", "-subj", "/CN=localhost",
                        "-addext", "subjectAltName=IP:127.0.0.1,DNS:localhost", "-days", "1",
                        "-keyout", str(evidence / f"{name}-key.pem"), "-out", str(evidence / f"{name}-cert.pem")],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True, timeout=20)
    truststore = evidence / "fixture-trust.jks"
    subprocess.run(["/usr/lib/jvm/java-25-openjdk/bin/keytool", "-importcert", "-noprompt", "-alias", "fixture",
                    "-file", str(evidence / "trusted-cert.pem"), "-keystore", str(truststore), "-storetype", "JKS",
                    "-storepass:env", "ABDM_FIXTURE_STOREPASS"], env={**os.environ, "ABDM_FIXTURE_STOREPASS": "local-fixture-only"},
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True, timeout=20)
    os.environ["JAVA_OPTS"] = os.environ.get("JAVA_OPTS", "") + f" -Djavax.net.ssl.trustStore={truststore} -Djavax.net.ssl.trustStoreType=JKS"

    class Target(http.server.BaseHTTPRequestHandler):
        def log_message(self, *_): pass
        def do_GET(self):
            cross_origin_headers.append(dict(self.headers))
            begin, end = 0, len(payload) - 1
            if self.headers.get("Range"):
                start, stop = self.headers["Range"].removeprefix("bytes=").split("-")
                begin = int(start); end = min(int(stop) if stop else end, end)
                self.send_response(206)
                self.send_header("Content-Range", f"bytes {begin}-{end}/{len(payload)}")
            else: self.send_response(200)
            self.send_header("Content-Length", str(end - begin + 1)); self.send_header("Accept-Ranges", "bytes")
            self.send_header("ETag", '"runtime-v1"'); self.end_headers()
            try: self.wfile.write(payload[begin:end + 1])
            except (BrokenPipeError, ConnectionResetError): pass

    target = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Target)
    tls_servers = []
    for name in ("trusted", "untrusted"):
        tls = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Target)
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(evidence / f"{name}-cert.pem", evidence / f"{name}-key.pem")
        tls.socket = context.wrap_socket(tls.socket, server_side=True)
        tls_servers.append(tls)
    class Origin(http.server.BaseHTTPRequestHandler):
        def log_message(self, *_): pass
        def do_GET(self):
            if self.headers.get("Authorization") != basic:
                self.send_response(401); self.send_header("Content-Length", "0"); self.end_headers(); return
            self.send_response(302)
            self.send_header("Location", f"http://127.0.0.1:{target.server_port}/file")
            self.send_header("Content-Length", "0"); self.end_headers()
    origin = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Origin)
    class Proxy(http.server.BaseHTTPRequestHandler):
        def log_message(self, *_): pass
        def do_GET(self):
            proxy_requests.append(self.path)
            if self.headers.get("Proxy-Authorization") != proxy_auth:
                self.send_response(407); self.send_header("Proxy-Authenticate", 'Basic realm="fixture"'); self.send_header("Content-Length", "0"); self.end_headers(); return
            allowed = (f"http://127.0.0.1:{origin.server_port}/", f"http://127.0.0.1:{target.server_port}/")
            if not self.path.startswith(allowed):
                self.send_error(403); return
            request = urllib.request.Request(self.path, headers={key: value for key, value in self.headers.items() if key.lower() not in ("proxy-authorization", "host", "connection")})
            # No automatic proxy/redirect discovery in the owned forwarding fixture.
            class NoRedirect(urllib.request.HTTPRedirectHandler):
                def redirect_request(self, *_): return None
            opener = urllib.request.build_opener(urllib.request.ProxyHandler({}), NoRedirect())
            try: response = opener.open(request, timeout=10)
            except urllib.error.HTTPError as error: response = error
            with response:
                self.send_response(response.code)
                for key, value in response.headers.items():
                    if key.lower() not in ("connection", "server", "date", "transfer-encoding"): self.send_header(key, value)
                self.end_headers()
                try: self.wfile.write(response.read())
                except (BrokenPipeError, ConnectionResetError): pass
    proxy = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Proxy)
    for server in (target, origin, proxy, *tls_servers):
        assert server.server_port != 15151
        threading.Thread(target=server.serve_forever, daemon=True).start()
    service = args.package / "lib/service/bin/service" if args.package else runtime.SERVICE
    package_cli = args.package / "bin/abdm-qt" if args.package else None
    client = None
    def start():
        if package_cli:
            result = subprocess.run([str(package_cli), "start", "--runtime-policy", str(policy_file)], cwd=evidence, capture_output=True, timeout=60)
            assert result.returncode == 0, "Packaged launcher failed; inspect private log"
            return runtime.available(profile)
        return runtime.ensure_service(profile, ["--runtime-policy", str(policy_file)], service)
    try:
        policy["proxy"] = {"type": "http", "host": "127.0.0.1", "port": proxy.server_port, "username": "proxy-user", "password": "proxy-password"}
        policy_file.write_text(json.dumps(policy))
        assert start()
        first_descriptor = (profile / "qt-service.json").read_bytes()
        assert not runtime.ensure_service(profile, ["--runtime-policy", str(policy_file)], service)
        assert (profile / "qt-service.json").read_bytes() == first_descriptor
        checks.append("cold launch and attach reuse one authenticated service without replacing epoch")
        client = Client(profile, "runtime-smoke")
        assert client.snapshot()["runtime"]["mode"] == "explicit-runtime"
        url = f"http://127.0.0.1:{origin.server_port}/redirect"
        add = client.command("add", url=url, name="authenticated.bin", start=True, connections=2,
                             credentials={"username": "fixture-user", "password": "fixture-password", "headers": {"Cookie": "fixture-cookie", "X-Secret": "private-header"}})
        assert add["ok"], add
        download_id = add["id"]
        wait_for(lambda: next((r for r in client.snapshot()["rows"] if r["id"] == download_id and r["state"] == "completed"), None), 30)
        assert hashlib.sha256((destination / "authenticated.bin").read_bytes()).digest() == hashlib.sha256(payload).digest()
        assert proxy_requests and cross_origin_headers
        for headers in cross_origin_headers:
            assert not {key.lower() for key in headers} & {"authorization", "cookie", "x-secret", "proxy-authorization"}
        assert "fixture-password" not in json.dumps(client.snapshot())
        checks.append("original engine transfer via authenticated HTTP proxy and source Basic auth; cross-origin redirects strip custom secrets; exact checksum")
        saved = client.command("add", url="https://example.invalid/not-fetched", name="edited.bin", start=False)
        assert saved["ok"]
        edited = client.command("download.edit", id=saved["id"], url=url, connections=1, speedLimit="0",
                                credentials={"username": "fixture-user", "password": "fixture-password"})
        assert edited["ok"]
        assert not client.command("download.edit", id=saved["id"], url=url, connections=0)["ok"]
        assert not client.command("add", url=url, name="escape.bin", folder=str(evidence))["ok"]
        assert not client.command("add", url="http://127.0.0.1:15151/never", name="forbidden.bin")["ok"]
        checks.append("HTTP(S) saved without fetching, paused source/options edit, invalid full-draft/escaping destination/15151 rejection")
        draining = client.command("add", url=f"http://127.0.0.1:{target.server_port}/file", name="drained.bin", start=True, connections=1, speedLimit="65536")
        assert draining["ok"]
        partial = wait_for(lambda: next((r for r in client.snapshot()["rows"] if r["id"] == draining["id"] and 0 < int(r["downloaded"]) < len(payload)), None), 20)
        client.close(); client = None
        runtime.stop(profile)
        assert runtime.lease_free(profile) and not (profile / "qt-service.json").exists()
        # Change the explicit proxy only while stopped; TLS uses the normal direct engine.
        policy["proxy"] = {"type": "direct"}
        policy_file.write_text(json.dumps(policy))
        # A stale publication does not authorize killing any PID or unlinking a lock.
        (profile / "qt-service.json").write_bytes(first_descriptor)
        assert start()
        assert (profile / "qt-service.json").read_bytes() != first_descriptor
        client = Client(profile, "runtime-restarted")
        secure = client.command("add", url=f"https://127.0.0.1:{tls_servers[0].server_port}/file", name="trusted-tls.bin", start=True)
        assert secure["ok"]
        wait_for(lambda: next((r for r in client.snapshot()["rows"] if r["id"] == secure["id"] and r["state"] == "completed"), None), 30)
        assert (destination / "trusted-tls.bin").read_bytes() == payload
        rejected = client.command("add", url=f"https://127.0.0.1:{tls_servers[1].server_port}/file", name="untrusted-tls.bin", start=True)
        assert rejected["ok"]
        wait_for(lambda: next((r for r in client.snapshot()["rows"] if r["id"] == rejected["id"] and r["state"] == "failed"), None), 20)
        assert not (destination / "untrusted-tls.bin").exists()
        checks.append("owned HTTPS fixture completes with isolated explicit truststore; untrusted TLS rejects without payload, no certificate/hostname validation bypass or system trust changes")
        drained = next(r for r in client.snapshot()["rows"] if r["id"] == draining["id"])
        assert int(drained["downloaded"]) >= int(partial["downloaded"]) and drained["state"] != "downloading"
        assert client.command("download.edit", id=draining["id"], speedLimit="0")["ok"]
        assert client.command("resume", id=draining["id"])["ok"]
        wait_for(lambda: next((r for r in client.snapshot()["rows"] if r["id"] == draining["id"] and r["state"] == "completed"), None), 30)
        assert (destination / "drained.bin").read_bytes() == payload
        checks.append("authenticated shutdown while a real transfer is active drains and saves progress, then explicit restart/resume preserves exact payload")
        row = next(r for r in client.snapshot()["rows"] if r["id"] == saved["id"])
        assert row["details"]["url"] == url and row["details"]["connections"] == 1
        assert row["state"] != "downloading"
        assert client.command("resume", id=saved["id"])["ok"]
        wait_for(lambda: next((r for r in client.snapshot()["rows"] if r["id"] == saved["id"] and r["state"] == "completed"), None), 30)
        assert (destination / "edited.bin").read_bytes() == payload
        checks.append("authenticated joined stop releases lease; stale-descriptor recovery rotates epoch without automatic resume/replay; persisted original credentials/options resume to exact checksum")
        # Browser extension contract: the original HTTP API and native-messaging host.
        wait_for(lambda: client.snapshot()["browserIntegration"]["state"] != "starting", 15)
        status = client.snapshot()["browserIntegration"]
        assert status == {"state": "listening", "error": None, "occupant": None, "retrying": False,
                          "enabled": True, "port": api_port, "auth": False}, status
        opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
        def api(path, body, origin="moz-extension://abdm-runtime-smoke"):
            request = urllib.request.Request(f"http://localhost:{api_port}/{path}", data=json.dumps(body).encode(), method="POST", headers={"Origin": origin})
            with opener.open(request, timeout=10) as response:
                return response.read().decode()
        assert api("ping", None) == "pong"
        link = f"http://127.0.0.1:{target.server_port}/file"
        browser_add = {"items": [{"type": "http", "link": link, "suggestedName": "browser.bin", "downloadPage": None, "description": None,
                                  "headers": {"Cookie": "browser-cookie", "Host": "ignored", "User-Agent": "RuntimeSmoke/1"}}],
                       "options": {"silentAdd": False, "silentStart": False}}
        try:
            api("add", browser_add, origin="https://attacker.invalid"); raise AssertionError("web origin accepted")
        except urllib.error.HTTPError as error:
            assert error.code == 403
        frontend = Client(profile, "runtime-smoke-frontend", role="frontend")
        try:
            assert frontend.snapshot()["browserRequests"] == []  # A connected frontend: no window is launched.
            assert api("add", browser_add) == "OK"
            pending = frontend.snapshot()["browserRequests"]
            assert len(pending) == 1 and pending[0]["items"][0]["name"] == "browser.bin" and pending[0]["items"][0]["hasHeaders"]
            assert "browser-cookie" not in json.dumps(frontend.snapshot())
            before = len(cross_origin_headers)
            accepted = frontend.command("browser.accept", requestId=pending[0]["id"], start=True, categoryId="none",
                                        items=[{"index": 0, "url": link, "name": "browser.bin"}])
            assert accepted["ok"], accepted
            wait_for(lambda: next((r for r in frontend.snapshot()["rows"] if r["id"] == accepted["id"] and r["state"] == "completed"), None), 30)
            assert (destination / "browser.bin").read_bytes() == payload and frontend.snapshot()["browserRequests"] == []
            # Progress/completion window hints for exactly the accepted stable ID; presentation only.
            hints = [(e["kind"], e["id"]) for e in frontend.snapshot()["downloadWindows"]["events"]]
            assert ("started", accepted["id"]) in hints and ("completed", accepted["id"]) in hints, hints
            assert accepted["ids"] == [accepted["id"]]
            integration = frontend.snapshot()["integration"]
            assert integration["showDownloadProgressDialog"] is True and integration["showDownloadCompletionDialog"] is True
            sent = [h for h in cross_origin_headers[before:] if h.get("Cookie") == "browser-cookie"]
            assert sent and all(h.get("User-Agent") == "RuntimeSmoke/1" for h in sent)
        finally:
            frontend.close()
        checks.append("browser HTTP API (original endpoints, loopback, web-origin refusal): GUI request waits for the frontend, cookies stay service-side, accepted download sends captured Cookie/User-Agent to exact checksum and publishes started/completed window hints for the accepted ID")
        cli = [str(package_cli)] if package_cli else ["python3", str(runtime.HERE / "runtime.py")]
        def native(*messages):
            data = b"".join(struct.pack("=I", len(m)) + m for m in (json.dumps(m).encode() for m in messages))
            # Browsers append the manifest path and extension ID; stdout must be protocol-only.
            result = subprocess.run([*cli, "native-host", "--runtime-policy", str(policy_file), str(evidence / "manifest.json"),
                                     "firefox-integration@abdownloadmanager.com"], input=data, capture_output=True, cwd=evidence, timeout=90)
            out, replies = result.stdout, []
            while out:
                size = struct.unpack("=I", out[:4])[0]; replies.append(json.loads(out[4:4 + size])); out = out[4 + size:]
            assert result.returncode == 0, result.stderr[-400:]
            return replies
        def message(message_id, action, value):
            return {"id": message_id, "content": {"action": action, "isError": False, "payload": json.dumps(value)}}
        silent = lambda name: {"items": [{"type": "http", "link": f"{link}?native={name}", "suggestedName": name}], "options": {"silentAdd": True, "silentStart": True}}
        replies = native(message("B_ping", "ping", None), message("B_add", "add", silent("native.bin")))
        assert [(r["id"], r["content"]["payload"], r["content"]["isError"]) for r in replies] == [("B_ping", "true", False), ("B_add", "true", False)], replies
        wait_for(lambda: next((r for r in client.snapshot()["rows"] if r["name"] == "native.bin" and r["state"] == "completed"), None), 30)
        assert (destination / "native.bin").read_bytes() == payload
        client.close(); client = None
        runtime.stop(profile)
        epoch_before = (profile / "qt-service.json").exists()
        replies = native(message("B_cold", "add", silent("cold-start.bin")))
        assert not epoch_before and replies[0]["content"]["payload"] == "true", replies
        client = Client(profile, "runtime-native-cold")
        wait_for(lambda: next((r for r in client.snapshot()["rows"] if r["name"] == "cold-start.bin" and r["state"] == "completed"), None), 30)
        assert (destination / "cold-start.bin").read_bytes() == payload
        wait_for(lambda: client.snapshot()["browserIntegration"]["state"] == "listening", 15)
        checks.append("native-messaging host (original stdio protocol, browser-appended arguments) pings without the service, hands off once, and cold-starts a stopped service")
        monitor = args.package / "lib/abdm-activity-monitor" if args.package else runtime.ACTIVITY_MONITOR
        if monitor.is_file():
            assert client.snapshot()["settingsPolicy"]["activityAdapter"]
            assert client.command("settings.update", settings={"useSpeedLimit": "EnabledWhenBessy"})["ok"]
            wait_for(lambda: client.snapshot()["activity"]["available"], 10)
            assert client.snapshot()["activity"]["monitor"] == "running"
            assert client.command("settings.update", settings={"useSpeedLimit": "Disabled"})["ok"]
            activity = client.snapshot()["activity"]
            assert activity["monitor"] == "stopped" and not activity["available"] and activity["appliedSpeedLimit"] == "0"
            checks.append("explicit runtime configures the packaged/source Wayland input-only idle helper; selecting busy mode starts it, disabling joins it and removes the applied limit without injecting input")
        client.close(); client = None
        if args.native_ui:
            cli = [str(package_cli)] if package_cli else ["python3", str(runtime.HERE / "runtime.py")]
            log = evidence / "native-launch.log"
            epoch_before = (profile / "qt-service.json").read_bytes()
            with log.open("wb") as stream:
                launcher = subprocess.Popen([*cli, "launch", "--runtime-policy", str(policy_file)], cwd=evidence, stdout=stream, stderr=stream, start_new_session=True)
                qt_pid = None
                try:
                    def find_owned_qt():
                        nonlocal qt_pid
                        if launcher.poll() is not None: raise AssertionError("Native launcher exited early")
                        children = Path(f"/proc/{launcher.pid}/task/{launcher.pid}/children").read_text().split()
                        expected = (args.package / "lib/qt-capability-shell" if args.package else runtime.FRONTEND).resolve()
                        for child in children:
                            if Path(f"/proc/{child}/exe").resolve() == expected:
                                qt_pid = int(child); return True
                        return False
                    wait_for(find_owned_qt, 10)
                    time.sleep(2)
                    assert launcher.poll() is None
                    # Read only the watcher's registration list and compare sender PIDs.
                    # Never activate or inspect another application's tray/window.
                    def owned_tray(pid):
                        command = shutil.which("qdbus6")
                        if not command:
                            return None
                        watcher = subprocess.run([command, "org.kde.StatusNotifierWatcher", "/StatusNotifierWatcher",
                                                  "org.kde.StatusNotifierWatcher.RegisteredStatusNotifierItems"], capture_output=True, text=True, timeout=5)
                        if watcher.returncode:
                            return None
                        for item in watcher.stdout.splitlines():
                            owner = item.split("/", 1)[0]
                            owner_pid = subprocess.run([command, "org.freedesktop.DBus", "/org/freedesktop/DBus",
                                                        "org.freedesktop.DBus.GetConnectionUnixProcessID", owner], capture_output=True, text=True, timeout=5)
                            if owner_pid.returncode == 0 and owner_pid.stdout.strip() == str(pid):
                                return True
                        return False
                    tray_available = owned_tray(qt_pid)
                    if tray_available is not None:
                        wait_for(lambda: owned_tray(qt_pid), 10)
                    # The installed window must render with the desktop style it was reviewed under:
                    # a private copy of kdeglobals, never the user's real writable config.
                    qt_env = dict(entry.split("=", 1) for entry in Path(f"/proc/{qt_pid}/environ").read_text().split("\0") if "=" in entry)
                    real_config = Path(os.environ.get("XDG_CONFIG_HOME") or Path.home() / ".config")
                    assert Path(qt_env["XDG_CONFIG_HOME"]) != real_config and Path(qt_env["HOME"]) != Path.home()
                    if (real_config / "kdeglobals").is_file():
                        copied = Path(qt_env["XDG_CONFIG_HOME"]) / "kdeglobals"
                        assert not copied.is_symlink() and copied.read_bytes() == (real_config / "kdeglobals").read_bytes()
                    # A concurrent launch must not create another frontend or service.
                    second = subprocess.run([*cli, "launch", "--runtime-policy", str(policy_file)], cwd=evidence, capture_output=True, timeout=15)
                    assert second.returncode == 0 and b"Activation requested" in second.stderr
                    stopped_pid = qt_pid
                    os.kill(qt_pid, signal.SIGTERM)  # Only our verified direct child, never an installed window.
                    launcher.wait(timeout=15)
                    qt_pid = None
                    if tray_available is not None:
                        wait_for(lambda: owned_tray(stopped_pid) is False, 10)
                        checks.append("owned native Qt frontend registers a real StatusNotifier tray item and removes only its own registration on exit")
                finally:
                    if qt_pid is not None and launcher.poll() is None:
                        os.kill(qt_pid, signal.SIGTERM); launcher.wait(timeout=15)
                    elif launcher.poll() is None:
                        launcher.terminate(); launcher.wait(timeout=15)
            assert runtime.available(profile) and (profile / "qt-service.json").read_bytes() == epoch_before
            assert not any(error in log.read_text() for error in ("TypeError", "ReferenceError", "failed to load", "is not a type"))
            checks.append("native portable/source launcher opens private-state Qt with a copied desktop style, requests activation instead of duplicating the frontend, and closing owned Qt leaves the same service epoch running")
        runtime.stop(profile)
        if package_cli:
            assert subprocess.run([str(package_cli), "--help"], cwd=evidence, stdout=subprocess.DEVNULL).returncode == 0
            checks.append("relocated portable package runs from unrelated cwd with external system JVM/Qt dependencies")
        (evidence / "results.json").write_text(json.dumps({"checks": checks, "passed": True}, indent=2))
        print(json.dumps({"evidence": str(evidence), "checks": checks}, indent=2))
    finally:
        if client: client.close()
        if runtime.available(profile): runtime.stop(profile)
        for server in (proxy, origin, target, *tls_servers): server.shutdown(); server.server_close()


if __name__ == "__main__":
    main()
