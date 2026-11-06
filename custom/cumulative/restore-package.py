"""Restore the exact omitted CI files without recompiling or changing the engine."""
from pathlib import Path, PurePosixPath
import argparse
import base64
import hashlib
import json
import stat
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[2]
ENGINE_COMMIT = "29022d6176d82b9281075cc2189d19b11ebce463"
CURRENT = {"run": 42279447874, "artifact": 13537633072, "bytes": 63952619,
           "sha256": "b93a17c47066473232e07460c73eb4077f2df066e5e129f3d6515453a963eb2e"}
RECOVERY = {"run": 42240697023, "artifact": 13520058314, "bytes": 64186799,
            "sha256": "38298fddd7e5c9f0aa53171ac53b97298645055337122626936c0ffed719b917"}
RESTORE = {
    "openmw.cfg": {"bytes": 28865, "sha256": "2291eb4e130b803938b26be461475bef76e9f9035e6889e5d778a5b52423ea66"},
    "lua51.dll": {"bytes": 546304, "sha256": "adb11e30f7a5eed80eb8c367b35c1479c94f3fcdc2a8cd65307a2fcfaf46e700"},
    "resources/lua_libs/content.lua": {"bytes": 3524, "sha256": "611fc12b070c161c11dccc324e6fd74df16aaffe17b8565e716992ca1796be69"},
}
PACKAGING_PATHS = {
    ".github/workflows/push.yml", "custom/cumulative/prepare-upgrade.py",
    "custom/cumulative/restore-package.py", "custom/cumulative/check-startup.ps1",
    "custom/cumulative/source-plan.json", "custom/cumulative/README.md",
}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def identity(data):
    return {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}


def json_bytes(value):
    return (json.dumps(value, indent=2) + "\n").encode("utf-8")


def check_archive(path, expected):
    actual = identity(path.read_bytes())
    require(actual == {k: expected[k] for k in ("bytes", "sha256")}, "Wrong CI archive: " + str(path))


