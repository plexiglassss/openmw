"""Verify the complete accepted source union without relaxing historical gates.

Run before the existing lookup patch, then with --after-lookup after its own
integration check. Historical scoped verifiers run unchanged in pinned worktrees.
"""
from pathlib import Path, PurePosixPath
import argparse
import collections
import hashlib
import json
import re
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]


def require(condition, message):
    if not condition:
        raise ValueError(message)


def git(*args, root=ROOT):
    return subprocess.check_output(["git", *args], cwd=root)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def blob(data):
    return hashlib.sha1(b"blob " + str(len(data)).encode() + b"\0" + data).hexdigest()


def tree(revision, root):
    result = {}
    for row in git("ls-tree", "-r", "-z", revision, root=root).split(b"\0"):
        if row:
            meta, name = row.split(b"\t", 1)
            mode, kind, identity = meta.decode().split()
            require(kind == "blob", "Unsupported source tree member: " + name.decode())
            result[name.decode()] = (mode, identity)
    return result


def safe_path(name):
    p = PurePosixPath(name)
    require(not p.is_absolute() and ".." not in p.parts and "\\" not in name,
            "Unsafe source path: " + name)


def merge_bytes(root, name, resolution, sources):
    ours = sources[resolution["ours"]]["head"]
    theirs = sources[resolution["theirs"]]["head"]
    base = resolution["base"]
    require(git("merge-base", ours, theirs, root=root).decode().strip() == base,
            "Wrong shared-settings merge base")
    with tempfile.TemporaryDirectory(prefix="openmw-defaults-merge-") as temporary:
        paths = [Path(temporary) / label for label in ("ours", "base", "theirs")]
        for p, commit in zip(paths, (ours, base, theirs)):
            p.write_bytes(git("show", f"{commit}:{name}", root=root))
        run = subprocess.run(["git", "merge-file", "-p", *map(str, paths)],
                             cwd=root, capture_output=True)
        require(run.returncode == 0, "Shared defaults are not a clean merge: " + run.stderr.decode())
        return run.stdout


