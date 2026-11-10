"""Complete and exercise the public x64 applications in the staged Windows distribution."""
import argparse
import base64
import ctypes
from ctypes import wintypes
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile
import time

PUBLIC = ("openmw", "openmw-launcher", "openmw-wizard", "openmw-iniimporter", "openmw-navmeshtool",
          "openmw-cs", "bsatool", "esmtool", "niftest", "openmw-essimporter", "openmw-bulletobjecttool")
EXTRAS = PUBLIC[5:]
RUNTIME = ("vcruntime140.dll", "vcruntime140_1.dll", "msvcp140.dll", "msvcp140_1.dll",
           "msvcp140_2.dll", "msvcp140_atomic_wait.dll")
ALIAS = b'@echo off\r\ncall "%~dp0tools\\combined-performance\\SaneMagic\\Install.cmd"\r\n'
ROOT = Path(__file__).resolve().parents[2]


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def stage(build, install):
    for name in [*(tool + ".exe" for tool in EXTRAS), "defaults-cs.bin"]:
        target = install / name
        source = build / name
        if target.exists() and digest(target) != digest(source):
            raise ValueError("Unexpected existing tool differs from this build: " + name)
        if not target.exists():
            shutil.copy2(source, target)
    # Projects may add editor-only support files alongside engine resources.
    for path in sorted((build / "resources").rglob("*")):
        if path.is_file():
            dest = install / path.relative_to(build)
            if not dest.exists():
                dest.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(path, dest)
    alias = install / "Install-SaneMagic-Adapters.cmd"
    if alias.exists() and alias.read_bytes() != ALIAS:
        raise ValueError("Conflicting Sane Magic shortcut in the installation")
    alias.write_bytes(ALIAS)


def confirm_distribution(install):
    required = [*(name + ".exe" for name in PUBLIC), *RUNTIME, "redist/vc_redist.x64.exe",
                "defaults.bin", "defaults-cs.bin", "openmw.cfg", "lua51.dll", "resources/lua_libs/content.lua",
                "resources/defaultfilters", "gamecontrollerdb.txt", "platforms/qwindows.dll",
                "Install-SaneMagic-Adapters.cmd", "tools/combined-performance/SaneMagic/Install.cmd",
                "Install-Native-Job-Adapters.cmd", "Restore-Native-Job-Adapters.cmd", "Run-Combined.cmd", "Run-Inherited.cmd"]
    missing = [name for name in required if not (install / name).is_file()]
    if missing:
        raise ValueError(f"Missing Windows distribution files: {missing}")
    decoded = base64.b64decode(b"".join((install / "defaults-cs.bin").read_bytes().split()), validate=True)
    if decoded != (ROOT / "files/openmw-cs.cfg").read_bytes():
        raise ValueError("Editor defaults do not match the source")

    system = Path(os.environ["SystemRoot"]) / "System32"
    binaries = [p for p in install.rglob("*") if p.suffix.lower() in (".exe", ".dll")
                and p.parent != install / "redist"]
    imports = {}
    for binary in sorted(binaries):
        with binary.open("rb") as stream:
            if stream.read(2) != b"MZ":
                raise ValueError(f"Invalid executable: {binary}")
            stream.seek(60)
            stream.seek(struct.unpack("<I", stream.read(4))[0])
            if stream.read(6) != b"PE\0\0\x64\x86":
                raise ValueError(f"Incorrect PE architecture: {binary}")
        output = subprocess.check_output(["dumpbin", "/nologo", "/dependents", str(binary)],
                                         text=True, errors="replace")
        deps = sorted(set(re.findall(r"^\s*([\w.+-]+\.dll)\s*$", output, flags=re.I | re.M)))
        for dll in deps:
            name = dll.lower()
            if (install / dll).is_file() or (binary.parent / dll).is_file():
                continue
            if name.startswith(("api-ms-win-", "ext-ms-win-")):
                continue
            if not name.startswith(("msvcp14", "vcruntime14", "concrt14", "vccorlib14")) and (system / dll).is_file():
                continue
            raise ValueError(f"{binary.name} imports missing DLL {dll}")
        imports[binary.relative_to(install).as_posix()] = deps
    return imports


