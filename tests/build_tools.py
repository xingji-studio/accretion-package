#!/usr/bin/env python3
"""Validate build selections before starting any compiler."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from project import Settings


class ConfigurationTests(unittest.TestCase):
    def test_separate_host_and_installer_targets(self):
        config = Settings.from_env({"AP_PLATFORM": "linux-x64", "AP_FRONTEND": "gui", "AP_RUNTIME_PLATFORMS": "all"})
        self.assertEqual(config.platform, "linux")
        self.assertEqual(set(config.runtimes), {"windows", "linux", "xj380"})
        self.assertEqual(config.modes, ("gui", "tui"))

    def test_mismatched_local_frontend_and_backend_rejected(self):
        with self.assertRaisesRegex(ValueError, "must match"):
            Settings.from_env({"AP_FRONTEND_PLATFORM": "windows", "AP_BACKEND_PLATFORM": "linux"})

    def test_xj380_host_has_actionable_target_hint(self):
        with self.assertRaisesRegex(ValueError, "AP_RUNTIME_PLATFORMS=xj380"):
            Settings.from_env({"AP_PLATFORM": "xj380"})

    def test_invalid_selections_fail_before_build(self):
        for env in ({"AP_JOBS": "0"}, {"AP_RUNTIME_UI": "typo"}, {"AP_FRONTEND": "typo"}, {"AP_BUILD_TYPE": "typo"}):
            with self.subTest(env=env), self.assertRaises(ValueError):
                Settings.from_env({"AP_PLATFORM": "linux", **env})


if __name__ == "__main__":
    unittest.main(verbosity=2)
