"""Require the combined candidate's executed native query regressions and measurements."""
from pathlib import Path
import argparse
import hashlib
import json
import math
import xml.etree.ElementTree as ET


CELL_CASES = {
    "typedQuerySharesSnapshotsWithoutInvalidatingRegisteredPointers",
    "typedQueryObservesDeletionAndUndeletionInSameUpdate",
    "typedQueryDoesNotTrustAnOldEmptyMergedViewAfterUndelete",
    "typedQueryTracksContentDeletionAndRefDataReplacement",
    "typedQueryPreservesMoveMembershipAndOrder",
    "typedQueryRefreshesAfterInsertionAndKeepsOldEmptySnapshot",
    "typedQueryFiltersPlayerAndHiddenMarkers",
    "typedQueryRefreshesIdentityAndSpecialTypeAfterReplacement",
    "typedQueryKeepsMembershipWhileDynamicPropertiesChange",
    "registryAvoidsOnlyExactlyIdempotentRegistration",
    "typedQueryRefreshesAfterRegistryRemoval",
    "typedQueryRetainsEmptyCellShortcutAndRejectsUnsupportedTypes",
    "typedQueryBenchmark",
}
MAGIC_CASES = {
    "nativeQueryFactoriesMatchLuaAdapterCallsAndMasterMode",
    "effectSourceQueryPreservesOrderDuplicatesAndImmutableSnapshots",
    "effectSourceQueryObservesCasterAvailabilityAndAccessContext",
    "effectSourceQuerySelectionDoesNotReorderOrDeduplicateResults",
    "magnitudeQueryResolvesActorOnceAndObservesSameFrameMutations",
    "nativeEffectPresenceMatchesLegacyIterationRatherThanTotalMagnitude",
    "nativeEffectSourceQueryBenchmark",
}


def identity(path):
    data = path.read_bytes()
    return {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}


def verify(path, fallback_path):
    root = ET.parse(path).getroot()
    cases = list(root.iter("testcase"))
    if (not cases or len(cases) != int(root.get("tests", "0"))
            or any(int(root.get(key, "0")) for key in ("failures", "errors", "disabled"))
            or any(node.tag in {"failure", "error", "skipped"} for node in root.iter())
            or any(case.get("status") != "run" or case.get("result") != "completed" for case in cases)):
        raise ValueError("Incomplete or failed native test execution: " + path.name)
    indexed = {(case.get("classname"), case.get("name")): case for case in cases}
    if len(indexed) != len(cases):
        raise ValueError("Duplicate native test identities")
    required = ({("CellLookupTest", name) for name in CELL_CASES}
                | {("MWLuaMagicBindingsTest", name) for name in MAGIC_CASES})
    if not required <= indexed.keys():
        raise ValueError("Required combined candidate regressions did not run: " + repr(sorted(required - indexed.keys())))

    def properties(suite, name):
        return {p.get("name"): p.get("value") for p in indexed[(suite, name)].findall("./properties/property")}

    factory_name = "nativeQueryFactoriesMatchLuaAdapterCallsAndMasterMode"
    if properties("MWLuaMagicBindingsTest", factory_name).get("combined_performance_mode") != "enabled":
        raise ValueError("The production Lua factory regression did not run in combined mode")
    fallback_root = ET.parse(fallback_path).getroot()
    if (any(int(fallback_root.get(key, "0")) for key in ("failures", "errors", "disabled"))
            or any(node.tag in {"failure", "error", "skipped"} for node in fallback_root.iter())):
        raise ValueError("Failed or incomplete master-off native execution")
    factories = [case for case in fallback_root.iter("testcase")
                 if case.get("classname") == "MWLuaMagicBindingsTest" and case.get("name") == factory_name]
    if (len(factories) != 1 or factories[0].get("status") != "run" or factories[0].get("result") != "completed"
            or {p.get("name"): p.get("value") for p in factories[0].findall("./properties/property")}
            .get("combined_performance_mode") != "disabled"):
        raise ValueError("The production Lua factory regression did not run in master-off mode")

    def positive(values, key):
        result = float(values[key])
        if not math.isfinite(result) or result <= 0:
            raise ValueError("Invalid native measurement: " + key)
        return result

    cell = properties("CellLookupTest", "typedQueryBenchmark")
    for key, expected in (("references", 1024), ("rounds", 7), ("queries_per_round", 1000)):
        if int(cell[key]) != expected:
            raise ValueError("Cell-query benchmark workload changed: " + key)
    cell_before = positive(cell, "uncached_median_ns_per_query")
    cell_after = positive(cell, "cached_median_ns_per_query")
    magic = properties("MWLuaMagicBindingsTest", "nativeEffectSourceQueryBenchmark")
    magic_before = positive(magic, "legacy_materialization_model_median_ns_per_actor")
    magic_after = positive(magic, "compiled_query_median_ns_per_actor")
    return {
        "schema_version": 1, "result": "PASS", "xml": identity(path), "master_off_xml": identity(fallback_path),
        "required_cell_cases": sorted(CELL_CASES), "required_magic_cases": sorted(MAGIC_CASES),
        "cell_query_benchmark": {
            "uncached_median_ns_per_query": cell_before,
            "cached_median_ns_per_query": cell_after,
            "cached_to_uncached_ratio": cell_after / cell_before,
            "references": 1024, "queries_per_round": 1000, "rounds": 7,
            "scope": cell["scope"],
        },
        "effect_query_benchmark": {
            "legacy_materialization_model_median_ns_per_actor": magic_before,
            "compiled_query_median_ns_per_actor": magic_after,
            "compiled_to_materialization_ratio": magic_after / magic_before,
            "scope": magic["scope"],
        },
        "timing_is_acceptance_gate": False,
        "gameplay_fps_measured": False,
        "installed_custom_lua_runtime_tested": False,
    }


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("engine_xml", type=Path)
    parser.add_argument("master_off_xml", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    report = verify(args.engine_xml, args.master_off_xml)
    args.output.write_text(json.dumps(report, indent=2) + "\n")
    print("PASS: combined native cell-query and magic-query tests executed; measurements recorded.")
