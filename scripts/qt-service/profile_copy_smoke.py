#!/usr/bin/env python3
"""Generated fixtures ONLY: real CLI copy/recovery, cross-process lease and quarantine.
Never reads installed/old profiles. Does not promote a candidate or start a transfer.
Keeps all generated evidence, including any partial/unknown workspace, on failure.
"""
from __future__ import annotations
import argparse
import fcntl
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import tempfile
import uuid

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
spec = importlib.util.spec_from_file_location("copy_smoke_runtime", HERE / "runtime.py")
runtime = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runtime)


def sha(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def evidence(root):
    return {str(p.relative_to(root)): (sha(p), p.stat().st_mtime_ns, p.stat().st_ino)
            for p in root.rglob("*") if p.is_file()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True, help="NEW private evidence directory under build/qt-ui")
    args = parser.parse_args()
    output = runtime.no_symlinks(args.output)
    if not output.is_relative_to(REPO / "build/qt-ui"):
        raise ValueError("Private evidence must remain under build/qt-ui")
    output.mkdir(mode=0o700)
    fixture = Path(tempfile.mkdtemp(prefix="profile-copy-smoke-", dir=REPO / "build/qt-service"))
    source = fixture / "source"; source.mkdir(mode=0o700)
    payload = fixture / "payload"; payload.mkdir(mode=0o700)
    service_args = ["--isolated-profile", str(source)]
    environment = dict(os.environ, JAVA_HOME="/usr/lib/jvm/java-25-openjdk")
    # The runtime launcher inherits JAVA_HOME for its OWN fresh isolated child only.
    os.environ["JAVA_HOME"] = environment["JAVA_HOME"]
    started = False

    def cli(label, option, path, expected=0):
        process = subprocess.Popen([str(runtime.SERVICE), option, str(path)], cwd=REPO, env=environment,
                                   stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            stdout, stderr = process.communicate(timeout=60)
        except subprocess.TimeoutExpired as error:
            # subprocess.run(timeout=...) would SIGKILL a copy writer. Retain this
            # owned process and its unknown outcome instead; never force cleanup.
            (output / f"{label}.unknown.json").write_text(json.dumps({"pid": process.pid, "outcome": "unknown", "forcedKill": False}) + "\n")
            raise RuntimeError("Copy CLI deadline exceeded; writer retained, inspect privately") from error
        result = subprocess.CompletedProcess(process.args, process.returncode, stdout, stderr)
        (output / f"{label}.stdout").write_bytes(stdout)
        (output / f"{label}.stderr").write_bytes(stderr)
        if (result.returncode == 0) != (expected == 0):
            raise AssertionError(f"Unexpected CLI outcome: {label}; inspect private log")
        return result

    def request(name, destination, choices):
        path = fixture / f"{name}.json"
        runtime.write_private(path, {"source": str(source), "destination": str(destination),
                                     "payloadRoots": [str(payload)], "choices": choices})
        return path

    try:
        started = runtime.ensure_service(source, service_args)
        assert started
        with runtime.connect(source) as (stream, hello):
            def command(op, **fields):
                command_id = str(uuid.uuid4())
                reply = runtime.exchange(stream, {"type": "command", "epoch": hello["epoch"], "commandId": command_id, "op": op, **fields})
                assert reply.get("commandId") == command_id and reply.get("result", {}).get("ok") is True
                return reply["result"]
            # Add exactly once, never resume. No HTTP server/port (including 15151) is bound.
            command("add", url="http://127.0.0.1:12345/generated-never-requested", name="fixture.bin", start=False,
                    credentials={"username": "generated-user", "password": "generated-private"})
            command("settings.update", settings={"threadCount": 4})
        runtime.stop(source)
        started = False
        settings = source / "app-data/config/appSettings.json"
        temporary = settings.with_name(settings.name + ".tmp")
        temporary.write_text(settings.read_text().replace('"threadCount":4', '"threadCount":8'))
        assert temporary.read_bytes() != settings.read_bytes()
        (payload / "fixture.bin.dl-0.abdm.part").write_bytes(bytes(range(251)) * 17)
        (source / "Downloads/without-extension.bin").write_bytes(b"generated partial bytes\0\xff")
        before = evidence(source); payload_before = evidence(payload)
        lock = source / "app-data/config/app.lock"
        # POSIX lockf and Java FileChannel MUST agree across processes, not just within JVM.
        busy = fixture / "rejected-busy"
        with lock.open("r+b") as owner:
            fcntl.lockf(owner, fcntl.LOCK_EX | fcntl.LOCK_NB)
            cli("competing-owner", "--fixture-profile-copy", request("busy", busy, []), expected=1)
            assert not busy.exists()
        assert evidence(source) == before
        archive = fixture / "archive"
        cli("archive", "--fixture-profile-copy", request("archive-request", archive, []))
        cli("archive-verify", "--fixture-profile-verify", archive)
        recovery = fixture / "recovery"
        choices = [{"target": "app-data/config/appSettings.json", "use": "temporary",
                    "committedSha256": sha(settings), "temporarySha256": sha(temporary)}]
        cli("recovery", "--fixture-profile-copy", request("recovery-request", recovery, choices))
        cli("recovery-verify", "--fixture-profile-verify", recovery)
        assert sha(recovery / "candidate/app-data/config/appSettings.json") == sha(temporary)
        assert sha(recovery / "original/app-data/config/appSettings.json") == sha(settings)
        assert sha(recovery / "original/app-data/config/appSettings.json.tmp") == sha(temporary)
        assert evidence(source) == before and evidence(payload) == payload_before
        copied_before = evidence(recovery)
        cli("quarantined-engine", "--isolated-profile", recovery / "candidate", expected=1)
        launcher = subprocess.run(["python3", str(HERE / "runtime.py"), "start", "--isolated-profile", str(recovery / "candidate")],
                                  cwd=REPO, env=environment, stdin=subprocess.DEVNULL, capture_output=True, timeout=15)
        (output / "quarantined-launcher.stderr").write_bytes(launcher.stderr)
        assert launcher.returncode == 2
        assert evidence(recovery) == copied_before
        cli("refuse-reuse", "--fixture-profile-copy", fixture / "recovery-request.json", expected=1)
        assert evidence(recovery) == copied_before
        assert evidence(source) == before and evidence(payload) == payload_before
        result = {"fixture": str(fixture), "archive": str(archive), "recovery": str(recovery),
                  "sourceUnchanged": True, "payloadUnchanged": True, "crossProcessLeaseRejected": True,
                  "quarantineRejectedBeforeWrites": True, "checksumsVerified": True,
                  "engineValidated": False, "executionAllowed": False, "realDataImported": False}
        (output / "results.json").write_text(json.dumps(result, indent=2) + "\n")
        print(json.dumps(result, indent=2))
    finally:
        if started:
            # Authenticated joined shutdown ONLY. A failed drain keeps its writer and evidence.
            runtime.stop(source)


if __name__ == "__main__":
    main()
