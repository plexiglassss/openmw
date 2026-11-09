"""Verify native UI state and work counts with both same-executable fallbacks."""

from pathlib import Path
import argparse
import hashlib
import importlib.util
import json
import re


SPEC = importlib.util.spec_from_file_location(
    "native_layout_parity", Path(__file__).with_name("verify-layout-parity.py"))
PARITY = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PARITY)

BENCHMARK = "NestedRetainedHudSparseAndFullUpdatesBenchmark"
WORKLOADS = ("unchanged", "sparse", "full", "retained")
WORKLOAD_PARAMETERS = {
    "hud_rows": 32,
    "timed_batches": 5,
    "updates_per_batch": 24,
    "warmup_updates": 8,
}
NEW_SNAPSHOTS = {
    "LayoutReuseTracksConstraintsAndMutatedRetainedExternalFields": (
        "initial-retained-growth", "mutated-external-and-parent", "retained-hidden",
        "retained-shown", "child-action-updates-outer-root", "retained-reordered"),
    "RetainedTextEditorsKeepCaretSelectionAndScrollPasses": (
        "editors-created", "editors-caret-and-scroll", "editors-retained-refresh",
        "editors-narrow", "editors-wide", "editor-font-change"),
    BENCHMARK: ("nested-created",) + tuple("nested-" + workload for workload in WORKLOADS),
}
FALLBACK_CASES = {
    "LayoutReuseFallsBackForLiveMetatableReads": ("fallback", "moved"),
    "RetainedTextEditorsKeepCaretSelectionAndScrollPasses": ("fallback",),
}


def positive_int(properties, key, context):
    value = int(properties[key])
    if value <= 0:
        raise ValueError("Nonpositive native benchmark value: " + context + "/" + key)
    return value


def labels(properties):
    return tuple(line[1:-1] for line in properties["layout_snapshots"].splitlines()
                 if line.startswith("[") and line.endswith("]"))


def read_mode(path, mode):
    cases = PARITY.read_cases(path, "optimized")
    PARITY.verify_property_cases(path, "optimized")
    native_mode = "reuse" if mode == "combined" else "legacy"
    combined_mode = "disabled" if mode == "master_off" else "enabled"
    for name, properties in cases.items():
        if properties.get("native_update_mode") != native_mode:
            raise ValueError("Wrong native update mode in " + mode + ": " + name)
        if properties.get("combined_performance_mode") != combined_mode:
            raise ValueError("Wrong combined performance mode in " + mode + ": " + name)
    for name, expected in NEW_SNAPSHOTS.items():
        if labels(cases[name]) != expected:
            raise ValueError("Missing or reordered native snapshots in " + mode + ": " + name)
    dynamic_labels = labels(cases["LayoutReuseFallsBackForLiveMetatableReads"])
    patterns = (r"dynamic-external-reads-[1-9][0-9]*", r"dynamic-template-reads-[1-9][0-9]*",
                r"moved-dynamic-external-reads-[1-9][0-9]*")
    if len(dynamic_labels) != len(patterns) or any(
            not re.fullmatch(pattern, label) for pattern, label in zip(patterns, dynamic_labels)):
        raise ValueError("Dynamic lookup snapshots are incomplete: " + mode)
    fallback_counts = {}
    for name, prefixes in FALLBACK_CASES.items():
        fallback_counts[name] = {}
        for prefix in prefixes:
            calculations = positive_int(cases[name], prefix + "_layout_calculations", mode + "/" + name)
            reuses = int(cases[name][prefix + "_layout_reuses"])
            if reuses != 0:
                raise ValueError("Dynamic/editor fallback incorrectly reused geometry: " + mode + "/" + name)
            fallback_counts[name][prefix] = {"calculations": calculations, "reuses": reuses}
    benchmark = cases[BENCHMARK]
    for key, expected in WORKLOAD_PARAMETERS.items():
        if int(benchmark[key]) != expected:
            raise ValueError("Unexpected native HUD workload parameters: " + mode + "/" + key)
    workloads = {}
    for workload in WORKLOADS:
        median = positive_int(benchmark, workload + "_median_update_ns", mode)
        calculations = positive_int(benchmark, workload + "_layout_calculations", mode)
        reuses = int(benchmark[workload + "_layout_reuses"])
        if (mode == "combined" and reuses <= 0) or (mode != "combined" and reuses != 0):
            raise ValueError("Native HUD reuse counters disagree with process mode: " + mode + "/" + workload)
        workloads[workload] = {"median_update_ns": median, "layout_calculations": calculations,
                               "layout_reuses": reuses}
    return cases, workloads, fallback_counts


