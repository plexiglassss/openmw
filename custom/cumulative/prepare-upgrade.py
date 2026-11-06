"""Prepare only a fresh CI install tree; never run on a user's game directory."""
from pathlib import Path
import base64
import hashlib
import json
import subprocess
import sys
import xml.etree.ElementTree as ET


ROOT = Path(__file__).resolve().parents[2]
PRESERVE = ("lua51.dll", "resources/lua_libs/content.lua", "version.dll", "openmw.cfg", "openmw_fg.ini")


def identity(path):
    data = path.read_bytes()
    return {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}


def native_tests(path):
    tests = ET.parse(path).getroot()
    cases = list(tests.iter("testcase"))
    identities = {(case.get("classname"), case.get("name")) for case in cases}
    if (not cases or len(cases) != int(tests.get("tests", "0")) or len(identities) != len(cases)
            or any(int(tests.get(k, "0")) for k in ("failures", "errors", "disabled"))
            or any(node.tag in {"failure", "error", "skipped"} for node in tests.iter())
            or any(case.get("status") != "run" or case.get("result") != "completed" for case in cases)):
        raise ValueError("Incomplete or failed native test evidence: " + path.name)
    return {**identity(path), "tests": len(cases), "failures": 0, "errors": 0, "disabled": 0, "skipped": 0}


def prepare(root, source_root):
    if root == source_root or root != source_root / "install":
        raise ValueError("Only the repository's fresh CI install directory is supported")
    if (root / "UPGRADE-PRESERVATION.json").exists() or (root / "verification/CI-PACKAGE-SHA256.json").exists():
        raise ValueError("Upgrade tree was already prepared; rebuild the fresh CI install tree")
    for name in ("openmw.exe", "openmw-launcher.exe", "defaults.bin", "CI-ID.txt", "components-tests.xml", "openmw-tests.xml",
                 "cumulative-source-manifest.json", "cumulative-build-source-manifest.json"):
        if not (root / name).is_file():
            raise ValueError("Missing required CI output: " + name)
    if not (root / "resources").is_dir():
        raise ValueError("Missing engine resources")
    test_results = {name: native_tests(root / name) for name in ("components-tests.xml", "openmw-tests.xml")}
    commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=source_root).decode().strip()
    source_plan_hash = identity(source_root / "custom/cumulative/source-plan.json")["sha256"]
    manifests = {}
    for name, phase in (("cumulative-source-manifest.json", "committed_source"),
                        ("cumulative-build-source-manifest.json", "after_lookup")):
        data = json.loads((root / name).read_text())
        if (data.get("result") != "PASS" or data.get("commit") != commit or data.get("phase") != phase
                or data.get("source_plan_sha256") != source_plan_hash
                or data.get("accepted_heads_are_ancestors_of_commit") is not True):
            raise ValueError("Cumulative source manifest belongs to another source or phase: " + name)
        manifests[name] = {**identity(root / name), "commit": commit, "phase": phase}
    expected = (source_root / "files/settings-default.cfg").read_bytes()
    decoded = base64.b64decode(b"".join((root / "defaults.bin").read_bytes().split()), validate=True)
    if decoded != expected:
        raise ValueError("Compiled defaults are not the exact merged source defaults")
    files = {}
    for path in root.rglob("*"):
        if path.is_symlink() or getattr(path, "is_junction", lambda: False)():
            raise ValueError("Unsupported symlink or junction in CI install output")
        relative = path.relative_to(root)
        for component in relative.parts:
            if (component in {".", ".."} or component.rstrip(" .") != component
                    or any(ord(c) < 32 or ord(c) == 127 or c in '<>:"\\|?*' for c in component)):
                raise ValueError("Noncanonical Windows package path: " + relative.as_posix())
        if path.is_file():
            name = relative.as_posix().casefold()
            if name in files:
                raise ValueError("Case-colliding package files: " + name)
            files[name] = path
    before = {path.relative_to(root).as_posix(): identity(path) for path in files.values()}
    removed = {}
    for name in PRESERVE:
        path = files.get(name.casefold())
        if path is not None:
            data = path.read_bytes()
            removed[name] = {"present_in_ci_install": True, "bytes": len(data),
                             "sha256": hashlib.sha256(data).hexdigest()}
        else:
            removed[name] = {"present_in_ci_install": False}
    # Validate everything first, then omit only these explicitly protected paths.
    for name in PRESERVE:
        path = files.get(name.casefold())
        if path is not None:
            path.unlink()
    present = {p.relative_to(root).as_posix().casefold() for p in root.rglob("*") if p.is_file()}
    if present & {name.casefold() for name in PRESERVE}:
        raise ValueError("Protected installed file remains in the upgrade")
    retained = {path.relative_to(root).as_posix(): identity(path) for path in root.rglob("*") if path.is_file()}
    expected_retained = {name: row for name, row in before.items() if name.casefold() not in {p.casefold() for p in PRESERVE}}
    if retained != expected_retained:
        raise ValueError("A retained CI package file changed during upgrade preparation")
    verification = root / "verification/CI-PACKAGE-SHA256.json"
    verification.parent.mkdir(parents=True, exist_ok=True)
    verification.write_text(json.dumps({"commit": commit, "files": before}, indent=2) + "\n")
    report = {"result": "PASS", "commit": commit, "source_plan_sha256": source_plan_hash,
              "source_manifests": manifests, "native_tests": test_results,
              "original_ci_inventory": {"path": "verification/CI-PACKAGE-SHA256.json", **identity(verification)},
              "retained_ci_file_count": len(retained), "retained_bytes_equal_original_ci": True,
              "preserved_installed_paths": list(PRESERVE), "omitted_ci_files": removed,
              "merged_defaults_sha256": hashlib.sha256(expected).hexdigest(),
              "defaults_decoded_bytes_match_source": True,
              "test_runtime": "CI dependency LuaJIT; the user's preserved custom Lua DLL/content pair was not tested",
              "deployment_scope": "Overlay for the existing installation; not a complete fresh installation",
              "frame_generation": "Proxy built and tested separately; no proxy or FG configuration in this upgrade",
              "in_game_tested": False, "installed": False}
    (root / "UPGRADE-PRESERVATION.json").write_text(json.dumps(report, indent=2) + "\n")
    return report


if __name__ == "__main__":
    prepare(Path(sys.argv[1]).resolve(), ROOT)
    print("PASS: exact merged defaults; five installed runtime/configuration paths omitted.")
