"""Require executed native Smart Map, settings and actual MyGUI regressions."""
from pathlib import Path
import argparse
import hashlib
import json
import xml.etree.ElementTree as ET


POLICY = {
    "SmartMapPolicyTest": {
        "DisabledMasterLeavesViewAndDoesNotQueryWorld",
        "InteriorSelectionDoesNotDependOnExteriorRulesOrSpeed",
        "ExteriorRuleCanBeDisabledIndependently",
        "DoorThresholdIsInclusiveAndStopsBeforeActorQuery",
        "DoorSliderEndpointsReachTheReferenceQuery",
        "EnabledHostileRuleSelectsLocalInSparseExterior",
        "RestForbiddenSkipsHostileQueryButPreservesDoorRule",
        "FastExteriorMovementIsSuppressedAtInclusiveBoundary",
        "TeleportArrivalDoesNotInheritMovementSuppression",
        "HostileFightAndDistanceThresholdsAreInclusive",
        "InvalidActorDistanceCannotTriggerLocalMap",
    },
    "SmartMapLifecycleTest": {
        "CellDecisionWaitsForNextScriptFrameAndRunsOnce",
        "NewArrivalReplacesDeferredDecision",
        "ManualSelectionCancelsPendingAutomaticSwitch",
        "ResetDropsOldSaveDecisionAndAcceptsNewArrival",
    },
    "SmartMapLocationNameTest": {
        "RecognizesGenericInternalPrefixesWithoutModNames",
        "PreservesReadableNamesAndAmbiguousIdentifiers",
    },
}
CONTROLS = {
    "SmartMapControlsTest": {
        "AllOptionsRemainReadableAndReachableAtSmallAndLargeSizes",
        "SupportedThresholdEndpointsSurviveResizingAndScrolling",
        "LargeFontsAndLongTranslationsDoNotOverlapOrClipControls",
        "KeyboardFocusRevealsLastAndFirstControlsInSmallViewport",
        "MouseWheelOverLabelsAndButtonsScrollsWithoutChangingThresholds",
    },
}
SETTINGS = {
    "SettingsValuesTest": {
        "smartMapDefaultsAndExternalValuesUseSupportedRanges",
        "smartMapSettingsPersistIndependentlyOfMapAppearance",
    },
}


def inspect(path, required):
    data = path.read_bytes()
    root = ET.fromstring(data)
    result = {}
    for name, expected in required.items():
        suites = [suite for suite in root.iter("testsuite") if suite.get("name") == name]
        if len(suites) != 1:
            raise ValueError("Missing or duplicate native suite: " + name)
        cases = suites[0].findall("testcase")
        names = [case.get("name") for case in cases]
        if len(names) != len(set(names)) or not expected <= set(names):
            raise ValueError(f"Required native cases missing or duplicated: {name}: {sorted(expected - set(names))}")
        for case in cases:
            if case.get("name") not in expected:
                continue
            if (case.get("status") != "run" or case.get("result") != "completed"
                    or any(child.tag in {"failure", "error", "skipped"} for child in case)):
                raise ValueError(f"Native case did not pass: {name}.{case.get('name')}")
        result[name] = sorted(expected)
    return {"file": path.name, "sha256": hashlib.sha256(data).hexdigest(), "required_passed": result}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for phase in ("controls", "components", "engine"):
        parser.add_argument("--" + phase, required=True, type=Path)
    parser.add_argument("--output", type=Path, default=Path("smart-map-native-tests.json"))
    args = parser.parse_args()
    result = {
        "result": "PASS",
        "gameplay_tested": False,
        "scope": "Production policy and lifecycle helper, actual MyGUI widgets and settings file persistence; not an installed-modlist gameplay test.",
        "receipts": {
            "controls": inspect(args.controls, POLICY | CONTROLS),
            "components": inspect(args.components, SETTINGS),
            "engine": inspect(args.engine, POLICY | CONTROLS),
        },
    }
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print("PASS: Smart Map policy, lifecycle, real native form and settings regressions executed successfully.")


if __name__ == "__main__":
    main()
