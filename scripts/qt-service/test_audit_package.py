import hashlib
import json
from pathlib import Path
import tempfile
import unittest
import zipfile

from audit_package import audit


class PackageAuditTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="abdm-audit-")
        self.root = Path(self.tmp.name)
        (self.root / "lib/service/lib").mkdir(parents=True)
        (self.root / "notices").mkdir()
        jar = self.root / "lib/service/lib/fixture.jar"
        with zipfile.ZipFile(jar, "w") as z: z.writestr("META-INF/LICENSE", "generated fixture notice, not a real dependency")
        (self.root / "notices/fixture-license").write_text("fixture")
        (self.root / "dependencies.json").write_text(json.dumps([dict(jar="fixture.jar", sha256=hashlib.sha256(jar.read_bytes()).hexdigest(), embeddedNotices=["fixture-license"])]))
        self.manifest()

    def tearDown(self): self.tmp.cleanup()

    def manifest(self):
        files = {str(p.relative_to(self.root)): hashlib.sha256(p.read_bytes()).hexdigest() for p in self.root.rglob("*") if p.is_file() and p != self.root / "manifest.json"}
        (self.root / "manifest.json").write_text(json.dumps(dict(format=1, releaseApproved=False, files=files)))

    def test_missing_provenance_is_a_blocker_not_a_release_pass(self):
        report = audit(self.root, self.root / "no-cache")
        self.assertEqual(report["integrity"], "passed")
        self.assertEqual(report["missingCachedProvenance"], 1)
        self.assertFalse(report["redistributionApproved"])
        self.assertFalse(report["cleanMachineValidated"])

    def test_tampered_and_unlisted_artifacts_fail(self):
        (self.root / "unlisted").write_text("unknown")
        with self.assertRaises(ValueError): audit(self.root)
        self.manifest()
        (self.root / "unlisted").write_text("changed")
        with self.assertRaises(ValueError): audit(self.root)

    def test_symlink_and_release_approval_cannot_bypass_audit(self):
        (self.root / "alias").symlink_to("dependencies.json")
        with self.assertRaises(ValueError): audit(self.root)


if __name__ == "__main__": unittest.main()