def compare(combined, ui_legacy, master_off):
    paths = {"combined": combined, "ui_legacy_updates": ui_legacy, "master_off": master_off}
    results = {mode: read_mode(path, mode) for mode, path in paths.items()}
    snapshots = {}
    for name in sorted(PARITY.EXPECTED):
        reference = results["combined"][0][name]["layout_snapshots"]
        for mode, (cases, _, _) in results.items():
            if cases[name]["layout_snapshots"] != reference:
                raise ValueError("Native UI state differs in " + mode + ": " + name)
        snapshots[name] = {"sha256": hashlib.sha256(reference.encode()).hexdigest(),
                           "bytes": len(reference.encode()), "equal_in_all_modes": True}
    benchmarks = {mode: result[1] for mode, result in results.items()}
    ratios = {}
    for mode in ("ui_legacy_updates", "master_off"):
        ratios[mode] = {}
        for workload in WORKLOADS:
            current = benchmarks["combined"][workload]
            previous = benchmarks[mode][workload]
            ratios[mode][workload] = {
                "median_time_combined_to_fallback": current["median_update_ns"] / previous["median_update_ns"],
                "layout_calculations_combined_to_fallback": (
                    current["layout_calculations"] / previous["layout_calculations"]),
            }
    if results["ui_legacy_updates"][2] != results["master_off"][2]:
        raise ValueError("Fallback modes executed different dynamic/editor geometry work")
    for workload in WORKLOADS:
        if (benchmarks["ui_legacy_updates"][workload]["layout_calculations"]
                != benchmarks["master_off"][workload]["layout_calculations"]):
            raise ValueError("Fallback modes executed different HUD geometry work: " + workload)
    return {
        "schema_version": 1, "result": "PASS",
        "native_layout_cases_per_mode": len(PARITY.EXPECTED),
        "native_property_cases_per_mode": len(PARITY.EXPECTED_PROPERTIES),
        "xml": {mode: PARITY.identity(path) for mode, path in paths.items()},
        "snapshots": snapshots,
        "workload_parameters": WORKLOAD_PARAMETERS,
        "native_benchmark": benchmarks,
        "combined_to_fallback_ratios": ratios,
        "conservative_fallback_counts": {mode: result[2] for mode, result in results.items()},
        "mode_contract": {
            "combined": {"property_reader": "optimized", "native_updates": "reuse", "master": "enabled"},
            "ui_legacy_updates": {
                "property_reader": "optimized", "native_updates": "legacy", "master": "enabled"},
            "master_off": {"property_reader": "optimized", "native_updates": "legacy", "master": "disabled"},
        },
        "benchmark_is_timing_gate": False,
        "scope": "Actual MyGUI and OpenMW Lua UI; substituted platform file/render/font services",
        "in_game_tested": False,
    }


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("combined", type=Path)
    parser.add_argument("ui_legacy_updates", type=Path)
    parser.add_argument("master_off", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    report = compare(args.combined, args.ui_legacy_updates, args.master_off)
    args.output.write_text(json.dumps(report, indent=2) + "\n")
    print(f"PASS: {report['native_layout_cases_per_mode']} native UI cases match in all three modes.")
    for workload, values in report["combined_to_fallback_ratios"]["master_off"].items():
        print(f"{workload}: native update time ratio {values['median_time_combined_to_fallback']:.4f}, "
              f"geometry calculation ratio {values['layout_calculations_combined_to_fallback']:.4f}; "
              "game FPS not measured.")