def verify(root, plan, after_lookup=False, working_tree=False):
    require(plan["schema_version"] == 1, "Unknown integration plan schema")
    require(not (working_tree and after_lookup), "Prepublication mode must precede the lookup patch")
    baseline = plan["baseline"]
    sources = {row["name"]: row for row in plan["sources"]}
    require(len(sources) == len(plan["sources"]), "Duplicate contribution identity")
    for revision in [baseline, *[row["head"] for row in sources.values()]]:
        require(re.fullmatch(r"[0-9a-f]{40}", revision), "Source revisions must be immutable full SHAs")
        require(git("rev-parse", revision + "^{commit}", root=root).decode().strip() == revision,
                "Source revision did not resolve exactly")

    expected = tree(baseline, root)
    original = dict(expected)
    candidates = collections.defaultdict(dict)
    contribution_rows = []
    for label, source in sources.items():
        base = git("merge-base", baseline, source["head"], root=root).decode().strip()
        require(base == source["merge_base"], "Contribution merge base changed: " + label)
        before, after = tree(base, root), tree(source["head"], root)
        changed = sorted(p for p in before.keys() | after.keys() if before.get(p) != after.get(p))
        for name in changed:
            safe_path(name)
            require(name in after, "Unexpected contribution deletion: " + name)
            candidates[name][label] = after[name]
        contribution_rows.append({**source, "changed_files": changed})

    authored = set(plan["authored_files"])
    require(len(authored) == len(plan["authored_files"]), "Duplicate authored path")
    for name in authored:
        safe_path(name)
        require(name == ".github/workflows/push.yml" or name.startswith("custom/cumulative/"),
                "Authored integration code cannot exempt a native source file: " + name)
        require(name not in original or name == ".github/workflows/push.yml",
                "Integration path replaces an existing source file: " + name)

    resolutions = plan["resolutions"]
    require(set(resolutions) <= candidates.keys(), "Unused or unknown resolution")
    resolved = {}
    for name, options in candidates.items():
        if name in authored:
            continue
        if name in resolutions:
            resolution = resolutions[name]
            if resolution["kind"] == "choose":
                require(resolution["source"] in options, "Resolution chose a noncontributor: " + name)
                expected[name] = options[resolution["source"]]
            elif resolution["kind"] == "merge_text":
                require(name == "files/settings-default.cfg", "Only defaults permit text merging")
                data = merge_bytes(root, name, resolution, sources)
                require({row[0] for row in options.values()} == {"100644"}, "Unexpected defaults file mode")
                expected[name] = ("100644", blob(data))
            else:
                raise ValueError("Unknown resolution method: " + name)
            resolved[name] = {**resolution, "git_blob": expected[name][1]}
        else:
            identities = set(options.values())
            require(len(identities) == 1, "Unresolved source conflict: " + name)
            expected[name] = next(iter(identities))

    actual = tree("HEAD", root)
    dirty = set(git("diff", "HEAD", "--name-only", "-z", root=root).decode().rstrip("\0").split("\0")) - {""}
    if working_tree:
        # No new merge ancestry is claimed before publication. Inspect actual
        # candidate bytes while HEAD still names the original message branch.
        outputs = {"cumulative-source-manifest.json", "cumulative-build-source-manifest.json"}
        untracked = set(git("ls-files", "--others", "--exclude-standard", "-z", root=root)
                        .decode().rstrip("\0").split("\0")) - {""} - outputs
        require(untracked <= set(expected) | authored, "Unexpected untracked source: " + repr(sorted(untracked - set(expected) - authored)))
        for name in dirty | untracked | authored | set(candidates):
            path = root / name
            require(path.is_file() and not path.is_symlink(), "Missing or unsupported candidate file: " + name)
            mode = "100755" if path.stat().st_mode & 0o111 else "100644"
            actual[name] = (mode, blob(path.read_bytes()))
    else:
        for source in sources.values():
            subprocess.run(["git", "merge-base", "--is-ancestor", source["head"], "HEAD"],
                           cwd=root, check=True)
    require(set(actual) == set(expected) | authored,
            "Cumulative source paths differ: " + repr(sorted(set(actual) ^ (set(expected) | authored))))
    for name, identity in expected.items():
        if name not in authored:
            require(actual[name] == identity, "Cumulative source lost or altered a contribution: " + name)

    # The committed tree must contain both features. Only the already-reviewed
    # build-time lookup patch may then change tracked working files.
    lookup_paths = set(plan["lookup_patch_files"])
    if after_lookup:
        require(dirty == lookup_paths, "Unexpected working-tree changes after lookup patch: " + repr(sorted(dirty)))
        lookup = json.loads((root / "loading-repair-source-manifest.json").read_text())
        patched = {name for name, row in lookup["files"].items() if row["before_blob"] != row["after_blob"]}
        require(patched == lookup_paths, "Lookup patch changed another file")
        for name in lookup_paths:
            row = lookup["files"][name]
            require(row["before_blob"] == actual[name][1], "Lookup patch started from another source: " + name)
            require(row["after_blob"] == blob((root / name).read_bytes()), "Lookup output bytes differ: " + name)
    elif not working_tree:
        require(not dirty, "Source verification requires a clean committed tree: " + repr(sorted(dirty)))

    subprocess.run(["git", "diff", "--check", baseline], cwd=root, check=True)
    changed_sources = {}
    for name in sorted(set(candidates) | authored | (lookup_paths if after_lookup else set())):
        path = root / name
        require(path.is_file() and not path.is_symlink(), "Missing or unsupported source file: " + name)
        data = path.read_bytes()
        if not after_lookup or name not in lookup_paths:
            require(blob(data) == actual[name][1], "Working bytes differ from the committed source: " + name)
        require(path.suffix.lower() not in {".esp", ".esm", ".omwaddon", ".omwgame"},
                "Unexpected native plugin in engine integration")
        changed_sources[name] = {"sha256": sha(data), "bytes": len(data), "git_blob": blob(data),
                                 "contributors": sorted(candidates.get(name, {})), "authored_integration": name in authored}
    return {"result": "PASS", "commit": git("rev-parse", "HEAD", root=root).decode().strip(),
            "baseline": baseline, "phase": "prepublication_working_tree" if working_tree else
            ("after_lookup" if after_lookup else "committed_source"),
            "accepted_heads_are_ancestors_of_commit": not working_tree,
            "contributions": contribution_rows, "resolutions": resolved,
            "tracked_file_count": len(actual), "changed_source_files": changed_sources,
            "all_other_committed_source_equal_baseline": True,
            "native_semantic_edits_beyond_contributed_features": False,
            "historical_gates": "Run unchanged in separate pinned worktrees by the workflow",
            "in_game_tested": False, "installed_custom_lua_runtime_tested": False}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--after-lookup", action="store_true")
    parser.add_argument("--working-tree", action="store_true",
                        help="Check unpublished candidate bytes; do not claim merge-parent ancestry")
    args = parser.parse_args()
    plan_path = ROOT / "custom/cumulative/source-plan.json"
    report = verify(ROOT, json.loads(plan_path.read_text()), args.after_lookup, args.working_tree)
    report["source_plan_sha256"] = sha(plan_path.read_bytes())
    output = "cumulative-build-source-manifest.json" if args.after_lookup else "cumulative-source-manifest.json"
    (ROOT / output).write_text(json.dumps(report, indent=2) + "\n")
    print(f"PASS: complete union of {len(report['contributions'])} reviewed heads; "
          f"{report['tracked_file_count']} tracked files; phase {report['phase']}.")


if __name__ == "__main__":
    main()
