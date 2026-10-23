#!/usr/bin/env python3
"""OFFLINE generated-fixture semantic inventory; never boots an engine or activates a writer.
Only verified quarantined workspaces under build/qt-service are accepted by the CLI.
Reports are private stdout: redirect to a NEW 0600 evidence file. No source/payload
path mentioned inside a document is opened. Unknown/corrupt bytes stay in the archive.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]
LONG_MAX = 2**63 - 1
DB = "app-data/config/download_db/"
HTTP_FIELDS = set("type link headers username password downloadPage userAgent serverETag id folder name contentLength dateAdded startTime completeTime status preferredConnectionCount speedLimit fileChecksum".split())
QUEUE_FIELDS = set("id name maxConcurrent queueItems scheduledTimes stopQueueOnEmpty".split())
CATEGORY_FIELDS = set("id name icon path usePath acceptedFileTypes acceptedUrlPatterns items".split())
SCHEDULE_FIELDS = set("daysOfWeek startTime endTime enabledStartTime enabledEndTime".split())
EXTRA_FIELDS = set("id powerActionTypeOnFinish powerActionUseForceOnFinish".split())
SETTINGS_FIELDS = set("theme defaultDarkTheme defaultLightTheme language font uiScale showIconLabels useRelativeDateTime mergeTopBarWithTitleBar useNativeMenuBar threadCount maxConcurrentDownloads maxDownloadRetryCount dynamicPartCreation useServerLastModifiedTime useSparseFileAllocation useAverageSpeed useSpeedLimit speedLimit minPartSize appendExtensionToIncompleteDownloads sizeUnit speedUnit useCategoryByDefault showDownloadProgressDialog showDownloadCompletionDialog autoStartOnBoot notificationSound generalNotificationSound errorNotificationSound successNotificationSound apiEnabled apiPort apiAuthKey apiAuthEnabled useSystemTray trackDeletedFilesOnDisk deletePartialFileOnDownloadCancellation defaultDownloadFolder ignoreSSLCertificates userAgent".split())


def sha(data):
    return hashlib.sha256(data).hexdigest()


def strict_json(data):
    def pairs(entries):
        result = {}
        for key, value in entries:
            if key in result:
                raise ValueError("duplicate-json-key")
            result[key] = value
        return result
    value = json.loads(data, object_pairs_hook=pairs, parse_constant=lambda _: (_ for _ in ()).throw(ValueError("non-finite-json")))
    def depth(v, n=0):
        if n > 64:
            raise ValueError("json-depth-limit")
        if isinstance(v, dict):
            for c in v.values(): depth(c, n + 1)
        elif isinstance(v, list):
            for c in v: depth(c, n + 1)
    depth(value)
    return value


def relative(value):
    p = Path(value)
    if not isinstance(value, str) or not value or p.is_absolute() or str(p) != value or any(x in (".", "..") for x in p.parts):
        raise ValueError("invalid-inventory-path")
    return p


def no_links(path):
    for p in (path, *path.parents):
        if p.is_symlink(): raise ValueError("symlink-refused")


def private_read(path, limit):
    no_links(path)
    st = path.stat()
    if not path.is_file() or st.st_uid != os.getuid() or st.st_nlink != 1 or st.st_mode & 0o077 or st.st_size > limit:
        raise ValueError("nonprivate-or-oversized-input")
    return path.read_bytes()


def inventory(workspace: Path, tree: str, mapping=None):
    """Read only hash-bound copies; caller CLI additionally runs the service verifier.
    Conservative subset checker, NOT a certification of runnable legacy compatibility.
    """
    manifest_bytes = private_read(workspace / "manifest.json", 64 * 1024**2)
    manifest_hash = sha(manifest_bytes)
    if private_read(workspace / "COMPLETE", 65).decode() != manifest_hash + "\n":
        raise ValueError("incomplete-or-stale-workspace")
    manifest = strict_json(manifest_bytes)
    if tree not in ("original", "candidate") or manifest.get("executionAllowed") is not False or manifest.get("engineValidated") is not False:
        raise ValueError("unsupported-workspace")
    private_read(workspace / ".qt-copy-quarantine", 1024)
    private_read(workspace / tree / ".qt-copy-quarantine", 1024)
    inventories = manifest["copyInventories"]
    files = {e["path"]: e for e in inventories[tree]["files"]}
    if len(files) != len(inventories[tree]["files"]): raise ValueError("duplicate-inventory-path")
    issues, unknown, documents = [], [], {}
    def issue(code, path, **detail):
        issues.append(dict(code=code, path=path, **detail))
    def read_copy(copy_tree, name):
        entries = {e["path"]: e for e in inventories[copy_tree]["files"]}
        e = entries[name]
        p = workspace / relative(copy_tree) / relative(name)
        raw = private_read(p, 1024**3)
        if len(raw) != int(e["size"]) or sha(raw) != e["sha256"]: raise RuntimeError("copy-hash-mismatch")
        return raw
    for name in sorted(files):
        relative(name)
        if name.endswith(".tmp"):
            issue("unresolved-transaction", name)
        if name.startswith("app-data/config/") and name.endswith(".json"):
            try:
                raw = read_copy(tree, name)
                if len(raw) > 2 * 1024**2: raise ValueError("document-size-limit")
                documents[name] = strict_json(raw)
            except (ValueError, RecursionError, UnicodeError):
                issue("corrupt-or-ambiguous-json", name)
    def fields(doc, known, path):
        if not isinstance(doc, dict): raise ValueError("expected-object")
        for key in sorted(doc.keys() - known): unknown.append(dict(path=path, field=key))
    def integer(v, minimum=0, maximum=LONG_MAX - 1):
        if type(v) is not int or not minimum <= v <= maximum: raise ValueError("invalid-integer")
        return v
    def ids(values):
        if not isinstance(values, list): raise ValueError("invalid-memberships")
        result = [integer(v) for v in values]
        if len(result) != len(set(result)): raise ValueError("duplicate-membership")
        return result
    def canonical_id(name, folder):
        suffix = name.removeprefix(DB + folder + "/")
        if not re.fullmatch(r"(?:0|[1-9][0-9]*)\.json", suffix): raise ValueError("noncanonical-id-file")
        return integer(int(suffix[:-5]))
    records, parts, queues, categories = {}, {}, {}, {}
    for name, doc in documents.items():
        try:
            if name.startswith(DB + "downloadlist/"):
                id_ = canonical_id(name, "downloadlist")
                fields(doc, HTTP_FIELDS, name)
                if integer(doc["id"]) != id_: raise ValueError("record-id-mismatch")
                if doc.get("type") != "http": raise ValueError("unsupported-record-type")
                if not isinstance(doc.get("folder"), str) or not Path(doc["folder"]).is_absolute() or str(Path(doc["folder"])) != doc["folder"] or ".." in Path(doc["folder"]).parts:
                    raise ValueError("invalid-stored-folder")
                filename = doc.get("name")
                if not isinstance(filename, str) or not 1 <= len(filename) <= 180 or filename in (".", "..") or any(c in filename for c in "/\\") or any(ord(c) < 32 for c in filename):
                    raise ValueError("invalid-stored-name")
                integer(doc.get("contentLength", -1), -1)
                integer(doc.get("speedLimit", 0)); integer(doc.get("dateAdded", 0), 0, LONG_MAX)
                for key in ("startTime", "completeTime"):
                    if doc.get(key) is not None: integer(doc[key], 0, LONG_MAX)
                if doc.get("preferredConnectionCount") is not None: integer(doc["preferredConnectionCount"], 1, 256)
                for key in ("username", "password", "userAgent", "downloadPage", "serverETag", "fileChecksum"):
                    if doc.get(key) is not None and (not isinstance(doc[key], str) or len(doc[key]) > 8192): raise ValueError("unsupported-credential-or-metadata-field")
                headers = {} if doc.get("headers") is None else doc["headers"]
                if not isinstance(headers, dict) or len(headers) > 64 or any(not isinstance(v, str) or len(v) > 32768 for v in headers.values()): raise ValueError("unsupported-captured-headers")
                if doc.get("status", "Added") not in ("Added", "Paused", "Error", "Downloading", "Completed"): raise ValueError("unsupported-status")
                # URL/credentials intentionally never emitted in this report.
                if not isinstance(doc.get("link"), str): raise ValueError("invalid-link")
                records[id_] = doc
            elif name.startswith(DB + "parts/"):
                id_ = canonical_id(name, "parts")
                fields(doc, {"type", "list"}, name)
                if doc.get("type") != "ranges": raise ValueError("unsupported-media-segments")
                ranges = doc["list"]
                if not isinstance(ranges, list) or len(ranges) > 65536: raise ValueError("invalid-parts")
                decoded = []
                for i, part in enumerate(ranges):
                    fields(part, {"from", "to", "current"}, name + f"/list/{i}")
                    start = integer(part["from"])
                    end = None if part.get("to") is None else integer(part["to"], -1)
                    current = integer(part.get("current", start), start, LONG_MAX)
                    if end is not None and (end < start - 1 or current > end + 1): raise ValueError("invalid-range-progress")
                    decoded.append((start, end, current))
                decoded.sort()
                for previous, current in zip(decoded, decoded[1:]):
                    if previous[1] is None or previous[1] >= current[0]: raise ValueError("overlapping-ranges")
                parts[id_] = decoded
            elif name.startswith(DB + "queues/"):
                id_ = canonical_id(name, "queues")
                fields(doc, QUEUE_FIELDS, name)
                if integer(doc["id"]) != id_: raise ValueError("queue-id-mismatch")
                integer(doc.get("maxConcurrent", 2), 1, 1024)
                members = ids(doc.get("queueItems", []))
                schedule = doc.get("scheduledTimes")
                if schedule is not None:
                    fields(schedule, SCHEDULE_FIELDS, name + "/scheduledTimes")
                    if schedule.get("enabledStartTime") is True or schedule.get("enabledEndTime") is True:
                        issue("schedule-retained-never-activated", name)
                queues[id_] = members
            elif name == DB + "categories/categories.json":
                if not isinstance(doc, list): raise ValueError("invalid-category-list")
                for i, category in enumerate(doc):
                    fields(category, CATEGORY_FIELDS, name + f"/{i}")
                    id_ = integer(category["id"])
                    if id_ in categories: raise ValueError("duplicate-category-id")
                    categories[id_] = ids(category.get("items", []))
            elif any(name.startswith(DB + folder + "/") for folder in ("extra_download_settings", "extra_queue_settings")):
                folder = name.split("/")[-2]
                id_ = canonical_id(name, folder)
                fields(doc, EXTRA_FIELDS, name)
                if integer(doc["id"]) != id_: raise ValueError("extra-id-mismatch")
            elif name == "app-data/config/appSettings.json":
                fields(doc, SETTINGS_FIELDS, name)
            else:
                issue("unclassified-document", name)
        except (ValueError, KeyError, TypeError) as e:
            # Never echo source values or parser exception messages (may contain secrets).
            issue("schema-invalid-or-unsupported", name, reason=str(e) if isinstance(e, ValueError) else "missing-or-invalid-field")
    for folder in ("downloadlist", "parts", "queues", "categories", "extra_download_settings", "extra_queue_settings"):
        for name in files:
            if name.startswith(DB + folder + "/") and not name.endswith((".json", ".tmp")) and name != DB + "downloadlist/last_id.txt":
                issue("unclassified-store-entry", name)
    high_name = DB + "downloadlist/last_id.txt"
    high = None
    try:
        high = integer(strict_json(read_copy(tree, high_name)), -1)
        if high < max(records, default=-1): issue("regressed-high-water", high_name)
        if high >= LONG_MAX - 1: issue("exhausted-next-download-id", high_name)
    except (KeyError, ValueError, UnicodeError):
        issue("missing-or-invalid-high-water", high_name)
    for store, memberships in (("queues", queues), ("categories", categories)):
        owners = {}
        for id_, members in memberships.items():
            for member in members:
                if member not in records: issue("dangling-membership", store, ownerId=str(id_), downloadId=str(member))
                if member in owners: issue("multiple-memberships", store, downloadId=str(member))
                owners[member] = id_
    for name in documents:
        for folder, live in (("extra_download_settings", records), ("extra_queue_settings", queues)):
            if name.startswith(DB + folder + "/"):
                try:
                    if canonical_id(name, folder) not in live: issue("orphan-extra-settings", name)
                except ValueError: pass
    for id_, ranges in parts.items():
        record = records.get(id_)
        if record is None:
            issue("orphan-parts-retained", f"parts/{id_}.json"); continue
        length = record.get("contentLength", -1)
        if length >= 0:
            cursor = 0
            for start, end, current in ranges:
                if start != cursor or end is None or end >= length: issue("range-content-length-mismatch", f"parts/{id_}.json"); break
                cursor = end + 1
            if cursor != length: issue("range-coverage-incomplete", f"parts/{id_}.json")
        if record.get("status") == "Completed" and any(end is None or current != end + 1 for start, end, current in ranges):
            issue("completed-record-with-incomplete-ranges", f"parts/{id_}.json")
    mapped = []
    if mapping is not None:
        if set(mapping) != {"manifestSha256", "profileTree", "mappings"} or mapping["manifestSha256"] != manifest_hash or mapping["profileTree"] != tree:
            raise ValueError("stale-or-invalid-mapping-review")
        seen_ids, seen_files = set(), set()
        for m in mapping["mappings"]:
            if set(m) != {"downloadId", "sourcePath", "copyPath"}: raise ValueError("invalid-mapping-keys")
            if not isinstance(m["downloadId"], str) or not re.fullmatch(r"0|[1-9][0-9]*", m["downloadId"]): raise ValueError("invalid-mapping-id")
            id_ = int(m["downloadId"])
            if id_ not in records or id_ in seen_ids or m["copyPath"] in seen_files: raise ValueError("duplicate-or-unknown-mapping")
            record = records[id_]
            final = str(Path(record["folder"]) / record["name"])
            suffix = f".dl-{id_}.abdm.part"
            part_name = record["name"] if record["name"].endswith(suffix) else record["name"][:255 - len(suffix)] + suffix
            partial = str(Path(record["folder"]) / part_name)
            if m["sourcePath"] not in {final, partial}: raise ValueError("mapping-not-stored-destination")
            copy_path = relative(m["copyPath"])
            if len(copy_path.parts) < 3 or copy_path.parts[0] != "payloads" or not copy_path.parts[1].isdigit(): raise ValueError("mapping-outside-copied-payloads")
            index = int(copy_path.parts[1]); copy_tree = f"payloads/{index}"
            if copy_path.parts[1] != str(index): raise ValueError("noncanonical-payload-copy-index")
            source_root = Path(manifest["payloadRoots"][index])
            tail = Path(*copy_path.parts[2:])
            if str(source_root / tail) != m["sourcePath"]: raise ValueError("mapping-source-copy-mismatch")
            raw = read_copy(copy_tree, str(tail))
            size = len(raw)
            completed = record.get("status") == "Completed"
            if completed and (m["sourcePath"] != final or record.get("contentLength", -1) < 0 or size != record["contentLength"]):
                issue("completed-payload-size-or-layout-mismatch", m["copyPath"])
            for start, end, current in parts.get(id_, []):
                if current > start and current > size: issue("progress-beyond-payload", m["copyPath"])
            if not completed and id_ not in parts and size: issue("payload-without-range-evidence", m["copyPath"])
            seen_ids.add(id_); seen_files.add(m["copyPath"])
            mapped.append(dict(downloadId=str(id_), copyPath=m["copyPath"], size=str(size), sha256=sha(raw)))
        for id_ in records.keys() - seen_ids: issue("payload-mapping-unreviewed", "downloads", downloadId=str(id_))
    elif records:
        issue("destination-mapping-review-required", "downloads")
    # appSettings and future/platform fields are preserved as raw bytes, NOT certified
    # by an incomplete reimplementation of the original settings semantics.
    if "app-data/config/appSettings.json" in documents:
        issue("full-settings-semantics-unvalidated", "app-data/config/appSettings.json")
    return dict(format=1, mode="offline-read-only-review", manifestSha256=manifest_hash, profileTree=tree,
                engineValidated=False, executionAllowed=False, externalPayloadsComplete=False,
                sourceAccessed=False, bytesRewritten=False, automaticReplay=False,
                counts=dict(downloads=len(records), queues=len(queues), categories=len(categories), parts=len(parts)),
                highWater=None if high is None else str(high), unknownFields=unknown, issues=issues, reviewedPayloads=mapped,
                compatibility="blocked" if issues or unknown else "supported-subset-only-not-activation-authority",
                remainingDependencies=["lossless-engine-saves", "full-original-settings-and-schema-validation", "non-scheduling-writer-activation"])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--workspace", type=Path, required=True)
    parser.add_argument("--tree", choices=("original", "candidate"), required=True)
    parser.add_argument("--reviewed-mapping", type=Path)
    args = parser.parse_args()
    os.umask(0o077)
    p = args.workspace
    allowed = ROOT / "build/qt-service"
    if not p.is_absolute() or p != p.resolve() or not p.is_relative_to(allowed) or p == allowed:
        raise ValueError("Only normalized disposable fixture workspaces are allowed")
    # Full exact-tree/privacy/mtime/quarantine verification, without engine/source access.
    subprocess.run([str(ROOT / "desktop/service/build/install/service/bin/service"), "--fixture-profile-verify", str(p)],
                   cwd=ROOT, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    mapping = strict_json(private_read(args.reviewed_mapping, 128 * 1024)) if args.reviewed_mapping else None
    print(json.dumps(inventory(p, args.tree, mapping), indent=2))


if __name__ == "__main__":
    try: main()
    except Exception:
        raise SystemExit("Offline review rejected. Retain workspace and mapping; inspect privately. No engine started or recovery replayed.")
