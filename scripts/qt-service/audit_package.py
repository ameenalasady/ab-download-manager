#!/usr/bin/env python3
"""Read-only integrity/license-evidence audit of an OWN private preview, not legal approval.
No downloads, registrations, installation, execution of package code or system changes.
Cached POM license declarations are evidence only, not an SPDX/obligation determination.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import xml.etree.ElementTree as ET
import zipfile

ROOT = Path(__file__).resolve().parents[2]


def digest(path):
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(128 * 1024), b""): h.update(chunk)
    return h.hexdigest()


def audit(package, cache=None):
    if package.is_symlink(): raise ValueError("package alias refused")
    manifest = json.loads((package / "manifest.json").read_text())
    if manifest.get("format") != 1 or manifest.get("releaseApproved") is not False:
        raise ValueError("not a private preview manifest")
    expected = manifest["files"]
    actual = set()
    for p in package.rglob("*"):
        if p.is_symlink(): raise ValueError("packaged symlink refused")
        if p.is_file() and p != package / "manifest.json": actual.add(str(p.relative_to(package)))
    if actual != set(expected): raise ValueError("package inventory differs")
    for name, hash_ in expected.items():
        p = Path(name)
        if p.is_absolute() or str(p) != name or ".." in p.parts: raise ValueError("unsafe manifest path")
        if digest(package / p) != hash_: raise ValueError("package hash differs")
    dependencies = json.loads((package / "dependencies.json").read_text())
    jars = {p.name: p for p in (package / "lib/service/lib").glob("*.jar")}
    if len(dependencies) != len(jars) or {d["jar"] for d in dependencies} != set(jars):
        raise ValueError("dependency inventory differs")
    cache = cache or Path.home() / ".gradle/caches/modules-2/files-2.1"
    # Match by exact binary SHA, not by guessed artifact-name/version splitting.
    cached = {}
    if cache.is_dir():
        for p in cache.glob("*/*/*/*/*.jar"):
            if p.name in jars: cached.setdefault(p.name, []).append(p)
    rows = []
    for dep in dependencies:
        jar = jars[dep["jar"]]
        hash_ = digest(jar)
        if hash_ != dep["sha256"]: raise ValueError("dependency hash differs")
        for notice in dep["embeddedNotices"]:
            if Path(notice).name != notice or not (package / "notices" / notice).is_file(): raise ValueError("missing notice")
        matches = [p for p in cached.get(jar.name, []) if digest(p) == hash_]
        coordinates, declarations, sources = [], [], []
        for match in matches:
            version_dir = match.parent.parent
            group, artifact, version = version_dir.parts[-3:]
            coordinates.append(f"{group}:{artifact}:{version}")
            for pom in version_dir.glob("*/*.pom"):
                if pom.stat().st_size > 2 * 1024**2: continue
                try:
                    root = ET.parse(pom).getroot()
                    for license_ in root.findall("{*}licenses/{*}license"):
                        declarations.append({k: license_.findtext("{*}" + k) for k in ("name", "url", "distribution")})
                except ET.ParseError: continue
            sources.extend(dict(file=p.name, sha256=digest(p)) for p in version_dir.glob("*/*-sources.jar"))
        with zipfile.ZipFile(jar) as archive:
            embedded = [name for name in archive.namelist() if name.lower().startswith("meta-inf/") and
                        any(word in name.lower() for word in ("license", "notice", "copying")) and not name.endswith("/")]
        rows.append(dict(jar=jar.name, sha256=hash_, coordinates=sorted(set(coordinates)),
                         cachedLicenseDeclarations=declarations, cachedSourceArchives=sources,
                         embeddedNoticeCount=len(embedded), extractedNotices=dep["embeddedNotices"],
                         obligationsReviewed=False, replacementRightsVerified=False))
    native = []
    for name in ("qt-capability-shell", "abdm-activity-monitor"):
        binary = package / "lib" / name
        if binary.exists():
            result = subprocess.run(["readelf", "-d", str(binary)], capture_output=True, text=True, check=True)
            dynamic = [line.strip() for line in result.stdout.splitlines() if any(t in line for t in ("(NEEDED)", "(RPATH)", "(RUNPATH)"))]
            native.append(dict(binary=name, sha256=digest(binary), dynamic=dynamic,
                               developmentPathLeak=any(str(ROOT) in line or "/home/" in line for line in dynamic)))
    return dict(format=1, artifact=str(package), integrity="passed", dependencies=rows, native=native,
                releaseApproved=False, redistributionApproved=False,
                cleanMachineValidated=False, sandboxAvailable=Path("/usr/bin/bwrap").exists(),
                blockers=["per-dependency obligation and transitive provenance review",
                          "complete corresponding release source/build instructions",
                          "system Qt/KDE plugin licenses and LGPL replacement/relink rights where applicable",
                          "independent clean-machine runtime/plugin validation (host relocation is not clean-machine evidence)",
                          "manual accessibility/input/desktop acceptance"],
                missingCachedProvenance=sum(not r["coordinates"] for r in rows),
                missingCachedLicenseDeclarations=sum(not r["cachedLicenseDeclarations"] for r in rows),
                missingCachedSources=sum(not r["cachedSourceArchives"] for r in rows))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=Path, required=True)
    args = parser.parse_args()
    p = args.package
    allowed = ROOT / "build/qt-packages"
    if not p.is_absolute() or p != p.resolve() or not p.is_relative_to(allowed) or p == allowed:
        raise ValueError("Only own disposable private packages under build/qt-packages")
    print(json.dumps(audit(p), indent=2))


if __name__ == "__main__": main()
