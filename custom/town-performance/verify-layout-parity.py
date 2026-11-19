"""Require matching real native Lua UI snapshots; report timings without a speed gate."""
from pathlib import Path
import argparse
import hashlib
import json
import math
import xml.etree.ElementTree as ET


IMAGE_INHERITANCE_CASE = "ImageTemplatesInheritResourcesAcrossOverridesAndUpdates"
IMAGE_INHERITANCE_SNAPSHOTS = (
    "image-inherited",
    "image-overridden",
    "image-override-removed",
    "image-template-updated",
    "image-unchanged",
)
EXPECTED = {
    "StableTemplateHierarchyAndSiblingOrderRepair",
    "RetainedAttachmentsPreserveOwnershipAndDynamicObservations",
    "MutableTemplateSlotsAndReplacementKeepRetainedChild",
    "MovingRetainedElementRelayoutsThePreviousRootImmediately",
    "ReparentIntoGrowingSiblingUnderTheSameRoot",
    "NestedAutoSizeAndRelativeGrowRespondToASingleRootResize",
    "NestedWrappingTextPaddingVisibilityAndReorderStayConsistent",
    "RepeatedHudUpdatesBenchmark",
    "LayoutReuseTracksConstraintsAndMutatedRetainedExternalFields",
    "LayoutReuseFallsBackForLiveMetatableReads",
    "RetainedTextEditorsKeepCaretSelectionAndScrollPasses",
    "NestedRetainedHudSparseAndFullUpdatesBenchmark",
    "SparsePropertyRequestsMergeAndPreserveFullRefresh",
    "BatchedHudPropertiesBenchmark",
    IMAGE_INHERITANCE_CASE,
}
EXPECTED_PROPERTIES = {
    "PlainTablesPreserveTypesDefaultsAndImmediateMutation",
    "InvalidValuesAndOverriddenTemplateErrorsRestoreTheStack",
    "MetatableLookupsPreserveOrderSideEffectsAndProtectedErrors",
    "ReturnedOwningValuesSurviveFieldReplacementAndCollection",
    "MetatableOwnerReplacementUsesTheOriginalTemplate",
    "TextureResourcesPreserveInheritanceDefaultsAndErrors",
    "MissingFieldsPreserveLegacyNilTypeSemantics",
}


def identity(path):
    data = path.read_bytes()
    return {"sha256": hashlib.sha256(data).hexdigest(), "bytes": len(data)}


def read_cases(path, expected_mode):
    root = ET.parse(path).getroot()
    if (any(int(root.get(key, "0")) for key in ("failures", "errors", "disabled"))
            or any(node.tag in {"failure", "error", "skipped"} for node in root.iter())):
        raise ValueError("Failed or incomplete native execution: " + path.name)
    cases = {}
    for case in root.iter("testcase"):
        if case.get("classname") != "LuaUiLayoutTest":
            continue
        name = case.get("name")
        if name in cases or case.get("status") != "run" or case.get("result") != "completed":
            raise ValueError("Duplicate or unexecuted layout case: " + str(name))
        properties = {p.get("name"): p.get("value") for p in case.findall("./properties/property")}
        if not properties.get("layout_snapshots"):
            raise ValueError("Missing native hierarchy snapshots: " + str(name))
        if properties.get("layout_mode") != expected_mode:
            raise ValueError("Native layout evidence has the wrong process mode: " + str(name))
        if name == IMAGE_INHERITANCE_CASE:
            labels = tuple(line[1:-1] for line in properties["layout_snapshots"].splitlines()
                           if line.startswith("[") and line.endswith("]"))
            if labels != IMAGE_INHERITANCE_SNAPSHOTS:
                raise ValueError("Missing or reordered native Image inheritance snapshots: " + path.name)
        cases[name] = properties
    if set(cases) != EXPECTED:
        raise ValueError("Layout suite is missing or changed: " + repr(sorted(set(cases) ^ EXPECTED)))
    return cases


def verify_property_cases(path, expected_mode):
    cases = set()
    for case in ET.parse(path).getroot().iter("testcase"):
        if case.get("classname") != "LuaUiPropertyTest":
            continue
        name = case.get("name")
        properties = {p.get("name"): p.get("value") for p in case.findall("./properties/property")}
        if (name in cases or case.get("status") != "run" or case.get("result") != "completed"
                or properties.get("layout_mode") != expected_mode):
            raise ValueError("Incomplete or mismatched native property case: " + str(name))
        cases.add(name)
    if cases != EXPECTED_PROPERTIES:
        raise ValueError("Native property suite is missing or changed: " + repr(sorted(cases ^ EXPECTED_PROPERTIES)))


def compare(optimized, legacy):
    current, previous = read_cases(optimized, "optimized"), read_cases(legacy, "legacy")
    verify_property_cases(optimized, "optimized")
    verify_property_cases(legacy, "legacy")
    for label, values, expected in (("optimized", current, "reuse"), ("legacy", previous, "legacy")):
        if any(case.get("native_update_mode") != expected for case in values.values()):
            raise ValueError("Native geometry reuse mode differs from the requested process mode: " + label)
    cases = {}
    for name in sorted(EXPECTED):
        left, right = current[name]["layout_snapshots"], previous[name]["layout_snapshots"]
        if left != right:
            raise ValueError("Native layout behavior differs from the legacy path: " + name)
        cases[name] = {"snapshot_sha256": hashlib.sha256(left.encode()).hexdigest(),
                       "snapshot_bytes": len(left.encode()), "snapshots_equal": True}
    timings = {}
    for label, values in (("optimized", current), ("legacy", previous)):
        bench = values["RepeatedHudUpdatesBenchmark"]
        timing = {key: int(bench[key]) for key in ("median_update_ns", "minimum_update_ns", "updates_per_batch")}
        if any(not math.isfinite(value) or value <= 0 for value in timing.values()):
            raise ValueError("Invalid native benchmark values: " + label)
        timings[label] = timing
    if timings["optimized"]["updates_per_batch"] != timings["legacy"]["updates_per_batch"]:
        raise ValueError("Benchmark iteration counts differ")
    return {
        "schema_version": 1, "result": "PASS", "native_layout_cases": len(cases),
        "native_property_cases_per_mode": len(EXPECTED_PROPERTIES),
        "property_cases": sorted(EXPECTED_PROPERTIES),
        "image_resource_inheritance": {
            "case": IMAGE_INHERITANCE_CASE,
            "snapshot_labels": list(IMAGE_INHERITANCE_SNAPSHOTS),
            "modes": ["optimized", "legacy"],
            "snapshots_equal": True,
        },
        "optimized_xml": identity(optimized), "legacy_xml": identity(legacy),
        "cases": cases, "native_benchmark": timings,
        "benchmark_ratio_optimized_to_legacy": (
            timings["optimized"]["median_update_ns"] / timings["legacy"]["median_update_ns"]),
        "benchmark_is_gate": False,
        "scope": "Actual MyGUI and OpenMW Lua UI; substituted platform file/render/font services",
        "in_game_tested": False,
    }


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("optimized", type=Path)
    parser.add_argument("legacy", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    report = compare(args.optimized, args.legacy)
    args.output.write_text(json.dumps(report, indent=2) + "\n")
    print(f"PASS: {report['native_layout_cases']} real native layout cases match the legacy path.")
    print("Native HUD update median ratio (candidate / legacy): "
          f"{report['benchmark_ratio_optimized_to_legacy']:.4f}; game FPS not measured.")
