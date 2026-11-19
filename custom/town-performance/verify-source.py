"""Verify the reviewed town-performance changes against the complete preserved engine tree.

The historical cumulative verifier remains unchanged and is executed separately
at the exact baseline. This gate permits only the individually reviewed feature
files, checks their original and final identities, and retains the existing
lookup-only build transformation.
"""
from pathlib import Path, PurePosixPath
import argparse
import hashlib
import json
import subprocess


ROOT = Path(__file__).resolve().parents[2]
BASELINE = "ce3d5a2e981999df432cf8df416352cc85842e97"
PLAN = "custom/town-performance/source-plan.json"
# Generated proof files are not source. This exception applies only to untracked
# working-tree outputs; committing any such file still changes the reviewed tree.
OUTPUTS = {
    "town-performance-source-manifest.json", "town-performance-build-source-manifest.json",
    "deleveler-source-manifest.json", "deleveler-build-source-manifest.json",
}
LOOKUP = {"apps/openmw/mwworld/worldmodel.cpp", "apps/openmw/mwworld/worldmodel.hpp"}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def git(*args):
    return subprocess.check_output(["git", *args], cwd=ROOT)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def blob(data):
    return hashlib.sha1(b"blob " + str(len(data)).encode() + b"\0" + data).hexdigest()


def tree(revision):
    result = {}
    for row in git("ls-tree", "-r", "-z", revision).split(b"\0"):
        if not row:
            continue
        metadata, name = row.split(b"\t", 1)
        mode, kind, identity = metadata.decode().split()
        require(kind == "blob", "Unsupported tree entry: " + name.decode())
        result[name.decode()] = {"mode": mode, "blob": identity}
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--working-tree", action="store_true")
    parser.add_argument("--after-lookup", action="store_true")
    args = parser.parse_args()
    require(not (args.working_tree and args.after_lookup), "Select one verification phase")
    plan_path = ROOT / PLAN
    plan = json.loads(plan_path.read_text())
    require(plan["schema_version"] == 1 and plan["baseline"] == BASELINE, "Wrong feature baseline")
    subprocess.run(["git", "merge-base", "--is-ancestor", BASELINE, "HEAD"], cwd=ROOT, check=True)
    for revision in plan["preserved_heads"]:
        subprocess.run(["git", "merge-base", "--is-ancestor", revision, "HEAD"], cwd=ROOT, check=True)
    before = tree(BASELINE)
    actual = tree("HEAD")
    dirty = set(git("diff", "HEAD", "--name-only", "-z").decode().split("\0")) - {""}
    if args.working_tree:
        untracked = set(git("ls-files", "--others", "--exclude-standard", "-z").decode().split("\0")) - {""} - OUTPUTS
        for name in dirty | untracked:
            path = ROOT / name
            require(path.is_file() and not path.is_symlink(), "Deleted or unsupported source: " + name)
            actual[name] = {"mode": "100755" if path.stat().st_mode & 0o111 else "100644", "blob": blob(path.read_bytes())}
    changed = {name for name in before.keys() | actual.keys() if before.get(name) != actual.get(name)}
    rows = plan["files"]
    require(PLAN not in rows and rows, "The feature plan must describe nonempty exact changes")
    require(changed == set(rows) | {PLAN}, "Unreviewed source changes: " + repr(sorted(changed ^ (set(rows) | {PLAN}))))
    verified = {}
    for name, expected in rows.items():
        path = PurePosixPath(name)
        require(not path.is_absolute() and ".." not in path.parts and "\\" not in name, "Unsafe source path")
        require(path.suffix.lower() not in {".esm", ".esp", ".omwaddon", ".omwgame"}, "Unexpected game plugin")
        require(expected["before"] == before.get(name), "Original source identity changed: " + name)
        require(expected["after"] == actual.get(name), "Reviewed source identity changed: " + name)
        require(expected.get("reason"), "Missing reviewed change purpose: " + name)
        local = ROOT / name
        require(local.is_file() and not local.is_symlink(), "Missing source: " + name)
        # The loading repair independently verifies its exact working output below.
        # Verify the feature plan against its committed input when both touch the same file.
        data = git("show", "HEAD:" + name) if args.after_lookup and name in LOOKUP else local.read_bytes()
        require(digest(data) == expected["sha256"] and len(data) == expected["bytes"], "Working source differs: " + name)
        verified[name] = expected
    require((ROOT / PLAN).is_file() and not (ROOT / PLAN).is_symlink(), "Missing source plan")
    if not args.working_tree:
        require(blob(plan_path.read_bytes()) == actual[PLAN]["blob"], "Uncommitted source plan")
    if args.after_lookup:
        require(dirty == LOOKUP, "Unexpected build-time modifications: " + repr(sorted(dirty)))
        lookup = json.loads((ROOT / "loading-repair-source-manifest.json").read_text())
        patched = {name for name, row in lookup["files"].items() if row["before_blob"] != row["after_blob"]}
        require(patched == LOOKUP, "Lookup patch scope changed")
        for name in LOOKUP:
            require(lookup["files"][name]["before_blob"] == actual[name]["blob"], "Lookup input changed: " + name)
            require(lookup["files"][name]["after_blob"] == blob((ROOT / name).read_bytes()), "Lookup output changed: " + name)
    elif not args.working_tree:
        require(not dirty, "Uncommitted source changes: " + repr(sorted(dirty)))
    subprocess.run(["git", "diff", "--check", BASELINE], cwd=ROOT, check=True)
    phase = "prepublication_working_tree" if args.working_tree else ("after_lookup" if args.after_lookup else "committed_source")
    report = {
        "result": "PASS", "commit": git("rev-parse", "HEAD").decode().strip(), "phase": phase,
        "baseline": BASELINE, "source_plan_sha256": digest(plan_path.read_bytes()),
        "accepted_heads_are_ancestors_of_commit": not args.working_tree,
        "tracked_file_count": len(actual), "reviewed_changes": verified,
        "all_other_committed_source_equal_baseline": True,
        "historical_verification": "Unchanged combined, menu and cumulative gates executed separately at their exact preserved baselines",
        "in_game_tested": False, "installed_custom_lua_runtime_tested": False,
    }
    output = ROOT / ("town-performance-build-source-manifest.json" if args.after_lookup else "town-performance-source-manifest.json")
    output.write_text(json.dumps(report, indent=2) + "\n")
    print(f"PASS: {len(rows)} individually bound town-performance files; all {len(actual)} source paths checked; {phase}.")


if __name__ == "__main__":
    main()