def canonical_members(archive):
    result, seen = {}, set()
    for info in archive.infolist():
        name = info.filename
        p = PurePosixPath(name)
        require(name and not p.is_absolute() and name == p.as_posix() + ("/" if info.is_dir() else ""), name)
        require(all(part not in {".", ".."} and part.rstrip(" .") == part for part in p.parts), name)
        require(all(ord(c) >= 32 and ord(c) != 127 and c not in '<>:"\\|?*' for c in name), name)
        key = name.rstrip("/").casefold()
        require(key not in seen and not info.flag_bits & 1 and not stat.S_ISLNK(info.external_attr >> 16), name)
        seen.add(key)
        if info.is_dir():
            require(info.file_size == 0 and archive.read(info) == b"", name)
        else:
            require(p.suffix.lower() not in {".esm", ".esp", ".omwaddon", ".omwgame"}, name)
            result[name] = info
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--current", required=True, type=Path)
    parser.add_argument("--recovery", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    output = args.output.resolve()
    require(output == ROOT / "install", "Only a fresh repository install/ directory is supported")
    require(not output.exists() or not any(output.iterdir()), "Output must be empty; existing installations are unsupported")

    assembly_commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT).decode().strip()
    committed_script = subprocess.check_output(["git", "show", "HEAD:custom/cumulative/restore-package.py"], cwd=ROOT)
    require(committed_script == Path(__file__).read_bytes(), "The restoration script must match the claimed committed source")
    subprocess.run(["git", "merge-base", "--is-ancestor", ENGINE_COMMIT, "HEAD"], cwd=ROOT, check=True)
    changed = subprocess.check_output(["git", "diff", "--name-only", ENGINE_COMMIT, "HEAD"], cwd=ROOT).decode().splitlines()
    require(set(changed) <= PACKAGING_PATHS, "Engine/source changes cannot reuse an earlier executable: " + repr(changed))
    require(not subprocess.check_output(["git", "diff", "--name-only", "HEAD"], cwd=ROOT).strip(), "Commit package scripts before assembly")
    check_archive(args.current, CURRENT)
    check_archive(args.recovery, RECOVERY)
    with zipfile.ZipFile(args.current) as z:
        names = canonical_members(z)
        files = {name: z.read(info) for name, info in names.items()}
    inventory = json.loads(files["PACKAGE-SHA256.json"])
    require(set(inventory["files"]) == set(files) - {"PACKAGE-SHA256.json"}, "Original inventory census mismatch")
    for name, expected in inventory["files"].items():
        require(identity(files[name]) == expected, "Original file mismatch: " + name)
    original = json.loads(files["verification/CI-PACKAGE-SHA256.json"])
    omission = json.loads(files["UPGRADE-PRESERVATION.json"])
    require(original["commit"] == omission["commit"] == ENGINE_COMMIT, "Original build identity mismatch")
    require(set(original["files"]) - set(files) == set(RESTORE), "Unexpected original omissions")
    for name, expected in RESTORE.items():
        require(name not in files and original["files"][name] == expected, "Restoration binding mismatch: " + name)
        require(omission["omitted_ci_files"][name]["present_in_ci_install"] is True, name)

    with zipfile.ZipFile(args.recovery) as z:
        names = canonical_members(z)
        for name, expected in RESTORE.items():
            data = z.read(names[name])
            require(identity(data) == expected, "Recovery file mismatch: " + name)
            files[name] = data
    require(files["openmw.cfg"] == (ROOT / "files/openmw.cfg.local").read_bytes(), "Bootstrap differs from engine source")
    require(files["resources/lua_libs/content.lua"] == (ROOT / "components/lua_ui/content.lua").read_bytes(), "Internal Lua helper differs from engine source")
    for name, expected in original["files"].items():
        require(identity(files[name]) == expected, "Complete original CI file set differs: " + name)
    require(base64.b64decode(b"".join(files["defaults.bin"].split()), validate=True)
            == (ROOT / "files/settings-default.cfg").read_bytes(), "Merged defaults differ")
    require(not {"version.dll", "openmw_fg.ini"} & set(files), "Optional FG installation must remain separate")

    # Preserve the previous packaging records as historical evidence, then replace
    # the root instructions and inventory with accurate complete-package metadata.
    for name in ("UPGRADE-PRESERVATION.json", "PACKAGE-SHA256.json", "CUMULATIVE-UPGRADE.md", "CI-ID.txt"):
        historical_name = "PACKAGE-SHA256.original.json" if name == "PACKAGE-SHA256.json" else name
        files["verification/original-upgrade/" + historical_name] = files.pop(name)
    files["CUMULATIVE-UPGRADE.md"] = (ROOT / "custom/cumulative/README.md").read_bytes()
    files["CI-ID.txt"] = (
        "Commit " + ENGINE_COMMIT + "\n"
        "Native build/test run: " + str(CURRENT["run"]) + "\n"
        "Package assembly commit: " + assembly_commit + "\n"
        "Complete standard Windows package: original bootstrap, Lua DLL and internal helper restored\n"
        "All native binaries and other engine resources retain their tested CI bytes\n"
        "Custom Rubic0n users: retain/reapply your matching lua51.dll and resources/lua_libs/content.lua together\n"
        "Frame generation remains a separate optional DLL/configuration\n"
    ).encode("utf-8")
    repaired = {
        "result": "PASS_FOR_COMPLETE_CI_FILE_RESTORATION",
        "compiled_engine_commit": ENGINE_COMMIT, "package_assembly_commit": assembly_commit,
        "original_native_build": CURRENT, "recovery_archive": RECOVERY,
        "restored_files": RESTORE, "original_ci_file_count": len(original["files"]),
        "all_original_ci_runtime_bytes_restored": True,
        "native_binaries_unchanged": True, "native_tests_reused_from_original_run": True,
        "documentation_replaced": ["CUMULATIVE-UPGRADE.md", "CI-ID.txt"],
        "historical_packaging_records": "verification/original-upgrade",
        "optional_fg_files_absent": ["version.dll", "openmw_fg.ini"],
        "runtime": "Original standard CI Lua DLL and internal helper included as a pair",
        "actual_windows_startup": "Not executed by this restoration script; see the separate check-startup.ps1 report",
        "in_game_tested": False, "user_configuration_included": False,
    }
    files["PACKAGE-REPAIR.json"] = json_bytes(repaired)
    files["PACKAGE-SHA256.json"] = json_bytes({"tests": inventory["tests"],
        "files": {name: identity(data) for name, data in sorted(files.items())}})
    output.mkdir(exist_ok=True)
    for name, data in sorted(files.items()):
        path = output / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
    actual = {p.relative_to(output).as_posix(): identity(p.read_bytes()) for p in output.rglob("*") if p.is_file()}
    require(actual == {name: identity(data) for name, data in files.items()}, "Output file readback differs")
    print(json.dumps({"result": repaired["result"], "compiled_engine_commit": ENGINE_COMMIT,
        "package_assembly_commit": assembly_commit, "files": len(files), "restored": sorted(RESTORE)}, indent=2))


if __name__ == "__main__":
    main()
