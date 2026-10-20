"""Fail early if the Windows diagnostic configuration loses optimization or caching."""

import json
from pathlib import Path
import re
import subprocess
import sys


def check_engine_object_links(build, commands, errors):
    """Reject the /Z7 engine archive before compiling gigabytes of objects."""
    graph = (build / "build.ninja").read_text(encoding="utf-8").replace("\\", "/")
    prefix = str(build.resolve()).replace("\\", "/") + "/"
    objects = set()
    for entry in commands:
        output = entry.get("output", "").replace("\\", "/")
        if output.lower().startswith(prefix.lower()):
            output = output[len(prefix):]
        if "/openmw-lib.dir/" in output:
            objects.add(output)
    if not objects:
        errors.append("No engine objects found for checking direct executable links")
        return
    if re.search(r"^build [^\n]*openmw-lib\.lib:", graph, re.MULTILINE):
        errors.append("Embedded debug info must not be archived into openmw-lib.lib (LNK1248)")
    for target in ("openmw.exe", "openmw-tests.exe"):
        edge = re.search(r"^build " + re.escape(target) + r"(?: |:)[^\n]*", graph, re.MULTILINE)
        if edge is None:
            errors.append(f"Missing final executable build edge for {target}")
            continue
        # Only explicit inputs are passed to the linker. Order-only/implicit
        # dependencies alone do not prove that the executable gets the objects.
        explicit = edge[0].split(": ", 1)[1].split(" |", 1)[0].split()
        missing = objects.difference(explicit)
        if missing:
            errors.append(f"{target}: {len(missing)} engine objects missing from direct link")
    print(f"Checked direct links of {len(objects)} engine objects into both executables.")


def main():
    build = Path(sys.argv[1])
    cache = {}
    for line in (build / "CMakeCache.txt").read_text(encoding="utf-8").splitlines():
        if line and not line.startswith(("#", "//")) and "=" in line:
            key, value = line.split("=", 1)
            cache[key.split(":", 1)[0]] = value

    errors = []
    for key, expected in {
        "CMAKE_BUILD_TYPE": "Release",
        "CMAKE_MSVC_DEBUG_INFORMATION_FORMAT": "Embedded",
        "CMAKE_C_COMPILER_LAUNCHER": "sccache",
        "CMAKE_CXX_COMPILER_LAUNCHER": "sccache",
        "CMAKE_POLICY_DEFAULT_CMP0141": "NEW",
    }.items():
        if cache.get(key) != expected:
            errors.append(f"{key}: expected {expected!r}, found {cache.get(key)!r}")

    for enabled, keys in (
        (True, ("BUILD_OPENMW", "BUILD_LAUNCHER", "BUILD_WIZARD", "BUILD_MWINIIMPORTER",
                "BUILD_NAVMESHTOOL", "BUILD_COMPONENTS_TESTS", "BUILD_OPENMW_TESTS", "OPENMW_LTO_BUILD")),
        (False, ("BUILD_OPENCS", "BUILD_OPENCS_TESTS", "BUILD_BENCHMARKS", "BUILD_ESSIMPORTER",
                 "BUILD_BSATOOL", "BUILD_ESMTOOL", "BUILD_NIFTEST", "BUILD_BULLETOBJECTTOOL",
                 "PRECOMPILE_HEADERS_WITH_MSVC")),
    ):
        for key in keys:
            value = cache.get(key, "").upper()
            allowed = ("ON", "TRUE", "1", "YES") if enabled else ("OFF", "FALSE", "0", "NO")
            if value not in allowed:
                errors.append(f"{key}: expected {'enabled' if enabled else 'disabled'}, found {value!r}")

    linker_flags = cache.get("CMAKE_EXE_LINKER_FLAGS_RELEASE", "").split()
    for flag in ("/DEBUG", "/OPT:REF", "/OPT:ICF", "/INCREMENTAL:NO"):
        if flag not in linker_flags:
            errors.append(f"Missing Release linker flag {flag}")

    commands = json.loads((build / "compile_commands.json").read_text(encoding="utf-8"))
    checked = 0
    for entry in commands:
        filename = entry["file"].replace("\\", "/")
        if not filename.endswith(".cpp") or not any(
            part in filename for part in ("/components/", "/apps/openmw/", "/apps/launcher/")
        ):
            continue
        command = entry.get("command") or " ".join(entry["arguments"])
        checked += 1
        for flag in ("O2", "Ob2", "DNDEBUG", "Z7", "GL"):
            if not re.search(r'(?:^|\s)"?[-/]' + flag + r'(?:"?\s|"?$)', command):
                errors.append(f"{filename}: missing /{flag}")
        if re.search(r'(?:^|\s)"?[-/](?:Z[iI](?:"?\s|"?$)|Y[cu]|Fp)', command):
            errors.append(f"{filename}: uncachable shared PDB or precompiled-header flags")

    if not checked:
        errors.append("No engine/component/launcher C++ compile commands found")
    check_engine_object_links(build, commands, errors)
    # CMake can put a $LAUNCHER placeholder in rules.ninja and the actual
    # command in build.ninja. Ask Ninja to expand the real engine command.
    probe = next((entry for entry in commands
                  if entry["file"].replace("\\", "/").endswith("/apps/openmw/main.cpp")), None)
    if probe is None:
        errors.append("No main.cpp compilation found for checking the compiler launcher")
    else:
        resolved = subprocess.run(
            [cache["CMAKE_MAKE_PROGRAM"], "-C", str(build), "-t", "commands",
             probe["output"].replace("\\", "/")],
            capture_output=True, text=True, check=True,
        )
        if "sccache" not in resolved.stdout:
            errors.append("Expanded Ninja engine compile command does not invoke sccache")

    if errors:
        raise SystemExit("Invalid diagnostic configuration:\n" + "\n".join(errors[:30]))
    print(f"Verified {checked} optimized /GL /Z7 compile commands, PDB linking, targets and expanded cache launcher.")


if __name__ == "__main__":
    main()
