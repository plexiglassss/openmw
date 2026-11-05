"""Bind the shrine tooltip bridge to the user's preserved engine source."""
from pathlib import Path
import hashlib
import json
import subprocess

ROOT = Path(__file__).resolve().parents[2]
BASELINE = "528bffb348872a964d25c092fa5dd574c1b7f673"
ALLOWED = {
    ".github/workflows/push.yml",
    "apps/openmw/mwbase/windowmanager.hpp",
    "apps/openmw/mwgui/messagebox.hpp",
    "apps/openmw/mwgui/messagebox.cpp",
    "apps/openmw/mwgui/messageboxstate.hpp",
    "apps/openmw/mwgui/windowmanagerimp.hpp",
    "apps/openmw/mwgui/windowmanagerimp.cpp",
    "apps/openmw/mwlua/uibindings.cpp",
    "apps/openmw/mwscript/interpretercontext.hpp",
    "apps/openmw/mwscript/interpretercontext.cpp",
    "apps/openmw/mwscript/scriptmanagerimp.cpp",
    "apps/openmw_tests/CMakeLists.txt",
    "apps/openmw_tests/mwgui/testmessageboxstate.cpp",
    "files/lua_api/openmw/ui.lua",
}

def git(*args):
    return subprocess.check_output(["git", *args], cwd=ROOT)

def sha(data):
    return hashlib.sha256(data).hexdigest()

base_files = set(git("ls-tree", "-r", "--name-only", BASELINE).decode().splitlines())
changed = git("diff", "--name-only", BASELINE, "--").decode().splitlines()
unexpected = [p for p in changed if p not in ALLOWED and not p.startswith("custom/shrine-tooltips/")]
if unexpected:
    raise SystemExit("Changes outside the shrine tooltip bridge: " + ", ".join(unexpected))
subprocess.run(["git", "diff", "--check", BASELINE], cwd=ROOT, check=True)
files = {}
for name in changed:
    path = ROOT / name
    if not path.is_file():
        raise SystemExit("Unexpected source removal: " + name)
    before = git("show", f"{BASELINE}:{name}") if name in base_files else None
    files[name] = {"before_sha256": sha(before) if before is not None else None,
                   "after_sha256": sha(path.read_bytes()), "bytes": path.stat().st_size}

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
    "apps/openmw/mwgui/settingswindow.cpp",
    "apps/openmw/mwgui/loadingscreen.cpp",
    "apps/openmw/mwstate/statemanagerimp.cpp",
    "apps/openmw/mwworld/worldmodel.cpp",
    "files/settings-default.cfg",
    "custom/loading-repair/apply.py",
    "custom/loading-repair/check-integration.py",
    "custom/dialogue-colours/verify-source.py",
):
    before = git("show", f"{BASELINE}:{name}")
    if (ROOT / name).read_bytes() != before:
        raise SystemExit("Accepted engine feature changed unexpectedly: " + name)
    preserved[name] = sha(before)

for name in changed:
    if Path(name).suffix.lower() in {".esp", ".esm", ".omwaddon", ".omwgame"}:
        raise SystemExit("Unexpected native plugin change: " + name)

manifest = {
    "feature": "Read native message-box context and attach spell tooltips by guarded generation",
    "baseline": BASELINE,
    "commit": git("rev-parse", "HEAD").decode().strip(),
    "changed_files": files,
    "explicitly_preserved_files": preserved,
    "all_other_tracked_source_files_equal_baseline": True,
    "existing_native_spell_tooltip_renderer_unchanged": True,
    "native_plugin_changes": False,
    "build_time_lookup_patch": "Retained verbatim; separately applied and verified by CI",
    "in_game_tested": False,
}
(ROOT / "shrine-tooltips-source-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
print(f"PASS: {len(changed)} scoped files; all other source equals {BASELINE}.")

