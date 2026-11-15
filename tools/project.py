#!/usr/bin/env python3
"""One-command build, test and portable-folder packaging. No Python packages needed."""
from __future__ import annotations

import argparse
from dataclasses import dataclass
import json
import os
from pathlib import Path
import platform as system_platform
import shlex
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from deploy import deploy_package, qt_locations


def native_platform():
    if sys.platform == "win32":
        return "windows"
    if sys.platform.startswith("linux"):
        return "linux"
    raise ValueError("The build host must be Windows or Linux (WSL is supported).")


def normalize_platform(value):
    value = value.strip().lower().removesuffix("-x64")
    if value == "auto":
        value = native_platform()
    if value not in ("windows", "linux", "xj380"):
        raise ValueError(f"Unknown platform {value!r}; choose windows, linux or xj380.")
    return value


def local_path(value, *, resolve=True):
    value = str(value)
    if os.name != "nt" and len(value) > 2 and value[1] == ":" and value[2] in "/\\":
        value = subprocess.check_output(["wslpath", "-u", value], text=True).strip()
    path = Path(value).expanduser()
    return path.resolve() if resolve else path.absolute()


def env_value(env, name, fallback):
    return env.get(name, "").strip() or fallback


@dataclass
class Settings:
    platform: str
    frontend: str
    runtimes: tuple[str, ...]
    modes: tuple[str, ...]
    configuration: str
    jobs: int
    build_dir: Path
    package_dir: Path
    cmake: str
    qt_prefix: Path | None
    toolchain: Path | None
    adapter: Path | None
    stub_dir: Path | None
    extra_lib_dirs: tuple[Path, ...]

    @classmethod
    def from_env(cls, env):
        selected = env_value(env, "AP_PLATFORM", "auto")
        backend = normalize_platform(env_value(env, "AP_BACKEND_PLATFORM", selected))
        frontend_platform = normalize_platform(env_value(env, "AP_FRONTEND_PLATFORM", backend))
        if backend != frontend_platform:
            raise ValueError("The workbench starts its local apack backend: AP_FRONTEND_PLATFORM and "
                             "AP_BACKEND_PLATFORM must match. Build separate platform folders instead.")
        if backend == "xj380":
            raise ValueError("The packaging workbench/apack run on Windows or Linux. For native XJ380 "
                             "installers set AP_RUNTIME_PLATFORMS=xj380, leaving AP_PLATFORM as the host.")
        frontend = env_value(env, "AP_FRONTEND", "auto").lower()
        if frontend not in ("auto", "gui", "tui"):
            raise ValueError("AP_FRONTEND accepts auto, gui or tui.")
        raw = env_value(env, "AP_RUNTIME_PLATFORMS", backend).lower()
        targets = ("windows", "linux", "xj380") if raw == "all" else tuple(
            normalize_platform(p) for p in raw.replace(";", ",").split(",") if p.strip())
        if not targets:
            raise ValueError("AP_RUNTIME_PLATFORMS needs at least one installer target.")
        targets = tuple(dict.fromkeys(targets))
        ui = env_value(env, "AP_RUNTIME_UI", "both").lower()
        if ui not in ("both", "gui", "tui"):
            raise ValueError("AP_RUNTIME_UI accepts both, gui or tui.")
        configuration = env_value(env, "AP_BUILD_TYPE", "Release")
        if configuration not in ("Release", "Debug", "MinSizeRel", "RelWithDebInfo"):
            raise ValueError("AP_BUILD_TYPE accepts Release, Debug, MinSizeRel or RelWithDebInfo.")
        try:
            jobs = int(env_value(env, "AP_JOBS", str(min(os.cpu_count() or 2, 4))))
        except ValueError:
            raise ValueError("AP_JOBS must be a positive integer.") from None
        if jobs < 1:
            raise ValueError("AP_JOBS must be a positive integer.")
        def optional(name):
            return local_path(env[name]) if env.get(name, "").strip() else None
        return cls(backend, frontend, targets, ("gui", "tui") if ui == "both" else (ui,),
                   configuration, jobs,
                   local_path(env_value(env, "AP_BUILD_DIR", str(ROOT / "build/oneclick" / f"{backend}-{configuration.lower()}"))),
                   local_path(env_value(env, "AP_PACKAGE_DIR", str(ROOT / "dist" / f"accretion-package-{backend}-x64")), resolve=False),
                   env_value(env, "AP_CMAKE", "cmake"), optional("AP_QT_PREFIX"),
                   optional("AP_TOOLCHAIN_FILE"), optional("AP_XJ380_ADAPTER_SOURCE"), optional("AP_STUB_DIR"),
                   tuple(local_path(p) for p in env.get("AP_EXTRA_LIB_DIRS", "").split(os.pathsep) if p))


