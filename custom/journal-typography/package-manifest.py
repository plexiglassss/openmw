"""Inventory the actual installed Windows package after all tests have passed."""
from pathlib import Path
import hashlib
import json
import sys

root = Path(sys.argv[1]).resolve()
required = ("openmw.exe", "openmw-launcher.exe", "defaults.bin", "CI-ID.txt",
            "JOURNAL-TYPOGRAPHY.md", "journal-typography-source-manifest.json",
            "components-tests.xml", "openmw-tests.xml")
for name in required:
    if not (root / name).is_file():
        raise SystemExit("Missing required build file: " + name)
if not (root / "resources").is_dir():
    raise SystemExit("Missing engine resources directory")
files = {}
for path in sorted(root.rglob("*")):
    if path.is_file() and path.name != "PACKAGE-SHA256.json":
        if path.suffix.lower() in {".esp", ".esm", ".omwaddon", ".omwgame"}:
            raise SystemExit("Unexpected native content plugin in UI engine package: " + str(path))
        files[path.relative_to(root).as_posix()] = {
            "bytes": path.stat().st_size,
            "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
        }
(root / "PACKAGE-SHA256.json").write_text(json.dumps({"files": files}, indent=2) + "\n")
print(f"PASS: inventoried {len(files)} package files; no native content plugins.")
