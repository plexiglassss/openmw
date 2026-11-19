"""Exercise the real distribution check against missing entry/helper resources."""
from pathlib import Path
import importlib.util
import shutil
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("distribution", ROOT / "custom/combined-performance/complete-windows-distribution.py")
distribution = importlib.util.module_from_spec(spec)
spec.loader.exec_module(distribution)


class BuiltinLuaDistribution(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.install = Path(self.temp.name)
        for folder, directory in (("data", "vfs"), ("data-mw", "vfs-mw")):
            source = ROOT / "files" / folder
            for file in (source / "scripts").rglob("*.lua"):
                target = self.install / "resources" / directory / file.relative_to(source)
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(file, target)
        base = (ROOT / "files/data/builtin.omwscripts").read_text()
        mw = (ROOT / "files/data-mw/builtin.omwscripts.in").read_text().replace("@BUILTIN_SCRIPTS@", base)
        for directory in ("vfs", "vfs-mw"):
            target = self.install / "resources" / directory / "builtin.omwscripts"
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(base if directory == "vfs" else mw)

    def test_complete_scripts_and_descriptors(self):
        result = distribution.confirm_builtin_lua(self.install)
        self.assertIn("vfs/scripts/omw/input/modbindings.lua", result["source_scripts"])
        self.assertIn("vfs/scripts/omw/input/modbindingmodel.lua", result["source_scripts"])
        self.assertEqual(set(result["descriptor_references"]), {"vfs", "vfs-mw"})

    def test_missing_entry(self):
        (self.install / "resources/vfs/scripts/omw/input/modbindings.lua").unlink()
        with self.assertRaisesRegex(ValueError, "Missing built-in Lua file"):
            distribution.confirm_builtin_lua(self.install)

    def test_missing_required_helper(self):
        (self.install / "resources/vfs/scripts/omw/input/modbindingmodel.lua").unlink()
        with self.assertRaisesRegex(ValueError, "Missing built-in Lua file"):
            distribution.confirm_builtin_lua(self.install)

    def test_changed_helper(self):
        target = self.install / "resources/vfs/scripts/omw/input/modbindingmodel.lua"
        target.write_bytes(target.read_bytes() + b"\nreturn {}\n")
        with self.assertRaisesRegex(ValueError, "differs from the source"):
            distribution.confirm_builtin_lua(self.install)

    def test_missing_descriptor(self):
        (self.install / "resources/vfs-mw/builtin.omwscripts").unlink()
        with self.assertRaisesRegex(ValueError, "Missing built-in script descriptor"):
            distribution.confirm_builtin_lua(self.install)

    def test_stale_descriptor_cannot_silently_disable_the_new_page(self):
        for directory in ("vfs", "vfs-mw"):
            target = self.install / "resources" / directory / "builtin.omwscripts"
            original = target.read_bytes()
            target.write_bytes(original.replace(b"MENU: scripts/omw/input/modbindings.lua", b"# removed"))
            with self.assertRaisesRegex(ValueError, "descriptor differs from the source"):
                distribution.confirm_builtin_lua(self.install)
            target.write_bytes(original)

    def test_dangling_descriptor_reference(self):
        target = self.install / "resources/vfs-mw/builtin.omwscripts"
        target.write_bytes(target.read_bytes() + b"\nMENU: scripts/omw/nonexistent.lua\n")
        with self.assertRaisesRegex(ValueError, "Missing referenced built-in script"):
            distribution.confirm_builtin_lua(self.install)

    def test_unsafe_descriptor_reference(self):
        target = self.install / "resources/vfs/builtin.omwscripts"
        target.write_bytes(target.read_bytes() + b"\nMENU: ../escape.lua\n")
        with self.assertRaisesRegex(ValueError, "Unsafe built-in script path"):
            distribution.confirm_builtin_lua(self.install)


if __name__ == "__main__":
    unittest.main()
