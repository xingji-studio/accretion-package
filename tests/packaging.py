#!/usr/bin/env python3
"""Black-box checks of a relocated release, without source/build/Qt SDK paths."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--package-dir", type=Path, required=True)
args, unittest_args = parser.parse_known_args()
SOURCE = args.package_dir.resolve()
INFO = json.loads((SOURCE / "package-manifest.json").read_text(encoding="utf-8"))
if (INFO["platform"] == "windows") != (os.name == "nt"):
    raise SystemExit("Run these checks using Python native to the package platform (Windows Python for Windows).")


class PortablePackageTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix="accretion-portable-")
        cls.root = Path(cls.temporary.name)
        cls.package = cls.root / "独立发布 folder"
        shutil.copytree(SOURCE, cls.package)
        cls.cwd = cls.root / "unrelated working directory"
        cls.cwd.mkdir()
        cls.env = dict(os.environ)
        for key in ("LD_LIBRARY_PATH", "QT_PLUGIN_PATH", "QT_QPA_PLATFORM_PLUGIN_PATH", "QT_QML_IMPORT_PATH",
                    "QML2_IMPORT_PATH", "CMAKE_PREFIX_PATH", "AP_QT_PREFIX"):
            cls.env.pop(key, None)
        cls.env.update(QT_QPA_PLATFORM="offscreen", XDG_CACHE_HOME=str(cls.root / "cache"),
                       XDG_CONFIG_HOME=str(cls.root / "config"))
        cls.backend = cls.package / "bin" / ("apack.exe" if os.name == "nt" else "apack")

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def run_program(self, path, arguments=(), *, input=None, expected=0, env=None):
        command = [str(path), *map(str, arguments)]
        if os.name == "nt" and path.suffix.lower() == ".cmd":
            command = subprocess.list2cmdline([os.environ.get("COMSPEC", "cmd.exe")]) + " /d /s /c \"" + subprocess.list2cmdline(command) + "\""
        result = subprocess.run(command, input=input, cwd=self.cwd, env=env or self.env,
                                capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=90)
        if expected is not None:
            self.assertEqual(result.returncode, expected, result.stdout + result.stderr)
        return result

    def checker(self, backend=False, expected=0):
        if os.name == "nt":
            return self.run_program(self.package / "check-deps.cmd", ["-Mode", "backend"] if backend else [], expected=expected)
        return self.run_program(self.package / "check-deps.sh", ["--backend"] if backend else [], expected=expected)

    def test_manifest_and_runtime_check(self):
        for item in INFO["files"]:
            path = self.package / item["path"]
            self.assertTrue(path.is_file(), item["path"])
            self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(), item["sha256"], item["path"])
        self.checker()
        self.checker(backend=True)

    def test_direct_backend_and_wrapper(self):
        self.assertIn("Accretion", self.run_program(self.backend, ["--help"]).stdout)
        wrapper = self.package / ("apack.cmd" if os.name == "nt" else "apack")
        self.assertIn("Accretion", self.run_program(wrapper, ["--help"]).stdout)

    def test_frontend(self):
        launcher = self.package / ("start-frontend.cmd" if os.name == "nt" else "start-frontend.sh")
        if INFO["frontend"] == "gui":
            self.run_program(launcher, ["--smoke-test"])
            binary = self.package / "bin" / ("apack-workbench.exe" if os.name == "nt" else "apack-workbench")
            env = dict(self.env)
            if os.name != "nt":
                env["LD_DEBUG"] = "libs"
            result = self.run_program(binary, ["--smoke-test"], env=env)
            if os.name != "nt":
                loaded = [line for line in result.stderr.splitlines() if "calling init:" in line and "libQt6" in line]
                self.assertTrue(loaded, result.stderr)
                self.assertTrue(all(str(self.package) in line for line in loaded), "Qt must load from the relocated package:\n" + "\n".join(loaded))
        else:
            output = self.root / "向导 project.json"
            source = self.root / "wizard-input"
            source.mkdir()
            shutil.copy2(self.backend, source / self.backend.name)
            values = ["Portable demo", "test.accretion.portable", str(source), self.backend.name, INFO["platform"] + "-x64"]
            self.run_program(launcher, ["--output", output], input="\n".join(values) + "\n")
            self.assertEqual(json.loads(output.read_text(encoding="utf-8"))["app"]["id"], "test.accretion.portable")

    def test_missing_dependency_reports_name(self):
        candidate = next((self.package / f["path"] for f in INFO["files"] if "qt6core" in Path(f["path"]).name.lower()), self.backend)
        saved = candidate.with_name(candidate.name + ".saved")
        candidate.rename(saved)
        try:
            result = self.checker(expected=1)
            self.assertIn(candidate.name, result.stdout + result.stderr)
            if candidate != self.backend:
                self.checker(backend=True)
        finally:
            saved.rename(candidate)

    def test_build_using_only_bundled_templates(self):
        for target in INFO["runtimePlatforms"]:
            with self.subTest(platform=target):
                payload = self.root / ("payload-" + target)
                payload.mkdir()
                mode = "tui" if "tui" in INFO["runtimeModes"] else "gui"
                suffix = ".exe" if target == "windows" else ".elf" if target == "xj380" else ""
                entry = "entry" + suffix
                stub = self.package / "bin/ap-stubs" / ("ap-setup-" + target + "-x64-" + mode + suffix)
                shutil.copy2(stub, payload / entry)
                project = self.root / ("project-" + target + ".json")
                self.run_program(self.backend, ["init", "--input", payload, "--main", entry, "--target", target + "-x64", "--id", "test.accretion.portable", "--output", project])
                setup = self.root / ("installer-" + target + (suffix or ".run"))
                self.run_program(self.backend, ["build", "--project", project, "--target", target + "-x64", "--ui", mode, "--output", setup])
                inspected = json.loads(self.run_program(self.backend, ["inspect", "--package", setup]).stdout)
                self.assertEqual(inspected["target"], target + "-x64")
                if target == "xj380":
                    epf = setup.with_suffix(".epf")
                    self.run_program(self.backend, ["build", "--project", project, "--target", "xj380-x64", "--ui", mode, "--format", "epf", "--output", epf])
                    self.assertEqual(epf.read_bytes()[:4], b"$EPF")
                    self.run_program(self.backend, ["inspect", "--package", epf])
                if target == INFO["platform"]:
                    destination = self.root / "not-installed"
                    plan = json.loads(self.run_program(setup, ["--dry-run", "--dir", destination]).stdout)
                    self.assertEqual(Path(plan["directory"]), destination)
                    self.assertFalse(destination.exists())


if __name__ == "__main__":
    unittest.main(argv=[__file__, *unittest_args], verbosity=2)
