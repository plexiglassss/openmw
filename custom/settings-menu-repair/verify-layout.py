"""Compare native menu XML with its accepted pre-grouping reference."""
from pathlib import Path
import hashlib
import json
import subprocess
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
LAYOUT = "files/data/mygui/openmw_settings_window.layout"
REFERENCE = "82dcf5a82f442c78affe08f78dac300c8d160f4a"
STOCK = "4ff261bf7c0d42a6cc06dd1ebc5d451802cff055"
NATIVE = ("Preferences", "Audio", "Controls", "Video", "Scripts", "Language")


def revision_bytes(revision):
    return subprocess.check_output(["git", "show", revision + ":" + LAYOUT], cwd=ROOT)


def parse(data):
    # RC1 has this redundant identical attribute; the accepted custom reference
    # already removed it. Normalize only that known parser defect for comparison.
    text = data.decode("utf-8-sig").replace(
        'name="ScriptBox" type="Widget" align="Stretch" name="ScriptBox"',
        'name="ScriptBox" type="Widget" align="Stretch"')
    return ET.fromstring(text).find("./Widget")


def canonical(node):
    return (node.tag, tuple(sorted(node.attrib.items())),
            (node.text or "").strip(), tuple(canonical(child) for child in node))


def pages(window):
    result = {}
    for page in window.find('./Widget[@name="SettingsTab"]').findall('./Widget[@type="TabItem"]'):
        caption = page.find('./Property[@key="Caption"]').get('value').strip()
        for name in NATIVE:
            if caption == "#{OMWEngine:" + name + "}":
                if name in result:
                    raise ValueError("Duplicate native page: " + name)
                result[name] = page
    if tuple(result) != NATIVE:
        raise ValueError("Native pages were removed, renamed or reordered")
    return result


def main():
    actual_bytes = (ROOT / LAYOUT).read_bytes()
    reference_bytes, stock_bytes = revision_bytes(REFERENCE), revision_bytes(STOCK)
    actual, reference, stock = map(parse, (actual_bytes, reference_bytes, stock_bytes))
    current_pages, reference_pages, stock_pages = map(pages, (actual, reference, stock))
    for name in NATIVE:
        if canonical(current_pages[name]) != canonical(reference_pages[name]):
            raise ValueError("Native page differs from accepted pre-grouping layout: " + name)
        if current_pages[name].find('./UserString[@key="CustomSettingsPage"]') is not None:
            raise ValueError("Native page opted into custom grouping: " + name)
    if actual.attrib != reference.attrib:
        raise ValueError("Stock outer window attributes changed")
    actual_tabs = actual.find('./Widget[@name="SettingsTab"]')
    reference_tabs = reference.find('./Widget[@name="SettingsTab"]')
    if (actual_tabs.attrib != reference_tabs.attrib
            or [canonical(child) for child in actual_tabs if child.tag != "Widget"]
            != [canonical(child) for child in reference_tabs if child.tag != "Widget"]):
        raise ValueError("Stock tab container geometry or properties changed")
    for path in ('./Property[@key="MinSize"]', './Widget[@name="OkButton"]'):
        if canonical(actual.find(path)) != canonical(reference.find(path)):
            raise ValueError("Stock window minimum or OK control changed: " + path)
    markers = [node.get('value') for node in actual.findall('.//UserString[@key="CustomSettingsPage"]')]
    if markers != ["Gameplay", "Journal"]:
        raise ValueError("Unexpected layout pages eligible for custom grouping")
    differences = [name for name in NATIVE if canonical(reference_pages[name]) != canonical(stock_pages[name])]
    if differences != ["Controls", "Video"]:
        raise ValueError("Unexpected pre-existing stock extensions: " + repr(differences))
    report = {
        "result": "PASS", "reference": REFERENCE, "upstream_rc1": STOCK,
        "native_pages_unchanged": list(NATIVE),
        "retained_preexisting_extensions": {"Controls": "Mod Keybindings", "Video": "Location rendering profiles"},
        "custom_layout_pages": markers,
        "sha256": {"candidate": hashlib.sha256(actual_bytes).hexdigest(),
                   "reference": hashlib.sha256(reference_bytes).hexdigest(),
                   "stock": hashlib.sha256(stock_bytes).hexdigest()},
        "scope": "Source XML comparison only; native runtime tests are required separately.",
    }
    (ROOT / "stock-menu-layout.json").write_text(json.dumps(report, indent=2) + "\n")
    print("PASS: six native menu subtrees, tab container, outer window and OK control retain the accepted reference layout")


if __name__ == "__main__":
    main()
