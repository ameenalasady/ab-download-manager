import hashlib
import json
import os
from pathlib import Path
import tempfile
import unittest

from profile_inventory import inventory, strict_json, DB, SETTINGS_FIELDS, ROOT


class InventoryTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="abdm-inventory-")
        self.root = Path(self.tmp.name)
        (self.root / "original").mkdir(mode=0o700)
        self.files = {}
        self.payloads = {}
        self.source_root = "/generated-fixture/Downloads"
        self.add(DB + "downloadlist/9007199254740993.json", dict(type="http", id=9007199254740993,
                 link="http://127.0.0.1:12345/not-fetched", folder=self.source_root, name="f.bin",
                 contentLength=8, status="Paused", headers={"Cookie": "secret-fixture"}, future={"huge": 9007199254740997}))
        self.add(DB + "downloadlist/last_id.txt", 9007199254740993)
        self.add(DB + "parts/9007199254740993.json", dict(type="ranges", list=[dict(**{"from": 0}, to=7, current=4)]))
        self.add(DB + "queues/0.json", dict(id=0, name="Main", queueItems=[9007199254740993]))
        self.add(DB + "categories/categories.json", [dict(id=0, name="Fixtures", items=[9007199254740993])])

    def tearDown(self):
        self.tmp.cleanup()

    def put(self, path, raw):
        path.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
        path.write_bytes(raw); path.chmod(0o600)
        return dict(path=path.name, size=str(len(raw)), sha256=hashlib.sha256(raw).hexdigest())

    def add(self, name, value, raw=None):
        raw = json.dumps(value).encode() if raw is None else raw
        e = self.put(self.root / "original" / name, raw); e["path"] = name
        self.files[name] = e

    def finish(self):
        self.put(self.root / ".qt-copy-quarantine", b"quarantine")
        e = self.put(self.root / "original/.qt-copy-quarantine", b"quarantine"); self.files[e["path"]] = e
        copies = {"original": dict(files=list(self.files.values()))}
        if self.payloads: copies["payloads/0"] = dict(files=list(self.payloads.values()))
        raw = json.dumps(dict(executionAllowed=False, engineValidated=False, copyInventories=copies,
                             payloadRoots=[self.source_root] if self.payloads else [])).encode()
        self.put(self.root / "manifest.json", raw)
        digest = hashlib.sha256(raw).hexdigest()
        self.put(self.root / "COMPLETE", (digest + "\n").encode())
        return digest

    def test_unknown_fields_big_ids_and_credentials_are_not_reserialized_or_leaked(self):
        self.finish()
        before = {p: p.read_bytes() for p in self.root.rglob("*") if p.is_file()}
        r = inventory(self.root, "original")
        self.assertEqual(r["highWater"], "9007199254740993")
        self.assertIn(dict(path=DB + "downloadlist/9007199254740993.json", field="future"), r["unknownFields"])
        self.assertNotIn("secret-fixture", json.dumps(r))
        self.assertFalse(r["executionAllowed"])
        self.assertEqual(before, {p: p.read_bytes() for p in before})

    def test_cross_store_and_regressed_high_water(self):
        self.add(DB + "downloadlist/last_id.txt", 2)
        self.add(DB + "queues/0.json", dict(id=0, name="Main", queueItems=[4]))
        self.finish(); codes = {i["code"] for i in inventory(self.root, "original")["issues"]}
        self.assertTrue({"regressed-high-water", "dangling-membership"} <= codes)

    def test_corrupt_duplicate_keys_transactions_and_ranges_remain_blocked(self):
        self.add(DB + "queues/0.json", None, b'{"id":0,"id":1}')
        self.add(DB + "downloadlist/last_id.txt.tmp", 3)
        self.add(DB + "parts/9007199254740993.json", dict(type="ranges", list=[{"from": 0, "to": 7, "current": 9}]))
        self.finish(); codes = {i["code"] for i in inventory(self.root, "original")["issues"]}
        self.assertTrue({"unresolved-transaction", "corrupt-or-ambiguous-json", "schema-invalid-or-unsupported"} <= codes)

    def test_reviewed_mapping_binds_source_copy_and_progress_without_source_access(self):
        name = "f.bin.dl-9007199254740993.abdm.part"
        e = self.put(self.root / "payloads/0" / name, b"abc"); self.payloads[name] = e
        digest = self.finish()
        mapping = dict(manifestSha256=digest, profileTree="original", mappings=[dict(downloadId="9007199254740993",
            sourcePath=self.source_root + "/" + name, copyPath="payloads/0/" + name)])
        r = inventory(self.root, "original", mapping)
        self.assertIn("progress-beyond-payload", {i["code"] for i in r["issues"]})
        self.assertFalse(r["sourceAccessed"])
        mapping["manifestSha256"] = "0" * 64
        with self.assertRaises(ValueError): inventory(self.root, "original", mapping)

    def test_unreviewed_escape_and_tampered_bytes_are_rejected(self):
        digest = self.finish()
        mapping = dict(manifestSha256=digest, profileTree="original", mappings=[dict(downloadId="9007199254740993",
            sourcePath=self.source_root + "/f.bin", copyPath="../outside")])
        with self.assertRaises(ValueError): inventory(self.root, "original", mapping)
        (self.root / "original" / (DB + "downloadlist/last_id.txt")).write_text("0")
        with self.assertRaises(RuntimeError): inventory(self.root, "original")

    def test_settings_classification_matches_original_service_policy_and_preserves_future_keys(self):
        import re
        source = (ROOT / "desktop/service/src/main/kotlin/com/abdownloadmanager/service/SettingsPolicy.kt").read_text()
        self.assertEqual(SETTINGS_FIELDS, set(re.findall(r'"([A-Za-z]+)" to SettingPolicy\.', source)))
        self.add("app-data/config/appSettings.json", dict(threadCount=4, futureSetting={"preserve": 9007199254740997}))
        self.finish()
        report = inventory(self.root, "original")
        self.assertIn(dict(path="app-data/config/appSettings.json", field="futureSetting"), report["unknownFields"])
        self.assertFalse(report["executionAllowed"])

    def test_duplicate_and_nonfinite_json_fail_closed(self):
        for raw in (b'{"a":1,"a":2}', b'NaN', b'Infinity'):
            with self.assertRaises(ValueError): strict_json(raw)


if __name__ == "__main__": unittest.main()
