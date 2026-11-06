"""Verify the bounded message-position change against the cumulative engine."""
from pathlib import Path
import configparser
import hashlib
import json
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]
BASELINE = "1b78b9c1a566b91bbe1576387842625f3ae509d1"
ALLOWED = {
    ".github/workflows/push.yml",
    "apps/openmw/CMakeLists.txt",
    "apps/openmw/mwgui/messagebox.cpp",
    "apps/openmw/mwgui/messagebox.hpp",
    "apps/openmw/mwgui/messageboxposition.hpp",
    "apps/openmw/mwgui/messagepositioncontrols.cpp",
    "apps/openmw/mwgui/messagepositioncontrols.hpp",
    "apps/openmw/mwgui/settingswindow.cpp",
    "apps/openmw/mwgui/settingswindow.hpp",
    "apps/openmw_tests/CMakeLists.txt",
    "apps/components_tests/settings/testvalues.cpp",
    "apps/openmw_tests/mwgui/testmessagepositioncontrols.cpp",
    "components/settings/categories/gui.hpp",
    "files/settings-default.cfg",
    "files/data/l10n/OMWEngine/en.yaml",
}


def git(*args):
    return subprocess.check_output(["git", *args], cwd=ROOT)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def before(path):
    return git("show", f"{BASELINE}:{path}")


def unchanged_region(old, new, start, end=None):
    def region(text):
        value = text[text.index(start):]
        return value[:value.index(end)] if end else value
    if region(old) != region(new):
        raise SystemExit("Unrelated message handling changed: " + start)


base_files = set(git("ls-tree", "-r", "--name-only", BASELINE).decode().splitlines())
changed = git("diff", "--name-only", BASELINE, "--").decode().splitlines()
unexpected = [p for p in changed if p not in ALLOWED and not p.startswith("custom/message-position/")]
if unexpected:
    raise SystemExit("Changes outside message position: " + ", ".join(unexpected))
subprocess.run(["git", "diff", "--check", BASELINE], cwd=ROOT, check=True)
files = {}
for name in changed:
    path = ROOT / name
    if not path.is_file():
        raise SystemExit("Unexpected source removal: " + name)
    prior = before(name) if name in base_files else None
    files[name] = {"before_sha256": sha(prior) if prior is not None else None,
                   "after_sha256": sha(path.read_bytes()), "bytes": path.stat().st_size}

message_path = "apps/openmw/mwgui/messagebox.cpp"
old = before(message_path).decode()
new = (ROOT / message_path).read_text()
unchanged_region(old, new, "    void MessageBoxManager::removeStaticMessageBox()",
                 "    MessageBox::MessageBox(")
unchanged_region(old, new, "    InteractiveMessageBox::InteractiveMessageBox(")
old_expiry = old[old.index("    void MessageBoxManager::onFrame("):old.index("        float height = 0;")]
new_expiry = new[new.index("    void MessageBoxManager::onFrame("):new.index("        updateMessageBoxes();")]
if old_expiry != new_expiry:
    raise SystemExit("Notification lifetime/expiry changed")
old_creation = old[old.index("    void MessageBoxManager::createMessageBox("):old.index("        int height = 0;")]
new_creation = new[new.index("    void MessageBoxManager::createMessageBox("):new.index(
    "        updateMessageBoxes();", new.index("    void MessageBoxManager::createMessageBox("))]
if old_creation != new_creation:
    raise SystemExit("Notification creation, lifetime or queue limit changed")

old_config = configparser.ConfigParser(interpolation=None, strict=False)
new_config = configparser.ConfigParser(interpolation=None, strict=False)
old_config.read_string(before("files/settings-default.cfg").decode())
new_config.read(ROOT / "files/settings-default.cfg")
for section in old_config.sections():
    for key, value in old_config[section].items():
        if new_config[section].get(key) != value:
            raise SystemExit("Existing setting default changed: " + section + "/" + key)
for key, value in {"message box position": "7", "message box horizontal offset": "0",
                   "message box vertical offset": "0"}.items():
    if new_config["GUI"].get(key) != value:
        raise SystemExit("Unexpected message-position default: " + key)

locale = "files/data/l10n/OMWEngine/en.yaml"
if not (ROOT / locale).read_bytes().startswith(before(locale)):
    raise SystemExit("Existing English translations changed")

gui_path = "components/settings/categories/gui.hpp"
old_gui = before(gui_path).decode()
new_gui = (ROOT / gui_path).read_text()
for declaration in re.findall(r"SettingValue<.*?\s+m\w+\{.*?\};", old_gui, re.S):
    if declaration not in new_gui:
        raise SystemExit("Existing GUI setting declaration changed")

preserved = {}
for name in (
    "apps/openmw/engine.cpp",
    "apps/openmw/mwgui/tooltips.cpp",
    "apps/openmw/mwgui/inventorywindow.cpp",
    "apps/openmw/mwgui/journalwindow.cpp",
    "apps/openmw/mwgui/journaltypography.hpp",
    "apps/openmw/mwgui/dialogue.cpp",
    "apps/openmw/mwgui/dialoguecolourcontrols.cpp",
    "apps/openmw/mwgui/dialogueformatting.cpp",
    "apps/openmw/mwgui/messageboxstate.hpp",
    "apps/openmw/mwgui/windowmanagerimp.cpp",
    "apps/openmw/mwlua/uibindings.cpp",
    "apps/openmw/mwgui/loadingscreen.cpp",
    "apps/openmw/mwstate/statemanagerimp.cpp",
    "apps/openmw/mwworld/worldmodel.cpp",
    "custom/loading-repair/apply.py",
    "custom/loading-repair/check-integration.py",
    "custom/dialogue-colours/verify-source.py",
    "custom/shrine-tooltips/verify-source.py",
):
    prior = before(name)
    if (ROOT / name).read_bytes() != prior:
        raise SystemExit("Earlier engine fix changed: " + name)
    preserved[name] = sha(prior)

manifest = {
    "feature": "Native temporary-message position and offsets in Options / Messages",
    "baseline": BASELINE,
    "commit": git("rev-parse", "HEAD").decode().strip(),
    "changed_files": files,
    "explicitly_preserved_files": preserved,
    "all_other_tracked_source_files_equal_baseline": True,
    "interactive_messages_and_shrine_tooltips_unchanged": True,
    "notification_lifetime_and_queue_limit_unchanged": True,
    "existing_setting_defaults_and_translations_preserved": True,
    "build_time_lookup_patch": "Unchanged; applied and verified separately by CI",
    "in_game_tested": False,
}
(ROOT / "message-position-source-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
print(f"PASS: {len(changed)} scoped files; other tracked source equals {BASELINE}.")
