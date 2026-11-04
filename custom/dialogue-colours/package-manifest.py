"""Read the installed package after tests; retain the exact delivery inventory."""
from pathlib import Path
import hashlib
import json
import sys
import xml.etree.ElementTree as ET

root = Path(sys.argv[1]).resolve()
for name in ("openmw.exe", "openmw-launcher.exe", "defaults.bin", "CI-ID.txt", "DIALOGUE-COLOURS.md",
             "dialogue-colours-source-manifest.json", "components-tests.xml", "openmw-tests.xml",
             "loading-repair-source-manifest.json", "loading-repair-applied.patch"):
    if not (root / name).is_file():
        raise SystemExit("Missing required build file: " + name)
if not (root / "resources").is_dir():
    raise SystemExit("Missing engine resources")
tests = {}
for name in ("components-tests.xml", "openmw-tests.xml"):
    suite = ET.parse(root / name).getroot()
    if any(int(suite.get(key, "0")) for key in ("failures", "errors", "disabled")):
        raise SystemExit("Failed or disabled tests in " + name)
    if int(suite.get("tests", "0")) == 0:
        raise SystemExit("No tests executed in " + name)
    tests[name] = {key: suite.get(key) for key in ("tests", "failures", "errors", "disabled", "time")}
files = {}
for path in sorted(root.rglob("*")):
    if not path.is_file() or path.name == "PACKAGE-SHA256.json":
        continue
    if path.suffix.lower() in {".esp", ".esm", ".omwaddon", ".omwgame"}:
        raise SystemExit("Unexpected native plugin in engine package: " + str(path))
    files[path.relative_to(root).as_posix()] = {
        "bytes": path.stat().st_size,
        "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
    }
(root / "PACKAGE-SHA256.json").write_text(json.dumps({"tests": tests, "files": files}, indent=2) + "\n")
print(f"PASS: {len(files)} package files inventoried; tests successful; no native plugins.")
