"""Verify the bounded colour feature against the last delivered journal source.

Run before the existing, separately recorded lookup-only build-time repair.
This checks source provenance and preservation, not live appearance.
"""
from pathlib import Path
import configparser
import hashlib
import json
import subprocess

ROOT = Path(__file__).resolve().parents[2]
BASELINE = "cbdf7a0adfac4c33f0d5f5bc62174f3bd3146fd0"
PREVIOUS_DELIVERY = "b34db5770f947b0f5b90dc699627c2ccdae12f16"
ALLOWED = {
    ".github/workflows/push.yml",
    "apps/components_tests/CMakeLists.txt",
    "apps/components_tests/misc/testdialoguetext.cpp",
    "apps/components_tests/settings/testvalues.cpp",
    "apps/openmw/CMakeLists.txt",
    "apps/openmw/mwbase/windowmanager.hpp",
    "apps/openmw/mwdialogue/journalimp.cpp",
    "apps/openmw/mwgui/bookpage.cpp",
    "apps/openmw/mwgui/bookpage.hpp",
    "apps/openmw/mwgui/dialogue.cpp",
    "apps/openmw/mwgui/dialogue.hpp",
    "apps/openmw/mwgui/dialoguecolourcontrols.cpp",
    "apps/openmw/mwgui/dialoguecolourcontrols.hpp",
    "apps/openmw/mwgui/dialogueformatting.cpp",
    "apps/openmw/mwgui/dialogueformatting.hpp",
    "apps/openmw/mwgui/journalbooks.cpp",
    "apps/openmw/mwgui/settingswindow.cpp",
    "apps/openmw/mwgui/settingswindow.hpp",
    "apps/openmw/mwgui/windowmanagerimp.cpp",
    "apps/openmw/mwgui/windowmanagerimp.hpp",
    "apps/openmw/mwscript/containerextensions.cpp",
    "apps/openmw_tests/CMakeLists.txt",
    "apps/openmw_tests/mwgui/testdialogueformatting.cpp",
    "apps/openmw_tests/mwgui/testdialoguecolourcontrols.cpp",
    "components/CMakeLists.txt",
    "components/misc/dialoguetext.cpp",
    "components/misc/dialoguetext.hpp",
    "components/settings/categories/gui.hpp",
    "docs/source/reference/modding/settings/GUI.rst",
    "files/data/l10n/OMWEngine/en.yaml",
    "files/settings-default.cfg",
}

# This follow-up changes only the requested topic headings, broken settings
# controls, and their tests/documentation. Earlier notification classification,
# journal rendering and all unrelated engine work must retain delivered bytes.
FOLLOWUP_ALLOWED = {
    ".github/workflows/push.yml",
    "apps/components_tests/settings/testvalues.cpp",
    "apps/openmw/CMakeLists.txt",
    "apps/openmw/mwgui/dialogue.cpp",
    "apps/openmw/mwgui/dialoguecolourcontrols.cpp",
    "apps/openmw/mwgui/dialoguecolourcontrols.hpp",
    "apps/openmw/mwgui/dialogueformatting.cpp",
    "apps/openmw/mwgui/dialogueformatting.hpp",
    "apps/openmw/mwgui/settingswindow.cpp",
    "apps/openmw/mwgui/settingswindow.hpp",
    "apps/openmw_tests/CMakeLists.txt",
    "apps/openmw_tests/mwgui/testdialogueformatting.cpp",
    "apps/openmw_tests/mwgui/testdialoguecolourcontrols.cpp",
    "components/settings/categories/gui.hpp",
    "docs/source/reference/modding/settings/GUI.rst",
    "files/data/l10n/OMWEngine/en.yaml",
    "files/settings-default.cfg",
}


def git(*args):
    return subprocess.check_output(["git", *args], cwd=ROOT)


def sha(data):
    return hashlib.sha256(data).hexdigest()


baseline_files = set(git("ls-tree", "-r", "--name-only", BASELINE).decode().splitlines())
changed = git("diff", "--name-only", BASELINE, "--").decode().splitlines()
unexpected = [p for p in changed if p not in ALLOWED and not p.startswith("custom/dialogue-colours/")]
if unexpected:
    raise SystemExit("Unreviewed changes outside dialogue colour scope: " + ", ".join(unexpected))
subprocess.run(["git", "diff", "--check", BASELINE], cwd=ROOT, check=True)

previous_files = set(git("ls-tree", "-r", "--name-only", PREVIOUS_DELIVERY).decode().splitlines())
followup_changed = git("diff", "--name-only", PREVIOUS_DELIVERY, "--").decode().splitlines()
unexpected_followup = [p for p in followup_changed
                       if p not in FOLLOWUP_ALLOWED and not p.startswith("custom/dialogue-colours/")]
