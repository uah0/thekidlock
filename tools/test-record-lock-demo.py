"""Safety checks for ADB demo selector logic, without contacting a lock."""
import importlib.util
from pathlib import Path
from types import SimpleNamespace
import tempfile
import unittest
from unittest.mock import patch
import xml.etree.ElementTree as ET

spec = importlib.util.spec_from_file_location("demo", Path(__file__).with_name("record-lock-demo.py"))
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class DemoSafetyTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.demo = module.Demo(SimpleNamespace(output=str(Path(self.temp.name) / "run"), adb="adb", serial="host:33719"))

    def nodes(self, *attributes):
        nodes = [ET.Element("node", {"package": "com.blelock.app", "enabled": "true", **a}) for a in attributes]
        self.demo.dump = lambda: nodes

    def test_duplicate_text_requires_class(self):
        self.nodes({"text": "Автооткрытие замка", "class": "android.widget.Button"},
                   {"content-desc": "Автооткрытие замка", "class": "android.widget.Switch"})
        self.assertEqual(self.demo.find("Автооткрытие", kind="android.widget.Switch").get("class"), "android.widget.Switch")
        with self.assertRaisesRegex(RuntimeError, "Неоднозначный"):
            self.demo.find("Автооткрытие")

    def test_disabled_button_never_tapped(self):
        self.nodes({"text": "Настройки замка", "enabled": "false"})
        with self.assertRaisesRegex(RuntimeError, "не найден"):
            self.demo.find("Настройки", timeout=0)

    def test_content_description_matches_start_of_string(self):
        self.nodes({"text": "", "content-desc": "Настройки приложения"})
        self.assertEqual(self.demo.find("^Настройки приложения").get("content-desc"), "Настройки приложения")

    def test_other_app_never_tapped(self):
        self.nodes({"text": "Настройки замка", "package": "other.app"})
        with self.assertRaisesRegex(RuntimeError, "не найден"):
            self.demo.find("Настройки", timeout=0)

    def test_invisible_bounds_rejected(self):
        with self.assertRaisesRegex(RuntimeError, "не виден"):
            self.demo.bounds(ET.Element("node", {"bounds": "[0,0][0,0]"}))

    def test_back_tap_avoids_status_bar_overlap(self):
        self.nodes({"content-desc": "Назад", "class": "android.widget.TextView", "clickable": "true", "bounds": "[45,33][180,168]"})
        calls = []
        self.demo.adb = lambda *args: calls.append(args)
        self.demo.tap("^Назад$", "android.widget.TextView")
        self.assertEqual(calls[0][:3], ("shell", "input", "tap"))
        self.assertGreater(int(calls[0][-1]), 128)

    def test_auto_mode_already_correct_no_command(self):
        self.nodes({"text": "Автооткрытие замка", "class": "android.widget.Switch", "checked": "false"})
        self.demo.tap = lambda *args: self.fail("Unexpected command")
        self.demo.auto_mode(False)

    def test_tap_not_repeated_after_connection_failure(self):
        failed = SimpleNamespace(returncode=1, stderr=b"device offline")
        connected = SimpleNamespace(returncode=0, stdout=b"", stderr=b"")
        with patch.object(module.subprocess, "run", side_effect=[failed, connected, connected]) as run:
            with self.assertRaisesRegex(RuntimeError, "offline"):
                self.demo.adb("shell", "input", "tap", "10", "20")
            self.assertEqual(run.call_count, 3)

    def test_orientation_read_retried_after_reconnect(self):
        failed = SimpleNamespace(returncode=1, stderr=b"device offline")
        connected = SimpleNamespace(returncode=0, stdout=b"", stderr=b"")
        read = SimpleNamespace(returncode=0, stdout=b"0\n", stderr=b"")
        with patch.object(module.subprocess, "run", side_effect=[failed, connected, connected, read]):
            self.assertEqual(self.demo.adb("shell", "settings", "get", "system", "user_rotation"), "0")


if __name__ == "__main__":
    unittest.main()
