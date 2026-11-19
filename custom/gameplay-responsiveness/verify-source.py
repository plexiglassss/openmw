"""Verify the scoped gameplay change against the last delivered cumulative engine."""
from pathlib import Path
import hashlib
import json
import subprocess

ROOT = Path(__file__).resolve().parents[2]
BASELINE = "1b78b9c1a566b91bbe1576387842625f3ae509d1"
CAPTURE_BASELINE = "528bffb348872a964d25c092fa5dd574c1b7f673"
ALLOWED = {
    ".github/workflows/push.yml",
    "components/lua/scripttracker.cpp",
    "components/lua/scripttracker.hpp",
    "components/settings/categories/lua.hpp",
    "files/settings-default.cfg",
    "apps/openmw/mwlua/luamanagerimp.cpp",
    "apps/components_tests/lua/testscriptscontainer.cpp",
}


def git(*args):
    return subprocess.check_output(["git", *args], cwd=ROOT)


def sha(data):
    return hashlib.sha256(data).hexdigest()


subprocess.run(["git", "merge-base", "--is-ancestor", BASELINE, "HEAD"], cwd=ROOT, check=True)
base_files = set(git("ls-tree", "-r", "--name-only", BASELINE).decode().splitlines())
changed = git("diff", "--name-only", BASELINE, "--").decode().splitlines()
unexpected = [p for p in changed if p not in ALLOWED and not p.startswith("custom/gameplay-responsiveness/")]
if unexpected:
    raise SystemExit("Changes outside the reviewed gameplay scope: " + ", ".join(unexpected))
subprocess.run(["git", "diff", "--check", BASELINE], cwd=ROOT, check=True)
files = {}
for name in changed:
    path = ROOT / name
    if not path.is_file():
        raise SystemExit("Unexpected source removal: " + name)
    before = git("show", f"{BASELINE}:{name}") if name in base_files else None
    files[name] = {"before_sha256": sha(before) if before is not None else None,
                   "after_sha256": sha(path.read_bytes()), "bytes": path.stat().st_size}
    if path.suffix.lower() in {".esp", ".esm", ".omwaddon", ".omwgame"}:
        raise SystemExit("Unexpected native plugin: " + name)

# The two intermediate shrine commits did not modify any runtime cache source.
cache_sources = [p for p in ALLOWED if p != ".github/workflows/push.yml"]
for name in cache_sources:
    if git("show", f"{BASELINE}:{name}") != git("show", f"{CAPTURE_BASELINE}:{name}"):
        raise SystemExit("Runtime baseline diverged from the recorded capture: " + name)

manifest = {
    "feature": "Bounded retention of inactive local Lua sandboxes and optional gameplay settings",
    "baseline": BASELINE,
    "capture_baseline": CAPTURE_BASELINE,
    "commit": git("rev-parse", "HEAD").decode().strip(),
    "changed_files": files,
    "all_other_tracked_source_equal_delivered_baseline": True,
    "cache_runtime_baseline_equals_captured_engine": True,
    "native_plugin_changes": False,
    "default_extra_retention_seconds": 0,
    "optional_preset_seconds": 30,
    "memory_pressure_threshold_bytes": 2147483648,
    "memory_threshold_is_not_a_hard_cap": True,
    "build_time_lookup_patch": "Preserved; applied and verified separately by CI",
    "in_game_tested": False,
}
(ROOT / "gameplay-responsiveness-source-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
print(f"PASS: {len(changed)} scoped files; all other source equals {BASELINE}.")