class Builder:
    def __init__(self, settings):
        self.settings = settings
        requested = settings.cmake
        self.cmake = shutil.which(requested) or (str(local_path(requested)) if Path(requested).is_file() else None)
        if not self.cmake:
            raise ValueError("Missing CMake >= 3.20. Install CMake or set AP_CMAKE to its executable path.")
        self.windows_tool = os.name == "nt" or self.cmake.lower().endswith(".exe")
        self.cmake_host = "windows" if self.windows_tool else "linux"
        if settings.platform == "linux" and self.cmake_host != "linux":
            raise ValueError("Use Linux CMake inside Linux/WSL to build Linux tools; AP_CMAKE currently selects Windows CMake.")

    def tool_path(self, value):
        path = local_path(value)
        if self.windows_tool and os.name != "nt":
            return subprocess.check_output(["wslpath", "-w", str(path)], text=True).strip().replace("\\", "/")
        return path.as_posix()

    def run(self, args, *, env=None):
        command = [str(x) for x in args]
        print("+ " + (subprocess.list2cmdline(command) if os.name == "nt" else shlex.join(command)), flush=True)
        subprocess.run(command, cwd=ROOT, check=True, env=env)

    def check(self):
        self.run([self.cmake, "--version"])
        print(f"Host tools: {self.settings.platform}-x64; frontend: {self.settings.frontend}; "
              f"installer targets: {', '.join(self.settings.runtimes)}")
        for name, path in (("Qt SDK", self.settings.qt_prefix), ("toolchain", self.settings.toolchain),
                           ("XJ380 adapter", self.settings.adapter), ("prebuilt templates", self.settings.stub_dir)):
            if path and not path.exists():
                raise ValueError(f"Missing {name}: {path}")
        if self.settings.frontend == "gui":
            print("GUI requires Qt 6 Widgets development files; AP_QT_PREFIX can select the SDK.")

    def build(self, tests=False):
        s = self.settings
        self.check()
        native_modes = ("gui", "tui") if tests else s.modes
        configure = [self.cmake, "-S", self.tool_path(ROOT), "-B", self.tool_path(s.build_dir),
                     f"-DCMAKE_BUILD_TYPE={s.configuration}", "-DAP_BUILD_PACKAGER=ON",
                     f"-DAP_BUILD_WORKBENCH={'OFF' if s.frontend == 'tui' else 'ON'}",
                     f"-DAP_REQUIRE_WORKBENCH={'ON' if s.frontend == 'gui' else 'OFF'}",
                     f"-DAP_BUILD_TESTS={'ON' if tests else 'OFF'}",
                     f"-DAP_BUILD_RUNTIMES={'ON' if s.platform in s.runtimes or tests else 'OFF'}",
                     "-DAP_INSTALLER_MODES=" + ";".join(native_modes)]
        if s.qt_prefix:
            # CMake otherwise keeps Qt6*_DIR from an older SDK ahead of the new prefix.
            configure.append("-UQt6*_DIR")
            configure.append("-DCMAKE_PREFIX_PATH=" + self.tool_path(s.qt_prefix))
        toolchain = s.toolchain
        if s.platform != self.cmake_host and not toolchain:
            toolchain = ROOT / "cmake/mingw-toolchain.cmake"
        if toolchain:
            configure.append("-DCMAKE_TOOLCHAIN_FILE=" + self.tool_path(toolchain))
        if s.adapter:
            configure.append("-DAP_XJ380_ADAPTER_SOURCE=" + self.tool_path(s.adapter))
        if not (s.build_dir / "CMakeCache.txt").exists() and self.cmake_host == "linux" and shutil.which("ninja"):
            configure.extend(["-G", "Ninja"])
        self.run(configure)
        self.run([self.cmake, "--build", self.tool_path(s.build_dir), "--config", s.configuration,
                  "--parallel", s.jobs])
        for target in s.runtimes:
            if target == s.platform:
                continue
            if s.stub_dir:
                for mode in s.modes:
                    if not (s.stub_dir / stub_name(target, mode)).is_file():
                        raise ValueError(f"Missing prebuilt template: {s.stub_dir / stub_name(target, mode)}")
                continue
            if target == "linux" and native_platform() != "linux":
                raise ValueError("Build Linux templates in Linux/WSL, or set AP_STUB_DIR to prebuilt templates.")
            cross_dir = s.build_dir / "cross" / target
            command = [sys.executable, ROOT / "tools/build.py", "--target", target, "--runtime-only",
                       "--ui", "both" if len(s.modes) == 2 else s.modes[0], "--out", cross_dir, "--jobs", s.jobs,
                       "--configuration", s.configuration]
            if s.adapter and target == "xj380":
                command.extend(["--xj-adapter", s.adapter])
            self.run(command)
        info = self.info()
        if s.frontend == "auto" and info["frontend"] != "gui":
            print("Qt 6 not detected: the frontend uses the built-in TUI wizard. "
                  "Set AP_FRONTEND=gui and AP_QT_PREFIX to require a graphical build.")
        print(f"Build ready: {s.build_dir}")

    def info(self):
        result = json.loads((self.settings.build_dir / f"ap-build-info-{self.settings.configuration}.json").read_text(encoding="utf-8"))
        if result["platform"] != self.settings.platform:
            raise ValueError("Build directory platform differs from AP_PLATFORM; choose another AP_BUILD_DIR.")
        return result

    def package(self):
        self.build()
        s = self.settings
        destination = s.package_dir
        if (destination == ROOT or destination in ROOT.parents or destination == s.build_dir or
                destination in s.build_dir.parents or s.build_dir in destination.parents):
            raise ValueError("AP_PACKAGE_DIR must be a dedicated distribution folder, outside the build directory.")
        if destination.is_symlink():
            raise ValueError("AP_PACKAGE_DIR must not be a symbolic link.")
        if destination.exists():
            marker = destination / "package-manifest.json"
            try:
                owned = json.loads(marker.read_text(encoding="utf-8")).get("format") == "accretion-portable-v1"
            except (OSError, ValueError):
                owned = False
            if not owned:
                raise ValueError(f"Refusing to replace a folder without an Accretion package marker: {destination}")
        destination.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix=".accretion-package-", dir=destination.parent) as temporary:
            staging = Path(temporary) / "package"
            self.run([self.cmake, "--install", self.tool_path(s.build_dir), "--config", s.configuration,
                      "--prefix", self.tool_path(staging)])
            stubs = staging / "bin/ap-stubs"
            stubs.mkdir(parents=True, exist_ok=True)
            expected = {stub_name(target, mode) for target in s.runtimes for mode in s.modes}
            for existing in stubs.iterdir():
                if existing.is_file() and existing.name not in expected:
                    existing.unlink()
            for target in s.runtimes:
                if target == s.platform:
                    continue
                source = s.stub_dir or s.build_dir / "cross" / target / "ap-stubs"
                for mode in s.modes:
                    shutil.copy2(source / stub_name(target, mode), stubs)
            for filename in expected:
                if not (stubs / filename).is_file():
                    raise ValueError(f"Missing selected installer template: {filename}")
            deploy_package(staging, s, self.info(), self)
            # A failed build/deployment leaves the previous distribution intact.
            previous = Path(temporary) / "previous"
            if destination.exists():
                destination.rename(previous)
            try:
                staging.rename(destination)
            except OSError:
                if previous.exists():
                    previous.rename(destination)
                raise
        print(f"Package ready: {destination}")
        print("Run check-deps, start-frontend, or apack from that folder; no source/build folder is needed.")

    def test(self):
        self.build(tests=True)
        ctest = Path(self.cmake).with_name("ctest.exe" if self.windows_tool else "ctest")
        if not ctest.is_file():
            raise ValueError("CTest is missing from the selected CMake installation.")
        env = dict(os.environ)
        info = self.info()
        if info["frontend"] == "gui":
            core, _, plugins, _ = qt_locations(info, self.settings)
            env["QT_PLUGIN_PATH"] = self.tool_path(plugins)
            env["QT_QPA_PLATFORM_PLUGIN_PATH"] = self.tool_path(plugins / "platforms")
            if not self.windows_tool:
                env["LD_LIBRARY_PATH"] = str(core.parent) + os.pathsep + env.get("LD_LIBRARY_PATH", "")
        self.run([ctest, "--test-dir", self.tool_path(self.settings.build_dir), "-C", self.settings.configuration,
                  "--output-on-failure"], env=env)


def stub_name(target, mode):
    suffix = ".exe" if target == "windows" else ".elf" if target == "xj380" else ""
    return f"ap-setup-{target}-x64-{mode}{suffix}"


def main():
    parser = argparse.ArgumentParser(description=__doc__, epilog="Environment variables and examples: docs/building.md")
    parser.add_argument("action", choices=("build", "package", "test", "check"))
    for flag in ("platform", "frontend-platform", "backend-platform", "frontend", "runtime-platforms", "runtime-ui",
                 "build-type", "jobs", "build-dir", "package-dir", "qt-prefix", "cmake", "stub-dir"):
        parser.add_argument("--" + flag)
    args = parser.parse_args()
    env = dict(os.environ)
    for key, value in vars(args).items():
        if key != "action" and value is not None:
            env["AP_" + key.upper()] = value
    try:
        settings = Settings.from_env(env)
        if system_platform.machine().lower() not in ("x86_64", "amd64") and not settings.toolchain:
            raise ValueError("Current binaries are x64; an explicit AP_TOOLCHAIN_FILE is needed on other architectures.")
        builder = Builder(settings)
        getattr(builder, args.action)()
        return 0
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        print(f"Accretion: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
