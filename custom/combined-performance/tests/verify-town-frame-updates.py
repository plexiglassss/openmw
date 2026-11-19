"""Compare the same Windows binaries with the town update switch on and off."""
import json
from pathlib import Path
from xml.etree import ElementTree

DISPATCH = "LuaScriptsContainerTest.RepeatedActorUpdateDispatchBenchmark"
ERRORS = "LuaScriptsContainerTest.UpdateHandlersKeepTracebacksOrderAndStateAfterCollection"
STATS = "MWLuaMagicBindingsTest.ModifiedStatBindingsReadLiveValuesAndPendingSelfWrites"
TYPES = "MWLuaMagicBindingsTest.RepeatedLuaTypeQueriesObserveRecordReplacementAndRemoval"
ATTACHMENTS = "LuaUiLayoutTest.RetainedAttachmentsPreserveOwnershipAndDynamicObservations"
HUD = "LuaUiLayoutTest.NestedRetainedHudSparseAndFullUpdatesBenchmark"


def executed(path):
    result = {}
    for case in ElementTree.parse(path).getroot().iter("testcase"):
        name = case.get("classname") + "." + case.get("name")
        if case.get("status") != "run" or case.get("result") != "completed" or case.find("failure") is not None:
            raise ValueError(f"{path}: {name} did not pass")
        result[name] = {p.get("name"): p.get("value") for p in case.findall("./properties/property")}
    return result


def main():
    modes = {}
    snapshots = []
    for label, components, engine in (
        ("current", "components-tests.xml", "openmw-tests.xml"),
        ("previous", "town-previous-components.xml", "town-previous-engine.xml"),
    ):
        tests = executed(components) | executed(engine)
        for name in (DISPATCH, ERRORS, STATS, TYPES, ATTACHMENTS, HUD):
            if name not in tests:
                raise ValueError("Missing test: " + name)
        snapshots.append({name: values["layout_snapshots"] for name, values in tests.items()
                          if name.startswith("LuaUiLayoutTest.")})
        modes[label] = {
            "median_ns_per_lua_update_callback": float(tests[DISPATCH]["median_ns_per_callback"]),
            "ai_stat_lookups_per_modified_read": int(tests[STATS]["native_stat_reads"]),
            "stable_widget_attachment_calls": int(tests[ATTACHMENTS]["unchanged_attachment_calls"]),
            "hud": {key: {
                "median_ns_per_refresh": float(tests[HUD][key + "_median_update_ns"]),
                "attachment_calls": int(tests[HUD][key + "_attachment_calls"]),
            } for key in ("unchanged", "sparse", "full", "retained")},
        }
    if not snapshots[0] or snapshots[0] != snapshots[1]:
        raise ValueError("Rendered widget hierarchy or state changed between modes")
    current, previous = modes["current"], modes["previous"]
    if (current["stable_widget_attachment_calls"] != 0 or previous["stable_widget_attachment_calls"] <= 0
            or current["ai_stat_lookups_per_modified_read"] != 1 or previous["ai_stat_lookups_per_modified_read"] != 2):
        raise ValueError("Expected stock stat read and stable attachment paths did not execute")
    report = {"result": "PASS", "matching_ui_cases": len(snapshots[0]), "modes": modes,
              "timing_scope": "Headless Windows engine fixtures. Game frame rates require the real modlist."}
    Path("town-frame-updates.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report))


if __name__ == "__main__":
    main()
