"""Package evidence contract tests; authored fixtures are not engine test results."""
from pathlib import Path
import hashlib
import importlib.util
import json
import tempfile
import unittest
from unittest.mock import patch

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("rc1_package_sources", HERE / "package-sources.py")
package = importlib.util.module_from_spec(spec)
spec.loader.exec_module(package)


class PackageSources(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.plan = {"upstream_rc1": "rc1-fixture", "custom_union": "union-fixture"}
        path = self.root / "custom/rc1/source-plan.json"
        path.parent.mkdir(parents=True)
        path.write_text(json.dumps(self.plan))
        self.prior = b'{"baseline":"old-combined-fixture"}'
        for name, phase in (("rc1-source-manifest.json", "source"), ("rc1-build-source-manifest.json", "build")):
            self.write(name, {"result": "PASS", "commit": "current-fixture", "phase": phase,
                "source_plan_sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
                "preserved_ancestry_verified": True, **self.plan,
                "lookup_optimization": "committed source; no CI transformation"})
        for name, phase in (("town-performance-source-manifest.json", "committed_source"),
                            ("town-performance-build-source-manifest.json", "after_lookup")):
            self.write(name, {"result": "PASS", "commit": "union-fixture", "phase": phase,
                "source_plan_sha256": hashlib.sha256(self.prior).hexdigest(),
                "accepted_heads_are_ancestors_of_commit": True})

    def write(self, name, value):
        (self.root / name).write_text(json.dumps(value))

    def verify(self):
        with patch.object(package.subprocess, "check_output", return_value=self.prior) as call:
            result = package.verify(self.root, self.root, "current-fixture")
            call.assert_called_once_with(["git", "show", "union-fixture:custom/town-performance/source-plan.json"], cwd=self.root)
            return result

    def test_accept_current_rc1_with_separate_historical_union(self):
        current, historical, prior = self.verify()
        self.assertEqual(set(current), {"rc1-source-manifest.json", "rc1-build-source-manifest.json"})
        self.assertEqual(set(historical), {"town-performance-source-manifest.json", "town-performance-build-source-manifest.json"})
        self.assertEqual(prior["baseline"], "old-combined-fixture")

    def test_reject_current_or_historical_evidence_relabeling(self):
        for name in ("rc1-source-manifest.json", "rc1-build-source-manifest.json",
                     "town-performance-source-manifest.json", "town-performance-build-source-manifest.json"):
            original = json.loads((self.root / name).read_text())
            for key, value in (("commit", "other"), ("phase", "other"), ("source_plan_sha256", "other"), ("result", "FAIL")):
                with self.subTest(name=name, key=key):
                    self.write(name, {**original, key: value})
                    with self.assertRaises(ValueError):
                        self.verify()
                    self.write(name, original)

    def test_native_cases_must_run_successfully(self):
        interactions = [("ModControlsInteractionTest", name) for name in (
            "ActualLabelsSelectModsCaptureKeysAndScrollBothPanels",
            "ConsumedLuaWheelDoesNotScrollTheNativeHost",
            "OverflowingNativeEditorKeepsWheelOwnershipAtItsBounds",
            "HorizontalWheelMovesHostedContentInTheRequestedDirection",
        )]
        fixtures = {
            "components-tests.xml": [("LuaScriptsContainerTest", "RealTimersRemainLiveDuringIdleAndPausedSimulation"),
                                     ("LuaScriptsContainerTest", "RealTimersRestoreRelativeDeadlinesAndRunWhilePaused")],
            "openmw-tests.xml": [("LuaUiLayoutTest", "Rc1CursorsAndLegacyPointersPreservePrecedenceAndInheritance"),
                                 ("ModKeybindings", "RuntimeDiscoveryWritebackConflictSemanticsAndCaptureLifecycle"),
                                 *interactions],
            "lua-ui-legacy-tests.xml": interactions,
            "lua-ui-legacy-updates-tests.xml": interactions,
            "combined-performance-off-tests.xml": interactions,
        }
        for name, cases in fixtures.items():
            content = '<testsuites>' + ''.join(f'<testcase classname="{suite}" name="{case}" status="run" result="completed"/>' for suite, case in cases) + '</testsuites>'
            (self.root / name).write_text(content)
        package.require_rc1_cases(self.root)
        for name in fixtures:
            original = (self.root / name).read_text()
            for replacement in ("<testsuites/>", original.replace('status="run"', 'status="notrun"'),
                                original.replace('/>', '><failure/></testcase>', 1)):
                with self.subTest(name=name, replacement=replacement):
                    (self.root / name).write_text(replacement)
                    with self.assertRaises(ValueError):
                        package.require_rc1_cases(self.root)
                    (self.root / name).write_text(original)
            # Dropping any one required case must fail even when the rest pass.
            for suite, case in fixtures[name]:
                with self.subTest(name=name, missing=case):
                    fragment = f'<testcase classname="{suite}" name="{case}" status="run" result="completed"/>'
                    (self.root / name).write_text(original.replace(fragment, ''))
                    with self.assertRaises(ValueError):
                        package.require_rc1_cases(self.root)
                    (self.root / name).write_text(original)


if __name__ == "__main__":
    unittest.main()