def wait_for_editor(install, environ):
    user = ctypes.WinDLL("user32", use_last_error=True)
    callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    user.EnumWindows.argtypes = (callback_type, wintypes.LPARAM)
    user.GetWindowThreadProcessId.argtypes = (wintypes.HWND, ctypes.POINTER(wintypes.DWORD))
    user.IsWindowVisible.argtypes = (wintypes.HWND,)
    user.GetWindowTextW.argtypes = (wintypes.HWND, wintypes.LPWSTR, ctypes.c_int)
    user.PostMessageW.argtypes = (wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM)
    process = subprocess.Popen([str(install / "openmw-cs.exe")], cwd=install, env=environ)
    try:
        titles = set()
        handles = []

        @callback_type
        def inspect(hwnd, _):
            pid = wintypes.DWORD()
            user.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
            if pid.value == process.pid and user.IsWindowVisible(hwnd):
                title = ctypes.create_unicode_buffer(512)
                user.GetWindowTextW(hwnd, title, len(title))
                titles.add(title.value)
                if title.value == "OpenMW-CS":
                    handles.append(hwnd)
            return True

        limit = time.monotonic() + 60
        while time.monotonic() < limit and process.poll() is None:
            user.EnumWindows(inspect, 0)
            if handles:
                user.PostMessageW(handles[0], 0x0010, 0, 0)  # WM_CLOSE
                process.wait(timeout=15)
                if process.returncode != 0:
                    raise ValueError(f"Editor closed with exit {process.returncode}")
                return "OpenMW-CS"
            time.sleep(0.2)
        raise ValueError(f"Editor startup window missing. Exit {process.poll()}, window titles {sorted(titles)}")
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()


def smoke_tools(install):
    env = os.environ.copy()
    system = Path(env["SystemRoot"])
    env["PATH"] = os.pathsep.join(map(str, (install, system / "System32", system)))
    for key in ("QT_PLUGIN_PATH", "QT_QPA_PLATFORM_PLUGIN_PATH", "QT_QPA_PLATFORM", "OSG_LIBRARY_PATH"):
        env.pop(key, None)
    observed = {}
    for name, code in (("bsatool", 1), ("esmtool", 1), ("niftest", 1),
                       ("openmw-essimporter", 0), ("openmw-bulletobjecttool", 0)):
        result = subprocess.run([str(install / (name + ".exe")), "--help"], env=env, cwd=install,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=30)
        if result.returncode != code or b"--help" not in result.stdout:
            raise ValueError(f"{name} --help: exit {result.returncode}, {result.stdout[:800]!r}")
        observed[name] = {"argument": "--help", "exit": code}
    with tempfile.TemporaryDirectory() as scratch:
        nif = Path(scratch) / "distribution-smoke.nif"
        nif.write_bytes(b"NetImmerse File Format, Version 4.0.0.2\n" + struct.pack("<III", 0x04000002, 0, 0))
        proc = subprocess.run([str(install / "niftest.exe"), str(nif)], cwd=install, env=env,
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=30)
        if proc.returncode or b"distribution-smoke.nif" not in proc.stdout or b"Failed to read" in proc.stdout:
            raise ValueError(f"NIF smoke failed: {proc.returncode}, {proc.stdout[:800]!r}")
        observed["niftest"]["sample_nif"] = "PASS"
    observed["openmw-cs"] = {"startup_window": wait_for_editor(install, env)}
    return observed


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("build", type=Path)
    parser.add_argument("install", type=Path)
    parser.add_argument("--preserve-existing", action="store_true")
    args = parser.parse_args()
    install = args.install.resolve()
    before = json.loads((install / "PACKAGE-SHA256.json").read_bytes())["files"] if args.preserve_existing else {}
    stage(args.build.resolve(), install)
    imports = confirm_distribution(install)
    launches = smoke_tools(install)
    for name, row in before.items():
        if digest(install / name) != row["sha256"]:
            raise ValueError("An original package file changed: " + name)
    current = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    combined = json.loads((install / "COMBINED-PACKAGE.json").read_bytes())
    report = dict(result="PASS", engine_commit=combined["candidate_commit"], packaging_commit=current,
                  tested_original_files_preserved=len(before), public_applications=list(PUBLIC), command_checks=launches,
                  tool_hashes={name: digest(install / (name + ".exe")) for name in PUBLIC}, imports=imports)
    folder = install / "verification"
    folder.mkdir(exist_ok=True)
    (folder / "WINDOWS-DISTRIBUTION.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"Verified {len(PUBLIC)} applications, imports of {len(imports)} Windows modules and editor startup.")


if __name__ == "__main__":
    main()
