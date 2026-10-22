#!/usr/bin/env python3
"""Generated original-format CLI inventory/mapping fixture. No engine, network or activation.
Retains all source/copy/review evidence. Never reads a real or installed profile.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile

from profile_inventory import ROOT, DB


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    output = parser.parse_args().output
    os.umask(0o077)
    if not output.is_absolute() or output != output.resolve() or not output.is_relative_to(ROOT / "build/qt-ui"):
        raise ValueError("Use a NEW private evidence directory")
    output.mkdir(mode=0o700)
    fixture = Path(tempfile.mkdtemp(prefix="semantic-profile-", dir=ROOT / "build/qt-service"))
    source = fixture / "source"; source.mkdir()
    payload = fixture / "payload"; payload.mkdir()
    id_ = 9007199254740993
    def put(name, value):
        p = source / name; p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(json.dumps(value)); return p
    lock = source / "app-data/config/app.lock"; lock.parent.mkdir(parents=True); lock.write_bytes(b"")
    settings = put("app-data/config/appSettings.json", dict(appendExtensionToIncompleteDownloads=True, threadCount=4, futureSetting={"kept": True}))
    temporary = put("app-data/config/appSettings.json.tmp", dict(appendExtensionToIncompleteDownloads=True, threadCount=8, futureSetting={"kept": True}))
    put(DB + f"downloadlist/{id_}.json", dict(type="http", id=id_, link="http://127.0.0.1:12345/generated-never-fetched",
        folder=str(payload), name="fixture.bin", contentLength=8, status="Paused", dateAdded=1,
        username="generated-user", password="generated-secret", futureRecord={"integer": 9007199254740997}))
    put(DB + "downloadlist/last_id.txt", id_)
    put(DB + f"parts/{id_}.json", dict(type="ranges", list=[{"from": 0, "to": 7, "current": 4}]))
    put(DB + "queues/0.json", dict(id=0, name="Main", queueItems=[id_], maxConcurrent=2,
        scheduledTimes=dict(daysOfWeek=["MONDAY"], startTime="02:00", endTime="03:00", enabledStartTime=True, enabledEndTime=False)))
    put(DB + "categories/categories.json", [dict(id=0, name="Fixtures", icon="icon:fileOpen", path=str(payload), usePath=False,
        acceptedFileTypes=[], acceptedUrlPatterns=[], items=[id_])])
    put(DB + f"extra_download_settings/{id_}.json", dict(id=id_, powerActionTypeOnFinish="Shutdown", powerActionUseForceOnFinish=True, futureExtra={"retain": 1}))
    part_name = f"fixture.bin.dl-{id_}.abdm.part"
    (payload / part_name).write_bytes(b"abcd")
    def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
    def snapshot(root): return {str(p.relative_to(root)): (sha(p), p.stat().st_mtime_ns, p.stat().st_ino) for p in root.rglob("*") if p.is_file()}
    before = snapshot(source), snapshot(payload)
    workspace = fixture / "workspace"
    request = fixture / "request.json"
    request.write_text(json.dumps(dict(source=str(source), destination=str(workspace), payloadRoots=[str(payload)], choices=[dict(
        target="app-data/config/appSettings.json", use="temporary", committedSha256=sha(settings), temporarySha256=sha(temporary))])))
    service = ROOT / "desktop/service/build/install/service/bin/service"
    with (output / "copy.log").open("wb") as log:
        process = subprocess.Popen([str(service), "--fixture-profile-copy", str(request)], cwd=ROOT, stdout=log, stderr=log)
        # wait(timeout) never kills the writer. Unknown outcomes are retained as-is.
        try: code = process.wait(timeout=60)
        except subprocess.TimeoutExpired:
            (output / "unknown.json").write_text(json.dumps(dict(pid=process.pid, forcedKill=False)) + "\n")
            raise
        assert code == 0
    mapping_file = fixture / "reviewed-mapping.json"
    mapping_file.write_text(json.dumps(dict(manifestSha256=sha(workspace / "manifest.json"), profileTree="candidate", mappings=[dict(
        downloadId=str(id_), sourcePath=str(payload / part_name), copyPath="payloads/0/" + part_name)])))
    with (output / "inventory.json").open("wb") as report, (output / "inventory.stderr").open("wb") as error:
        result = subprocess.run(["python3", str(ROOT / "scripts/qt-service/profile_inventory.py"), "--workspace", str(workspace),
            "--tree", "candidate", "--reviewed-mapping", str(mapping_file)], cwd=ROOT, stdout=report, stderr=error, timeout=60)
    assert result.returncode == 0
    report = json.loads((output / "inventory.json").read_text())
    assert report["highWater"] == str(id_) and len(report["reviewedPayloads"]) == 1
    assert not report["executionAllowed"] and not report["sourceAccessed"] and report["compatibility"] == "blocked"
    assert "payload-mapping-unreviewed" not in {i["code"] for i in report["issues"]}
    assert not (output / "inventory.json").read_text().find("generated-secret") >= 0
    assert before == (snapshot(source), snapshot(payload))
    result = dict(fixture=str(fixture), workspace=str(workspace), sourceAndPayloadUnchanged=True, mappingHashBound=True,
                  sourceOpenedByInventory=False, engineStarted=False, executionAllowed=False, status="passed-with-explicit-compatibility-blockers")
    (output / "results.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2))


if __name__ == "__main__": main()
