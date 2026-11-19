"""Validate current RC1 source evidence separately from its historical evidence."""
from pathlib import Path
import hashlib
import json
import subprocess
import xml.etree.ElementTree as ET


def verify(source_root, evidence_root, commit):
    source_root, evidence_root = Path(source_root), Path(evidence_root)
    plan_path = source_root / "custom/rc1/source-plan.json"
    plan = json.loads(plan_path.read_text())
    expected_hash = hashlib.sha256(plan_path.read_bytes()).hexdigest()
    current, historical = {}, {}
    for name, phase in (("rc1-source-manifest.json", "source"), ("rc1-build-source-manifest.json", "build")):
        report = json.loads((evidence_root / name).read_text())
        if (report.get("result") != "PASS" or report.get("commit") != commit or report.get("phase") != phase
                or report.get("source_plan_sha256") != expected_hash
                or report.get("preserved_ancestry_verified") is not True
                or report.get("upstream_rc1") != plan["upstream_rc1"]
                or report.get("custom_union") != plan["custom_union"]
                or report.get("lookup_optimization") != "committed source; no CI transformation"):
            raise ValueError("RC1 source evidence belongs to another tree or phase: " + name)
        current[name] = report
    union = plan["custom_union"]
    prior_bytes = subprocess.check_output(
        ["git", "show", union + ":custom/town-performance/source-plan.json"], cwd=source_root)
    prior_hash = hashlib.sha256(prior_bytes).hexdigest()
    for name, phase in (("town-performance-source-manifest.json", "committed_source"),
                        ("town-performance-build-source-manifest.json", "after_lookup")):
        report = json.loads((evidence_root / name).read_text())
        if (report.get("result") != "PASS" or report.get("commit") != union or report.get("phase") != phase
                or report.get("source_plan_sha256") != prior_hash
                or report.get("accepted_heads_are_ancestors_of_commit") is not True):
            raise ValueError("Historical cumulative source evidence differs from the union: " + name)
        historical[name] = report
    return current, historical, json.loads(prior_bytes)


def require_rc1_cases(evidence_root):
    required = {
        "components-tests.xml": {
            ("LuaScriptsContainerTest", "RealTimersRemainLiveDuringIdleAndPausedSimulation"),
            ("LuaScriptsContainerTest", "RealTimersRestoreRelativeDeadlinesAndRunWhilePaused"),
        },
        "openmw-tests.xml": {
            ("LuaUiLayoutTest", "Rc1CursorsAndLegacyPointersPreservePrecedenceAndInheritance"),
        },
    }
    for name, expected in required.items():
        cases = {(c.get("classname"), c.get("name")): c
                 for c in ET.parse(Path(evidence_root) / name).getroot().iter("testcase")}
        if not expected <= cases.keys():
            raise ValueError("Required RC1 native regression did not run: " + name)
        for key in expected:
            case = cases[key]
            if (case.get("status") != "run" or case.get("result") != "completed"
                    or any(child.tag in {"failure", "error", "skipped"} for child in case)):
                raise ValueError("Required RC1 native regression failed: " + repr(key))
