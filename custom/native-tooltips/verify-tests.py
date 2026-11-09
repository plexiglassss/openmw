"""Require executed native tooltip regressions and bind the exact XML receipts."""
from pathlib import Path
import argparse
import hashlib
import json
import xml.etree.ElementTree as ET


REQUIRED = {
    "controls": {"TooltipControlsTest": 4, "TravelTooltipViewTest": 4, "TravelTooltips": 6},
    "components": {"Esm3DungeonStatesTest": 19},
    "engine": {"TooltipControlsTest": 4, "TravelTooltipViewTest": 4, "TravelTooltips": 6},
}


def inspect(path, suites):
    data = path.read_bytes()
    root = ET.fromstring(data)
    rows = {}
    for name, minimum in suites.items():
        found = [suite for suite in root.iter("testsuite") if suite.get("name") == name]
        if len(found) != 1:
            raise ValueError(f"Missing or duplicate required suite: {name}")
        cases = found[0].findall("testcase")
        if len(cases) < minimum:
            raise ValueError(f"Required regression cases missing: {name}")
        for case in cases:
            if (case.get("status") != "run" or case.get("result") != "completed"
                    or case.find("failure") is not None or case.find("skipped") is not None
                    or case.find("error") is not None):
                raise ValueError(f"Required case did not pass: {name}.{case.get('name')}")
        rows[name] = [case.get("name") for case in cases]
    return {"file": path.name, "sha256": hashlib.sha256(data).hexdigest(), "suites": rows}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for phase in REQUIRED:
        parser.add_argument("--" + phase, required=True, type=Path)
    parser.add_argument("--output", type=Path, default=Path("native-tooltip-tests.json"))
    args = parser.parse_args()
    result = {"result": "PASS", "in_game_tested": False, "receipts": {}}
    for phase, suites in REQUIRED.items():
        result["receipts"][phase] = inspect(getattr(args, phase), suites)
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print("PASS: required native tooltip form, selection and save tests executed successfully.")


if __name__ == "__main__":
    main()
