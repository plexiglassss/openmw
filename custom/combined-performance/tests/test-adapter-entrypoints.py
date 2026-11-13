"""Exercise the shipped CMD files with Windows PowerShell 5.1 on a disposable hosted runner."""
import argparse
import gzip
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import uuid

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
INSTALLER = ROOT / "custom/gameplay-responsiveness/Install-Fix.ps1"
ORIGINAL = "12e5b050cb144396f213bb5ce193b6a7e7e7fce1"


def sha(data):
    return hashlib.sha256(data).hexdigest()


def write(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


def command(script, cwd, arguments="", *, expect=0, messages=()):
    result = subprocess.run(f'cmd.exe /d /c call "{script}" {arguments}', cwd=cwd, input=b"\r\n",
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=90)
    output = result.stdout.decode("utf-8", errors="replace")
    if result.returncode != expect or any(message not in output for message in messages):
        raise AssertionError(f"{script.name} exited {result.returncode}, expected {expect}:\n{output}")
    return output


def run(output):
    if os.name != "nt" or os.environ.get("RUNNER_ENVIRONMENT") != "github-hosted":
        raise SystemExit("The test uses default Documents paths and requires a disposable GitHub-hosted Windows runner.")
    metadata = subprocess.check_output(["powershell.exe", "-NoProfile", "-Command",
        "$PSVersionTable.PSVersion.ToString(); [Environment]::GetFolderPath('MyDocuments')"], text=True).splitlines()
    version, documents = metadata
    if not version.startswith("5.1.") or not Path(documents).is_absolute():
        raise RuntimeError("The test requires Windows PowerShell 5.1 and a valid Documents folder")
    spec = importlib.util.spec_from_file_location("adapter_packaging", HERE.parent / "prepare-package.py")
    packaging = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(packaging)
    log = Path(documents) / "My Games/OpenMW/openmw.log"
    backup_base = Path(documents) / "OpenMW Responsiveness Backups"
    fixture_backups = []
    cases = []
    original_script = subprocess.check_output(["git", "show", ORIGINAL + ":custom/gameplay-responsiveness/Install-Fix.ps1"], cwd=ROOT)
    with tempfile.TemporaryDirectory() as temp:
        work = Path(temp)
        engine = work / "OpenMW 0.52 - latest custom build"
        provider = work / "Mod data [fixture]"
        engine.mkdir()
        cfg = work / "openmw.cfg"
        cfg.write_bytes(b"# Windows adapter installer fixture\n")
        packaging.write_root_adapter_command(engine)
        packaging.write_root_adapter_command(engine, restore=True)
        setup = engine / "Install-Native-Job-Adapters.cmd"
        restore = engine / "Restore-Native-Job-Adapters.cmd"
        tools = engine / "tools/combined-performance"
        pairs = {}
        for name in packaging.COMPONENTS:
            packaged = tools / name
            packaged.mkdir(parents=True)
            component = HERE.parent / "mods" / name
            for filename in ("Install.cmd", "Restore.cmd"):
                shutil.copyfile(component / filename, packaged / filename)
            shutil.copyfile(INSTALLER, packaged / "Install-Fix.ps1")
            manifest = json.loads((component / "manifest.json").read_bytes())
            manifest["id"] = "native-installer-ci-" + uuid.uuid4().hex
            fixture_backups.append(backup_base / manifest["id"])
            manifest["expected_config_sha256"] = [sha(cfg.read_bytes())]
            manifest["expected_data_order_sha256"] = sha(str(provider).encode())
            for row in manifest["files"]:
                relative = Path(row["path"])
                baseline = (HERE / "fixtures/SaneMagic" / (relative.name + ".gz") if name == "SaneMagic" else
                            HERE / "fixtures/NativeJobAdapters" / (name + "-" + relative.name + ".gz"))
                before = gzip.decompress(baseline.read_bytes())
                after = (component / relative).read_bytes()
                assert (sha(before), sha(after)) == (row["expected_sha256"], row["installed_sha256"])
                write(provider / relative, before)
                write(packaged / "Data Files" / relative, after)
                pairs[provider / relative] = (before, after)
            (packaged / "manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
        # These are the literal no-argument CMD launchers used by players. Only create a new log on the hosted runner.
        log.parent.mkdir(parents=True, exist_ok=True)
        with log.open("x", encoding="utf-8") as stream:
            stream.write(f"[12:00:00 *] Loading config file: {cfg}\n[12:00:01 I] Revision: 8bd0d90b27\n"
                         f"[12:00:02 I] Adding data directory {provider}\n")
        try:
            name = tools / "SaneMagic/Install-Fix.ps1"
            name.write_bytes(original_script)
            command(setup, engine, expect=1, messages=("Installing SaneMagic", "GetFullPath", "not of a legal form"))
            assert all(path.read_bytes() == pair[0] for path, pair in pairs.items())
            assert not any(p.exists() for p in fixture_backups)
            cases.append("original CMD reproduced GetFullPath before any mod or backup write")
            shutil.copyfile(INSTALLER, name)
            for component in packaging.COMPONENTS:
                command(tools / component / "Install.cmd", work, "-CheckOnly", messages=("CHECK PASSED",))
            assert not any(p.exists() for p in fixture_backups)
            cases.append("all four component CMDs found their folders from a different working directory")
            command(setup, engine, messages=("INSTALLED: all four",))
            assert all(path.read_bytes() == pair[1] for path, pair in pairs.items())
            receipts = {p: (p.stat().st_mtime_ns, sha(p.read_bytes())) for root in fixture_backups for p in root.glob("*/receipt.json")}
            assert len(receipts) == 4
            cases.append("normal root CMD installed all six exact Lua payloads and created four backup receipts")
            again = command(setup, work, messages=("INSTALLED: all four",))
            assert again.count("ALREADY INSTALLED") == 4
            assert receipts == {p: (p.stat().st_mtime_ns, sha(p.read_bytes())) for root in fixture_backups for p in root.glob("*/receipt.json")}
            cases.append("second root install recognized all four components and retained the same backups")
            command(restore, work, messages=("RESTORED: all four",))
            assert all(path.read_bytes() == pair[0] for path, pair in pairs.items())
            assert all(json.loads(p.read_bytes())["state"] == "restored" for p in receipts)
            cases.append("root restore from another working directory returned all six files to their original hashes")
        finally:
            log.unlink()
            for path in fixture_backups:
                if path.is_dir():
                    shutil.rmtree(path)
        result = dict(result="PASS", powershell=version, original_script_sha256=sha(original_script),
                      installer_sha256=sha(INSTALLER.read_bytes()), files=len(pairs), cases=cases,
                      cmd_sha256={p.name: sha(p.read_bytes()) for p in (setup, restore)})
        output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
        print(json.dumps(result))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    run(parser.parse_args().output)
