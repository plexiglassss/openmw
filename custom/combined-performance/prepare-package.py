"""Add verified combined-candidate tools and prepared mod adapters to the complete CI package."""
from pathlib import Path
import argparse
import hashlib
import importlib.util
import json
import shutil
import subprocess


ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def module(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


def prepare(destination):
    destination = destination.resolve()
    if destination != ROOT / "install" or not (destination / "openmw.exe").is_file():
        raise ValueError("Only the fresh complete CI install tree is supported")
    target = destination / "tools/combined-performance"
    if target.exists():
        raise ValueError("Combined tools were already packaged")
    commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT).decode().strip()
    query = module(HERE / "verify-native.py", "combined_native_query_verification")
    report = query.verify(destination / "openmw-tests.xml", ROOT / "combined-performance-off-tests.xml")
    saved = json.loads((ROOT / "combined-native-tests.json").read_text())
    if saved != report:
        raise ValueError("Combined native report does not bind the packaged test execution")
    parity = module(ROOT / "custom/town-performance/verify-combined-layout.py", "combined_layout_verification")
    ui = parity.compare(destination / "openmw-tests.xml", ROOT / "lua-ui-legacy-updates-tests.xml",
                        ROOT / "combined-performance-off-tests.xml")
    if ui != json.loads((ROOT / "combined-layout-parity.json").read_text()):
        raise ValueError("Combined layout parity report does not bind all three process modes")

    target.mkdir(parents=True)
    for name in ("Start-Combined.ps1", "Run-Combined.cmd", "Run-Inherited.cmd"):
        shutil.copy2(HERE / name, target / name)
    (target / "capture").mkdir()
    shutil.copy2(ROOT / "custom/performance-capture-followup/Start-Capture.ps1", target / "capture/Start-Capture.ps1")
    adapter = HERE / "mods/SaneMagic"
    manifest = json.loads((adapter / "manifest.json").read_text())
    if {row["path"] for row in manifest["files"]} != {
            "scripts/SaneMagic/npc.lua", "scripts/SaneMagic/daesumm_pn.lua"}:
        raise ValueError("Unexpected SaneMagic adapter scope")
    script_parity_path = ROOT / "sanemagic-adapter-parity.json"
    script_parity = json.loads(script_parity_path.read_text())
    cases = script_parity.get("cases", [])
    expected_cases = {(name, runtime, native) for name in ("npc.lua", "daesumm_pn.lua")
                      for runtime in ("lupa.lua51", "lupa.luajit21") for native in (False, True)}
    if (script_parity.get("result") != "PASS" or script_parity.get("lupa_version") != "2.8"
            or len(cases) != len(expected_cases)
            or {(row["file"], row["runtime"], row["native_queries"]) for row in cases} != expected_cases):
        raise ValueError("Both SaneMagic adapters must pass in both Lua runtimes and query modes")
    by_name = {Path(row["path"]).name: row for row in manifest["files"]}
    for case in cases:
        row = by_name[case["file"]]
        if (case["result"] != "PASS" or case["snapshots_compared"] <= 0
                or case["baseline_sha256"] != row["expected_sha256"]
                or case["adapter_sha256"] != row["installed_sha256"]):
            raise ValueError("SaneMagic parity evidence does not bind the reviewed adapter sources")
    launcher_reports = ("combined-launcher-windows-powershell.json", "combined-launcher-pwsh.json")
    for name in launcher_reports:
        evidence = json.loads((ROOT / name).read_text(encoding="utf-8-sig"))
        checks = evidence.get("cases", [])
        if (evidence.get("result") != "PASS" or len(checks) != 3
                or {case["name"] for case in checks} != {"Combined", "Inherited", "CollectorFailure"}
                or any(case["result"] != "PASS" or case.get("environment_restored") is not True for case in checks)):
            raise ValueError("Combined launcher mode or environment-restoration checks did not pass: " + name)
    package = target / "SaneMagic"
    package.mkdir()
    manifest["allowed_revisions"] = sorted(set(manifest["allowed_revisions"]) | {commit, commit[:10]})
    manifest["candidate_commit"] = commit
    for row in manifest["files"]:
        source = adapter / row["path"]
        if sha(source) != row["installed_sha256"]:
            raise ValueError("SaneMagic adapter differs from its reviewed identity: " + row["path"])
        output = package / "Data Files" / row["path"]
        output.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, output)
    for name in ("Install.cmd", "Restore.cmd", "README.md"):
        shutil.copy2(adapter / name, package / name)
    shutil.copy2(ROOT / "custom/gameplay-responsiveness/Install-Fix.ps1", package / "Install-Fix.ps1")
    (package / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    shutil.copy2(HERE / "README.md", destination / "COMBINED-PERFORMANCE.md")
    for name in ("combined-native-tests.json", "combined-layout-parity.json",
                 "lua-ui-legacy-updates-tests.xml", "combined-performance-off-tests.xml",
                 "sanemagic-adapter-parity.json", *launcher_reports):
        shutil.copy2(ROOT / name, destination / name)
    for name, command in (("Run-Combined.cmd", "Run-Combined.cmd"), ("Run-Inherited.cmd", "Run-Inherited.cmd")):
        (destination / name).write_bytes(
            ('@echo off\r\ncall "%~dp0tools\\combined-performance\\' + command + '"\r\n').encode())
    (destination / "Install-SaneMagic-Adapters.cmd").write_bytes(
        b'@echo off\r\ncall "%~dp0tools\\combined-performance\\SaneMagic\\Install.cmd"\r\n')
    evidence = {
        "schema_version": 1, "result": "PASS", "candidate_commit": commit,
        "source_parent": "016577e6e33b16080e9c6828b38a3879526714d7",
        "native_query_verification_sha256": sha(destination / "combined-native-tests.json"),
        "native_layout_verification_sha256": sha(destination / "combined-layout-parity.json"),
        "script_parity_sha256": sha(destination / "sanemagic-adapter-parity.json"),
        "launcher_verification_sha256": {name: sha(destination / name) for name in launcher_reports},
        "adapter_manifest_sha256": sha(package / "manifest.json"),
        "installer_sha256": sha(package / "Install-Fix.ps1"),
        "tools": {p.relative_to(target).as_posix(): sha(p) for p in sorted(target.rglob("*")) if p.is_file()},
        "gameplay_fps_measured": False,
        "installed_custom_lua_runtime_tested": False,
        "runtime_pair_policy": "Keep the user's custom lua51.dll and matching content.lua together when upgrading",
    }
    (destination / "COMBINED-PACKAGE.json").write_text(json.dumps(evidence, indent=2) + "\n")
    print("PASS: combined native evidence, launch tools and both SaneMagic adapters packaged.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("destination", type=Path)
    prepare(parser.parse_args().destination)