if unexpected_followup:
    raise SystemExit("Unreviewed changes outside topic/settings repair: " + ", ".join(unexpected_followup))

files = {}
for name in changed:
    path = ROOT / name
    if not path.is_file():
        raise SystemExit("Unexpected removed file: " + name)
    before = git("show", f"{BASELINE}:{name}") if name in baseline_files else None
    files[name] = {"before_sha256": sha(before) if before is not None else None,
                   "after_sha256": sha(path.read_bytes()), "bytes": path.stat().st_size}

preserved = {}
for name in (
    "apps/openmw/engine.cpp", "apps/openmw/mwgui/loadingscreen.cpp",
    "apps/openmw/mwgui/loadingscreen.hpp", "apps/openmw/mwstate/statemanagerimp.cpp",
    "apps/openmw/mwworld/worldmodel.cpp", "apps/openmw/mwworld/worldmodel.hpp",
    "apps/openmw/mwgui/journalwindow.cpp", "apps/openmw/mwgui/journaltypography.hpp",
    "apps/openmw/mwgui/tooltips.cpp", "apps/openmw/mwgui/inventorywindow.cpp",
    "apps/openmw/mwdialogue/dialoguemanagerimp.cpp", "apps/openmw/mwdialogue/keywordsearch.cpp",
    "components/widgets/list.cpp", "components/widgets/imagebutton.cpp",
    "files/data/mygui/openmw_settings_window.layout",
    "custom/loading-repair/apply.py", "custom/loading-repair/check-integration.py",
):
    original = git("show", f"{BASELINE}:{name}")
    if (ROOT / name).read_bytes() != original:
        raise SystemExit("Preserved engine feature changed unexpectedly: " + name)
    preserved[name] = sha(original)

old_settings = configparser.ConfigParser(interpolation=None, strict=False)
new_settings = configparser.ConfigParser(interpolation=None, strict=False)
old_settings.read_string(git("show", f"{BASELINE}:files/settings-default.cfg").decode())
new_settings.read(ROOT / "files/settings-default.cfg", encoding="utf-8")
for section in old_settings.sections():
    for key, value in old_settings[section].items():
        if new_settings.get(section, key, fallback=None) != value:
            raise SystemExit(f"Existing default changed: [{section}] {key}")
previous_settings = configparser.ConfigParser(interpolation=None, strict=False)
previous_settings.read_string(git("show", f"{PREVIOUS_DELIVERY}:files/settings-default.cfg").decode())
for section in previous_settings.sections():
    for key, value in previous_settings[section].items():
        if new_settings.get(section, key, fallback=None) != value:
            raise SystemExit(f"Previously delivered default changed: [{section}] {key}")
for key in ("dialogue colouring", "dialogue use system colour", "dialogue classify narration", "journal colour narration"):
    if not new_settings.getboolean("GUI", key):
        raise SystemExit("Requested default must be enabled: " + key)

native = [p for p in git("ls-files").decode().splitlines()
          if Path(p).suffix.lower() in {".esp", ".esm", ".omwaddon", ".omwgame"}]
if native:
    raise SystemExit("Unexpected native content in engine feature: " + ", ".join(native))

manifest = {
    "feature": "Configurable dialogue topics, notices and narration colours with repaired settings layout",
    "baseline": BASELINE,
    "previous_delivery": PREVIOUS_DELIVERY,
    "commit": git("rev-parse", "HEAD").decode().strip(),
    "changed_files": files,
    "explicitly_preserved_files": preserved,
    "all_other_tracked_source_files_equal_baseline": True,
    "all_existing_settings_defaults_preserved": True,
    "changed_since_previous_delivery": {
        name: {"before_sha256": sha(git("show", f"{PREVIOUS_DELIVERY}:{name}")) if name in previous_files else None,
               "after_sha256": sha((ROOT / name).read_bytes())}
        for name in followup_changed
    },
    "all_other_tracked_source_files_equal_previous_delivery": True,
    "all_previous_dialogue_colour_defaults_preserved": True,
    "native_plugin_changes": False,
    "build_time_lookup_patch": "Retained; run and verify custom/loading-repair/apply.py separately",
    "in_game_tested": False,
}
(ROOT / "dialogue-colours-source-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
print(f"PASS: {len(changed)} scoped files; other source and existing defaults match {BASELINE}.")
print("Preserved journal controls, lookup build scripts, loading rollback, tooltips and dialogue selection.")
