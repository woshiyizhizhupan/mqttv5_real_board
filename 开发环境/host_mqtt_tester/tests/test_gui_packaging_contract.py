from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class GuiPackagingContractTests(unittest.TestCase):
    def test_gui_launcher_prefers_bundled_exe_before_python_fallback(self):
        launcher = (ROOT / "run_gui.ps1").read_text(encoding="utf-8")

        self.assertIn("mqtt_host_gui.exe", launcher)
        self.assertIn("Exit $LASTEXITCODE", launcher)
        self.assertLess(launcher.index("mqtt_host_gui.exe"), launcher.index("python -m venv"))

    def test_cli_packaging_entrypoints_call_real_module_mains(self):
        expected = {
            "board_command_probe_cli.py": "mqtt_tester.board_command_probe",
            "wait_topic_probe_cli.py": "mqtt_tester.wait_topic_probe",
            "sim_board_server_cli.py": "mqtt_tester.sim_board_server",
            "mqtt_loopback_cli.py": "mqtt_tester.mqtt_loopback",
        }

        entrypoint_dir = ROOT / "packaging_entrypoints"
        for filename, module in expected.items():
            text = (entrypoint_dir / filename).read_text(encoding="utf-8")
            self.assertIn(f"from {module} import main", text)
            self.assertIn("raise SystemExit(main())", text)


if __name__ == "__main__":
    unittest.main()
