"""Add verified generic native-job tools and reviewed mod adapters to the complete CI package."""
from pathlib import Path
import argparse
import hashlib
import importlib.util
import json
import shutil
import subprocess


ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
COMPONENTS = {
    "SaneMagic": {"scripts/SaneMagic/npc.lua", "scripts/SaneMagic/daesumm_pn.lua"},
    "AdvancedWorldMap": {"scripts/advanced_world_map/player.lua", "scripts/advanced_world_map/global.lua"},
    "QuestGuiderLite": {"scripts/quest_guider_lite/main.lua"},
    "ProximityTool": {"scripts/proximityTool/activeMarkers.lua"},
}


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def module(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


def verify_sanemagic(manifest):
    evidence = json.loads((ROOT / "sanemagic-adapter-parity.json").read_text())
    cases = evidence.get("cases", [])
    expected = {(name, runtime, native) for name in ("npc.lua", "daesumm_pn.lua")
                for runtime in ("lupa.lua51", "lupa.luajit21") for native in (False, True)}
    if (evidence.get("result") != "PASS" or evidence.get("lupa_version") != "2.8"
            or {(row["file"], row["runtime"], row["native_queries"]) for row in cases} != expected):
        raise ValueError("Both Sane Magic adapters must pass in both Lua runtimes and query modes")
    by_name = {Path(row["path"]).name: row for row in manifest["files"]}
    for case in cases:
        row = by_name[case["file"]]
        if (case["result"] != "PASS" or case["snapshots_compared"] <= 0
                or case["baseline_sha256"] != row["expected_sha256"]
                or case["adapter_sha256"] != row["installed_sha256"]):
            raise ValueError("Sane Magic parity evidence does not bind the reviewed adapter sources")


def verify_other_adapters(manifests):
    evidence = json.loads((ROOT / "native-job-adapter-parity.json").read_text())
    cases = evidence.get("cases", [])
    expected = {(component, path, runtime)
                for component, paths in COMPONENTS.items() if component != "SaneMagic"
                for path in paths for runtime in ("lupa.lua51", "lupa.luajit21")}
    actual = {(row["component"], row["path"], row["runtime"]) for row in cases}
    if (evidence.get("result") != "PASS" or evidence.get("lupa_version") != "2.8"
            or len(cases) != len(expected) or actual != expected):
        raise ValueError("Native-job adapters did not pass exact identity, syntax and distance parity checks")
    for case in cases:
        row = {item["path"]: item for item in manifests[case["component"]]["files"]}[case["path"]]
        if (case["result"] != "PASS" or case["baseline_sha256"] != row["expected_sha256"]
                or case["adapter_sha256"] != row["installed_sha256"]):
            raise ValueError("Adapter parity evidence does not bind the reviewed source: " + case["path"])


def write_root_adapter_command(destination, restore=False):
    components = list(COMPONENTS)
    if restore:
        components.reverse()
    operation = "-RestoreLatest" if restore else ""
    success = "RESTORED" if restore else "INSTALLED"
    lines = ["@echo off", "setlocal", "set \"ToolRoot=%~dp0tools\\combined-performance\""]
    for component in components:
        lines += [
            f'echo {"Restoring" if restore else "Installing"} {component}...',
            f'powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%ToolRoot%\\{component}\\Install-Fix.ps1" {operation}'.rstrip(),
            "if errorlevel 1 goto :failed",
        ]
    lines += ["echo.", f"echo {success}: all four native-job adapter components.", "pause", "exit /b 0",
              ":failed", "echo.", "echo Adapter operation stopped. Earlier successful components retain their verified backups.",
              "pause", "exit /b 1", ""]
    name = "Restore-Native-Job-Adapters.cmd" if restore else "Install-Native-Job-Adapters.cmd"
    (destination / name).write_bytes("\r\n".join(lines).encode())


def prepare(destination):
    destination = destination.resolve()
    if destination != ROOT / "install" or not (destination / "openmw.exe").is_file():
        raise ValueError("Only the fresh complete CI install tree is supported")
    target = destination / "tools/combined-performance"
    if target.exists():
        raise ValueError("Native-job tools were already packaged")
    commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT).decode().strip()

    source_manifests = {}
    for name, phase in (("town-performance-source-manifest.json", "committed_source"),
                        ("town-performance-build-source-manifest.json", "after_lookup")):
        report = json.loads((ROOT / name).read_text())
        if (report.get("result") != "PASS" or report.get("phase") != phase
                or report.get("commit") != commit or not report.get("accepted_heads_are_ancestors_of_commit")):
            raise ValueError("Cumulative source verification failed: " + name)
        source_manifests[name] = report
    query = module(HERE / "verify-native.py", "combined_native_query_verification")
    report = query.verify(destination / "openmw-tests.xml",
                          ROOT / "combined-performance-off-tests.xml")
    if json.loads((ROOT / "combined-native-tests.json").read_text()) != report:
        raise ValueError("Native report does not bind the packaged test execution")
    parity = module(ROOT / "custom/town-performance/verify-combined-layout.py", "combined_layout_verification")
    ui = parity.compare(destination / "openmw-tests.xml", ROOT / "lua-ui-legacy-updates-tests.xml",
                        ROOT / "combined-performance-off-tests.xml")
    if ui != json.loads((ROOT / "combined-layout-parity.json").read_text()):
        raise ValueError("Layout parity report does not bind all three process modes")

    manifests = {}
    for component, expected_paths in COMPONENTS.items():
        manifest = json.loads((HERE / "mods" / component / "manifest.json").read_text())
        if {row["path"] for row in manifest["files"]} != expected_paths:
            raise ValueError("Unexpected adapter scope for " + component)
        manifests[component] = manifest
    verify_sanemagic(manifests["SaneMagic"])
    verify_other_adapters(manifests)

    launcher_reports = ("combined-launcher-windows-powershell.json", "combined-launcher-pwsh.json")
    for name in launcher_reports:
        evidence = json.loads((ROOT / name).read_text(encoding="utf-8-sig"))
        checks = evidence.get("cases", [])
        if (evidence.get("result") != "PASS" or len(checks) != 10
                or {case["name"] for case in checks} != {"Combined", "Inherited", "PreviousFrameUpdates", "PreviousHud", "PreviousSpellReads", "PreviousParallelQueries", "WithoutRayBatch", "WithoutAsyncDistances", "WithoutLuaWorkers", "CollectorFailure"}
                or any(case["result"] != "PASS" or case.get("environment_restored") is not True for case in checks)):
            raise ValueError("Launcher mode or environment-restoration checks did not pass: " + name)

    target.mkdir(parents=True)
    for name in ("Start-Combined.ps1", "Run-Combined.cmd", "Run-Inherited.cmd", "Run-Previous-Frame-Updates.cmd", "Run-Previous-HUD.cmd", "Run-Previous-Spell-Reads.cmd", "Run-Previous-Parallel-Queries.cmd", "Run-Without-Ray-Batching.cmd", "Run-Without-Async-Distances.cmd", "Run-Without-Lua-Workers.cmd"):
        shutil.copy2(HERE / name, target / name)
    shutil.copytree(HERE / "Proximity Tool - Sparse HUD", target / "Proximity Tool - Sparse HUD")
    shutil.copytree(HERE / "Parallel Query Adapters", target / "Parallel Query Adapters")
    (target / "capture").mkdir()
    shutil.copy2(ROOT / "custom/performance-capture-followup/Start-Capture.ps1", target / "capture/Start-Capture.ps1")

    packaged_manifests = {}
    for component, manifest in manifests.items():
        adapter = HERE / "mods" / component
        package = target / component
        package.mkdir()
        manifest["allowed_revisions"] = sorted(set(manifest["allowed_revisions"]) | {commit, commit[:10]})
        manifest["candidate_commit"] = commit
        for row in manifest["files"]:
            source = adapter / row["path"]
            if sha(source) != row["installed_sha256"]:
                raise ValueError(component + " adapter differs from its reviewed identity: " + row["path"])
            output = package / "Data Files" / row["path"]
            output.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, output)
        for name in ("Install.cmd", "Restore.cmd", "README.md"):
            shutil.copy2(adapter / name, package / name)
        shutil.copy2(ROOT / "custom/gameplay-responsiveness/Install-Fix.ps1", package / "Install-Fix.ps1")
        (package / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
        packaged_manifests[component] = sha(package / "manifest.json")

    shutil.copy2(HERE / "README.md", destination / "COMBINED-PERFORMANCE.md")
    evidence_files = ("combined-native-tests.json", "combined-layout-parity.json",
                      "lua-ui-legacy-updates-tests.xml", "combined-performance-off-tests.xml",
                      "sanemagic-adapter-parity.json", "native-job-adapter-parity.json",
                      "native-jobs-tests.json", "town-frame-updates.json", "town-previous-components.xml",
                      "town-previous-engine.xml", "previous-hud-tests.xml", "sparse-hud-tests.json",
                      "previous-spell-reads.xml", "active-spell-reads.json", "previous-parallel-queries.xml", "map-icon-skin-tests.json", *launcher_reports)
    for name in evidence_files:
        shutil.copy2(ROOT / name, destination / name)
    for name, command in (("Run-Combined.cmd", "Run-Combined.cmd"), ("Run-Inherited.cmd", "Run-Inherited.cmd"),
                          ("Run-Previous-Frame-Updates.cmd", "Run-Previous-Frame-Updates.cmd"),
                          ("Run-Previous-HUD.cmd", "Run-Previous-HUD.cmd"),
                          ("Run-Previous-Spell-Reads.cmd", "Run-Previous-Spell-Reads.cmd"),
                          ("Run-Previous-Parallel-Queries.cmd", "Run-Previous-Parallel-Queries.cmd"),
                          ("Run-Without-Ray-Batching.cmd", "Run-Without-Ray-Batching.cmd"),
                          ("Run-Without-Async-Distances.cmd", "Run-Without-Async-Distances.cmd"),
                          ("Run-Without-Lua-Workers.cmd", "Run-Without-Lua-Workers.cmd")):
        (destination / name).write_bytes(
            ('@echo off\r\ncall "%~dp0tools\\combined-performance\\' + command + '"\r\n').encode())
    write_root_adapter_command(destination)
    write_root_adapter_command(destination, restore=True)

    evidence = {
        "schema_version": 2,
        "result": "PASS",
        "candidate_commit": commit,
        "source_parent": "8bd0d90b2727b8d654288d5c42e45a3edefc7146",
        "source_manifest_sha256": {name: sha(destination / name) for name in source_manifests},
        "native_query_verification_sha256": sha(destination / "combined-native-tests.json"),
        "native_layout_verification_sha256": sha(destination / "combined-layout-parity.json"),
        "town_frame_verification_sha256": sha(destination / "town-frame-updates.json"),
        "adapter_parity_sha256": {
            "SaneMagic": sha(destination / "sanemagic-adapter-parity.json"),
            "OtherAdapters": sha(destination / "native-job-adapter-parity.json"),
        },
        "launcher_verification_sha256": {name: sha(destination / name) for name in launcher_reports},
        "adapter_manifest_sha256": packaged_manifests,
        "installer_sha256": sha(target / "SaneMagic/Install-Fix.ps1"),
        "tools": {p.relative_to(target).as_posix(): sha(p) for p in sorted(target.rglob("*")) if p.is_file()},
        "gameplay_fps_measured": False,
        "installed_custom_lua_runtime_tested": False,
        "runtime_pair_policy": "Keep the user's custom lua51.dll and matching content.lua together when upgrading",
    }
    (destination / "COMBINED-PACKAGE.json").write_text(json.dumps(evidence, indent=2) + "\n")
    print("PASS: generic native-job evidence, launch tools and all four adapter components packaged.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("destination", type=Path)
    prepare(parser.parse_args().destination)
