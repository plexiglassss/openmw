"""Verify the bounded feature diff against the user's accepted engine source."""
from pathlib import Path
from collections import Counter
import hashlib
import json
import subprocess
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
BASELINE = "00af1d5c63313c61a46c6c8782ce45b3e05f023a"
ALLOWED = {
    ".github/workflows/push.yml",
    "apps/openmw/mwgui/bookpage.cpp",
    "apps/openmw/mwgui/bookpage.hpp",
    "apps/openmw/mwgui/journalbooks.cpp",
    "apps/openmw/mwgui/journalbooks.hpp",
    "apps/openmw/mwgui/settingswindow.cpp",
    "apps/openmw/mwgui/settingswindow.hpp",
    "apps/launcher/settingspage.cpp",
    "apps/launcher/ui/settingspage.ui",
    "apps/openmw_tests/CMakeLists.txt",
    "apps/openmw_tests/mwgui/test_journaltypography.cpp",
    "apps/components_tests/settings/testvalues.cpp",
    "components/settings/categories/gui.hpp",
    "files/settings-default.cfg",
    "files/data/l10n/OMWEngine/en.yaml",
    "files/data/mygui/openmw_settings_window.layout",
    "docs/source/reference/modding/settings/GUI.rst",
}


def git(*args):
    return subprocess.check_output(["git", *args], cwd=ROOT)


def sha(data):
    return hashlib.sha256(data).hexdigest()


changed = git("diff", "--name-only", BASELINE, "--").decode().splitlines()
unexpected = [p for p in changed if p not in ALLOWED and not p.startswith("custom/journal-typography/")]
if unexpected:
    raise SystemExit("Unreviewed changes outside typography scope: " + ", ".join(unexpected))
subprocess.run(["git", "diff", "--check", BASELINE], cwd=ROOT, check=True)

files = {}
baseline_files = set(git("ls-tree", "-r", "--name-only", BASELINE).decode().splitlines())
for name in changed:
    before = git("show", f"{BASELINE}:{name}") if name in baseline_files else None
    path = ROOT / name
    if not path.is_file():
        raise SystemExit("Unexpected removed file: " + name)
    files[name] = {"before_sha256": sha(before) if before is not None else None,
                   "after_sha256": sha(path.read_bytes()), "bytes": path.stat().st_size}

# Parse the UI resources and reject newly introduced duplicate widget names.
for name in ("apps/launcher/ui/settingspage.ui", "files/data/mygui/openmw_settings_window.layout"):
    tree = ET.parse(ROOT / name)
    nodes = list(tree.iter("widget")) + list(tree.iter("Widget"))
    names = Counter(node.attrib["name"] for node in nodes if node.attrib.get("name"))
    old_tree = ET.fromstring(git("show", f"{BASELINE}:{name}"))
    old_nodes = list(old_tree.iter("widget")) + list(old_tree.iter("Widget"))
    old_names = Counter(node.attrib["name"] for node in old_nodes if node.attrib.get("name"))
    if any(count > max(1, old_names.get(widget, 0)) for widget, count in names.items()):
        raise SystemExit("New duplicate widget names in " + name)

manifest = {
    "feature": "journal-only font size and paragraph alignment",
    "accepted_baseline": BASELINE,
    "commit": git("rev-parse", "HEAD").decode().strip(),
    "changed_files": files,
    "all_other_tracked_source_files_equal_baseline": True,
    "native_plugin_or_world_record_changes": False,
    "retained_lookup_build_scripts_equal_baseline": True,
    "visual_gameplay_tested": False,
}
(ROOT / "journal-typography-source-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
print(f"PASS: {len(changed)} scoped files; all other tracked source matches accepted baseline {BASELINE}.")
print("UI XML parsed with no newly introduced duplicate widget names. Native plugins/world records are outside this source diff.")
