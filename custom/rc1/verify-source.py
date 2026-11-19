"""Bind the RC1 port to its complete reviewed source tree.

Historical gates retain their original baselines. This gate records the RC1
adaptation separately; a matching hash is source identity, not gameplay proof.
The accepted lookup optimization is now committed, so CI builds this exact tree.
"""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess

ROOT = Path(__file__).resolve().parents[2]
PLAN = "custom/rc1/source-plan.json"
RC1 = "4ff261bf7c0d42a6cc06dd1ebc5d451802cff055"
UNION_TREE = "a34a506e66cfe3666a1fce8c90c9a4d2f3bbd199"
UNION_PARENTS = ["f6f8542512fc35508d9f0fdcb3725475a01aff60", "afa9a970b81ce193b5d6d3a36d24c995353e665d"]
PLAYER_FEATURE = "e39f2dd83a8308589cbd9940978fe142c713590b"
INVENTORY_SHA256 = "a98bcde73147416496aa7c158cdc272fe87b5f943e28ed6eb8120a1b99749f6c"


def git(*args):
    return subprocess.check_output(["git", *args], cwd=ROOT)


def require(condition, message):
    if not condition:
        raise ValueError(message)


def blob(data):
    return hashlib.sha1(b"blob " + str(len(data)).encode() + b"\0" + data).hexdigest()


def tree(revision):
    result = {}
    for row in git("ls-tree", "-r", "-z", revision).split(b"\0"):
        if row:
            metadata, path = row.split(b"\t", 1)
            mode, kind, identity = metadata.decode().split()
            require(kind == "blob", "Unsupported source entry: " + path.decode())
            result[path.decode()] = {"mode": mode, "blob": identity}
    return result


def verify():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--phase", choices=("source", "build"), default="source")
    args = parser.parse_args()
    plan = json.loads((ROOT / PLAN).read_text())
    require(plan["schema_version"] == 1 and plan["upstream_rc1"] == RC1, "Incorrect RC1 baseline")
    union = plan["custom_union"]
    require(git("rev-parse", union + "^{tree}").decode().strip() == UNION_TREE, "Cumulative union tree changed")
    require(git("show", "-s", "--format=%P", union).decode().split() == UNION_PARENTS, "Cumulative union parents changed")
    historical_plan = json.loads(git("show", union + ":custom/town-performance/source-plan.json"))
    require(plan["preserved_heads"] == historical_plan["preserved_heads"], "Historical preserved heads omitted or changed")
    require(plan["common_development_base"] == "2062366b727978165213a7be14319e977edf3d5a", "Common base changed")
    require(plan["additional_feature"]["original_commit"] == PLAYER_FEATURE, "Player feature provenance changed")
    require(hashlib.sha256((ROOT / "custom/rc1/source-preservation-inventory.json").read_bytes()).hexdigest()
            == INVENTORY_SHA256, "Independent branch inventory changed")
    for revision in [RC1, union, PLAYER_FEATURE, *plan["preserved_heads"]]:
        subprocess.run(["git", "merge-base", "--is-ancestor", revision, "HEAD"], cwd=ROOT, check=True)
    original, actual = tree(RC1), tree("HEAD")
    prior = tree(union)
    coverage = plan["cumulative_path_coverage"]
    require(set(coverage) == set(historical_plan["files"]), "Cumulative feature paths omitted or added")
    for path, row in coverage.items():
        require(row["cumulative"] == prior.get(path), "Historical feature identity changed: " + path)
        require(row["ported"] is not None and row["ported"] == actual.get(path), "Cumulative feature missing or changed: " + path)
        status = "exactly_retained" if prior.get(path) == actual.get(path) else "adapted_for_rc1"
        require(row["status"] == status, "Incorrect feature preservation classification: " + path)
    changed = {p for p in original.keys() | actual.keys() if original.get(p) != actual.get(p)}
    require(changed == set(plan["changes"]) | {PLAN}, "Unrecorded changes relative to official RC1")
    for path, row in plan["changes"].items():
        require(row["upstream"] == original.get(path), "Changed upstream identity: " + path)
        require(row["ported"] == actual.get(path), "Changed port identity: " + path)
        require(row["purpose"], "Missing change purpose: " + path)
    require(not git("diff", "HEAD", "--name-only").strip(), "Tracked source differs from the reviewed commit")
    for path, row in actual.items():
        p = ROOT / path
        require(p.is_file() and not p.is_symlink(), "Missing or unsupported source path: " + path)
        require(blob(p.read_bytes()) == row["blob"], "Working bytes differ: " + path)
    # Check newly ported edits; captured third-party source keeps its original formatting.
    subprocess.run(["git", "diff", "--check", plan["custom_union"]], cwd=ROOT, check=True)

    # Preserve the rejected-loading-stage checks without pretending the old
    # state-manager hash can authenticate the new upstream implementation.
    for path in ("components/loadinglistener/gameplayhandoff.hpp", "custom/loading-repair/handoff-test.cpp"):
        require(not (ROOT / path).exists(), "Rejected waiting stage returned: " + path)
    world = (ROOT / "apps/openmw/mwworld/worldmodel.cpp").read_text()
    header = (ROOT / "apps/openmw/mwworld/worldmodel.hpp").read_text()
    require("prepareCellLookup" not in world + header, "Extra pre-game index preparation returned")
    selection = world.split("if (useIndexedInactiveCellCandidates())", 1)[1].split("            else", 1)[0]
    require("for (CellStore* cell : mUnindexedLookupCells)" not in selection, "Unloaded-cell full scan returned")
    require("mUnindexedFallbackCells.begin()" in selection, "Exceptional lookup candidates missing")
    for action in ("clear();", "erase(cell);", "insert(cell);"):
        require("mUnindexedFallbackCells." + action in world, "Missing fallback membership update: " + action)

    commit = git("rev-parse", "HEAD").decode().strip()
    report = {
        "result": "PASS", "commit": commit, "phase": args.phase,
        "upstream_rc1": RC1, "custom_union": plan["custom_union"],
        "source_plan_sha256": hashlib.sha256((ROOT / PLAN).read_bytes()).hexdigest(),
        "tracked_paths_verified": len(actual), "exact_rc1_delta_paths": len(plan["changes"]),
        "preserved_ancestry_verified": True,
        "lookup_optimization": "committed source; no CI transformation",
        "historical_gates": "run separately at their original commits",
        "gameplay_tested": False,
        "meaning": "Source identity and ancestry only; native and gameplay results are separate evidence.",
    }
    output = "rc1-build-source-manifest.json" if args.phase == "build" else "rc1-source-manifest.json"
    (ROOT / output).write_text(json.dumps(report, indent=2) + "\n")
    if args.phase == "build":
        paths = ["apps/openmw/mwworld/worldmodel.cpp", "apps/openmw/mwworld/worldmodel.hpp"]
        lookup = {"commit": commit, "mode": "committed_source", "build_time_edits": 0,
                  "files": {p: actual[p] for p in paths},
                  "postload_waiting_stage": "absent", "extra_pre_game_index_preparation": "absent"}
        (ROOT / "loading-repair-source-manifest.json").write_text(json.dumps(lookup, indent=2) + "\n")
        (ROOT / "loading-repair-applied.patch").write_bytes(git("diff", plan["custom_union"], "HEAD", "--", *paths))
    print(f"PASS: {len(actual)} tracked paths match the RC1 port; {len(plan['changes'])} exact upstream deltas; {args.phase}.")


if __name__ == "__main__":
    verify()
