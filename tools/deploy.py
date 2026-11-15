"""Collect runtime files and create a relocatable release folder."""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[1]
BASE_LINUX = re.compile(r"^(ld-linux.*|linux-vdso.*|lib(c|m|dl|pthread|rt|resolv|util|anl)\.so\..*)$")
WINDOWS_SYSTEM = set(("kernel32 user32 gdi32 advapi32 shell32 ole32 oleaut32 comctl32 comdlg32 "
                      "shlwapi shcore msvcrt ucrtbase ntdll version rpcrt4 ws2_32 wsock32 winmm "
                      "imm32 setupapi dwmapi uxtheme dwrite d3d11 d3d12 dxgi dxguid opengl32 "
                      "glu32 secur32 crypt32 bcrypt bcryptprimitives ncrypt wintrust iphlpapi "
                      "netapi32 userenv wtsapi32 powrprof pdh mpr authz dnsapi normaliz "
                      "winhttp wininet winspool wldap32 usp10 msimg32 avrt cfgmgr32 dbghelp").split())


def digest(path):
    result = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            result.update(block)
    return result.hexdigest()


def native_path(value):
    if os.name != "nt" and re.match(r"^[A-Za-z]:[/\\]", str(value)):
        value = subprocess.check_output(["wslpath", "-u", str(value)], text=True).strip()
    return Path(value).resolve()


def is_dynamic_elf(path):
    with path.open("rb") as source:
        header = source.read(64)
        if len(header) < 64 or header[:6] != b"\x7fELF\x02\x01":
            return False
        offset = struct.unpack_from("<Q", header, 32)[0]
        stride, count = struct.unpack_from("<HH", header, 54)
        if stride < 4 or count > 4096:
            raise ValueError(f"Invalid ELF program headers: {path}")
        source.seek(offset)
        entries = source.read(stride * count)
        if len(entries) != stride * count:
            raise ValueError(f"Truncated ELF program headers: {path}")
        return any(struct.unpack_from("<I", entries, i * stride)[0] in (2, 3) for i in range(count))


def pe_imports(path):
    """Read import names without executing the PE, including on a cross-build host."""
    data = path.read_bytes()
    if data[:2] != b"MZ":
        return []
    def unpack(fmt, offset):
        if offset < 0 or offset + struct.calcsize(fmt) > len(data):
            raise ValueError(f"Truncated PE: {path}")
        return struct.unpack_from(fmt, data, offset)
    pe = unpack("<I", 60)[0]
    if data[pe:pe + 4] != b"PE\0\0":
        raise ValueError(f"Invalid PE header: {path}")
    count = unpack("<H", pe + 6)[0]
    optional_size = unpack("<H", pe + 20)[0]
    optional = pe + 24
    magic = unpack("<H", optional)[0]
    directory = optional + (112 if magic == 0x20B else 96 if magic == 0x10B else 0)
    if directory == optional or directory + 16 > optional + optional_size:
        raise ValueError(f"Unsupported PE optional header: {path}")
    sections = optional + optional_size
    def mapped(rva):
        for i in range(count):
            virtual_size, address, raw_size, raw = unpack("<IIII", sections + i * 40 + 8)
            if address <= rva < address + max(virtual_size, raw_size):
                relative = rva - address
                if relative >= raw_size:
                    break
                return raw + relative
        raise ValueError(f"Unmapped PE import RVA: {path}")
    rva, size = unpack("<II", directory + 8)
    if not rva:
        return []
    offset = mapped(rva)
    names = []
    for i in range(min(size // 20 + 1, 4096)):
        entry = unpack("<IIIII", offset + i * 20)
        if not any(entry):
            return names
        name_offset = mapped(entry[3])
        end = data.find(b"\0", name_offset, name_offset + 512)
        if end < 0:
            raise ValueError(f"Invalid DLL import name: {path}")
        name = data[name_offset:end].decode("ascii")
        if "/" in name or "\\" in name or name in ("", ".", ".."):
            raise ValueError(f"Unsafe DLL import name: {path}")
        names.append(name)
    raise ValueError(f"Unterminated PE import table: {path}")


def qt_locations(info, settings):
    core = native_path(info["qt_core"]) if info.get("qt_core") else None
    qmake = native_path(info["qmake"]) if info.get("qmake") and not info["qmake"].endswith("NOTFOUND") else None
    prefixes = [p for p in (settings.qt_prefix, core.parent.parent if core else None,
                           qmake.parent.parent if qmake else None) if p]
    candidates = []
    if qmake and qmake.is_file():
        env = dict(os.environ)
        if core and os.name != "nt":
            env["LD_LIBRARY_PATH"] = str(core.parent) + os.pathsep + env.get("LD_LIBRARY_PATH", "")
        result = subprocess.run([str(qmake), "-query", "QT_INSTALL_PLUGINS"], env=env,
                                capture_output=True, text=True, errors="replace")
        if result.returncode == 0 and result.stdout.strip():
            candidates.append(native_path(result.stdout.strip()))
    for prefix in prefixes:
        candidates.extend([prefix / "plugins", prefix / "qt6/plugins", prefix / "lib/qt6/plugins",
                           prefix / "lib/x86_64-linux-gnu/qt6/plugins"])
    if core:
        candidates.append(core.parent / "qt6/plugins")
    plugins = next((p for p in candidates if (p / "platforms").is_dir()), None)
    if not plugins:
        raise ValueError("Qt platform plugins were not found. Set AP_QT_PREFIX to a complete Qt SDK.")
    return core, qmake, plugins, prefixes


def ldd_dependencies(path, search_dirs):
    env = dict(os.environ)
    env["LD_LIBRARY_PATH"] = os.pathsep.join(str(p) for p in search_dirs) + os.pathsep + env.get("LD_LIBRARY_PATH", "")
    result = subprocess.run(["ldd", str(path)], capture_output=True, text=True, env=env)
    output = result.stdout + result.stderr
    missing = re.findall(r"^\s*(\S+)\s+=>\s+not found\s*$", output, re.MULTILINE)
    if "not found" in output and not missing:
        raise ValueError(f"Incompatible runtime libraries for {path}:\n{output.strip()}")
    dependencies = []
    for line in output.splitlines():
        match = re.match(r"\s*(\S+)\s+=>\s+(/.*?)\s+\(0x[0-9a-fA-F]+\)", line)
        if match:
            dependencies.append((match[1], Path(match[2])))
    if result.returncode and not missing:
        raise ValueError(f"Cannot inspect runtime dependencies for {path}:\n{output.strip()}")
    return dependencies, missing


def collect_linux(staging, settings, info):
    if not shutil.which("ldd"):
        raise ValueError("Packaging Linux binaries requires ldd on a Linux build host.")
    library_dir = staging / "lib"
    library_dir.mkdir()
    search = [library_dir, *settings.extra_lib_dirs]
    optional = []
    excluded = set()
    plugins = []
    if info["frontend"] == "gui":
        core, _, source_plugins, _ = qt_locations(info, settings)
        search.append(core.parent)
        groups = ("platforms", "imageformats", "iconengines", "styles", "platformthemes",
                  "xcbglintegrations", "wayland-decoration-client", "wayland-graphics-integration-client",
                  "wayland-shell-integration")
        for group in groups:
            source = source_plugins / group
            if source.is_dir():
                target = staging / "plugins" / group
                if group == "platforms":
                    target.mkdir(parents=True)
                    desktop = {"libqxcb.so", "libqwayland-egl.so", "libqwayland-generic.so", "libqoffscreen.so", "libqminimal.so"}
                    for plugin in source.iterdir():
                        if (plugin.name in desktop or plugin.name.startswith("libqwayland")) and plugin.is_file():
                            shutil.copy2(plugin, target)
                else:
                    shutil.copytree(source, target)
                plugins.extend(p for p in target.rglob("*.so") if p.is_file())
        if not (staging / "plugins/platforms/libqoffscreen.so").exists():
            raise ValueError("The Qt SDK lacks its offscreen platform plugin, required for package self-checks.")
    binaries = [p for p in (staging / "bin").rglob("*") if p.is_file() and is_dynamic_elf(p)]
    for binary in [*binaries, *plugins]:
        dependencies, missing = ldd_dependencies(binary, search)
        if missing and binary not in plugins:
            raise ValueError(f"Missing runtime dependencies for {binary.name}: {', '.join(missing)}. "
                             "Install them or set AP_EXTRA_LIB_DIRS.")
        if missing:
            optional.append({"plugin": binary.relative_to(staging).as_posix(), "missing": missing})
        for name, source in dependencies:
            if BASE_LINUX.match(name):
                excluded.add(name)
                continue
            target = library_dir / name
            if target.exists():
                if source.resolve() != target.resolve() and digest(source) != digest(target):
                    raise ValueError(f"Conflicting runtime library versions: {name}")
            else:
                shutil.copy2(source.resolve(), target)
    return sorted(excluded), optional


def windows_system(name):
    lower = name.lower()
    if lower.startswith(("api-ms-", "ext-ms-")) or lower.rsplit(".", 1)[0] in WINDOWS_SYSTEM:
        return True
    if os.name == "nt":
        return (Path(os.environ.get("SystemRoot", "C:/Windows")) / "System32" / name).is_file()
    return (Path("/mnt/c/Windows/System32") / name).is_file()


def collect_windows(staging, settings, info, builder):
    search = [staging / "bin", *settings.extra_lib_dirs]
    if info["frontend"] == "gui":
        core, qmake, plugins, _ = qt_locations(info, settings)
        search.append(core.parent)
        tools = [core.parent / "windeployqt.exe", qmake.parent / "windeployqt.exe" if qmake else core.parent / "windeployqt"]
        deploy = next((p for p in tools if p.is_file()), None)
        if not deploy:
            found = shutil.which("windeployqt") or shutil.which("windeployqt.exe")
            deploy = Path(found) if found else None
        if not deploy:
            raise ValueError("A Windows GUI package needs the matching SDK's windeployqt. Set AP_QT_PREFIX.")
        def tool_path(path):
            if os.name != "nt" and deploy.suffix.lower() == ".exe":
                return subprocess.check_output(["wslpath", "-w", str(path)], text=True).strip()
            return str(path)
        env = dict(os.environ)
        # windeployqt must use the Qt SDK that built the workbench.
        if os.name == "nt":
            env["PATH"] = str(core.parent) + os.pathsep + env.get("PATH", "")
        command = [str(deploy), "--debug" if settings.configuration == "Debug" else "--release",
                   "--no-translations", "--dir", tool_path(staging / "bin"),
                   "--plugindir", tool_path(staging / "plugins"), tool_path(staging / "bin/apack-workbench.exe")]
        subprocess.run(command, env=env, check=True)
        for name in ("qoffscreen.dll", "qoffscreend.dll"):
            source = plugins / "platforms" / name
            if source.exists():
                (staging / "plugins/platforms").mkdir(parents=True, exist_ok=True)
                shutil.copy2(source, staging / "plugins/platforms" / name)
    # MinGW compiler runtime DLLs, if this is a cross build. MSVC backend uses /MT.
    search.extend(Path("/usr/lib/gcc/x86_64-w64-mingw32").glob("*"))
    search.append(Path("/usr/x86_64-w64-mingw32/lib"))
    available = {}
    for folder in search:
        if folder.is_dir():
            available.update({p.name.lower(): p for p in folder.glob("*.dll") if p.is_file()})
    queue = [p for folder in (staging / "bin", staging / "plugins") if folder.exists()
             for p in folder.rglob("*") if p.suffix.lower() in (".exe", ".dll") and not p.name.lower().startswith("vc_redist")]
    inspected = set()
    system = set()
    while queue:
        binary = queue.pop()
        if binary in inspected:
            continue
        inspected.add(binary)
        for name in pe_imports(binary):
            if windows_system(name):
                system.add(name)
                continue
            destination = staging / "bin" / name
            if destination.exists():
                continue
            if name.lower().startswith(("vcruntime", "msvcp", "concrt", "vcomp")):
                # The matching official redistributable is a system prerequisite.
                system.add(name)
                continue
            source = available.get(name.lower())
            if not source:
                raise ValueError(f"Missing redistributable DLL {name}, required by {binary.name}. Set AP_EXTRA_LIB_DIRS.")
            shutil.copy2(source, destination)
            queue.append(destination)
    return sorted(system), []


def write_launchers(staging, settings, info):
    frontend = "apack-workbench" if info["frontend"] == "gui" else "apack"
    wizard = "" if info["frontend"] == "gui" else " wizard"
    if settings.platform == "linux":
        prefix = ('#!/bin/sh\nset -eu\n'
                  'ap_package_root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)\n'
                  'export LD_LIBRARY_PATH="$ap_package_root/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"\n')
        for name, command in (("apack", 'exec "$ap_package_root/bin/apack" "$@"\n'),
                              ("start-frontend.sh", f'exec "$ap_package_root/bin/{frontend}"{wizard} "$@"\n')):
            gui_env = ('export QT_PLUGIN_PATH="$ap_package_root/plugins"\n'
                       'export QT_QPA_PLATFORM_PLUGIN_PATH="$ap_package_root/plugins/platforms"\n') if name == "start-frontend.sh" and info["frontend"] == "gui" else ""
            text = prefix + gui_env + command
            (staging / name).write_text(text, encoding="utf-8")
            (staging / name).chmod(0o755)
        shutil.copy2(ROOT / "tools/package/check-deps.sh", staging)
        (staging / "check-deps.sh").chmod(0o755)
    else:
        for name, command in (("apack.cmd", '"%~dp0bin\\apack.exe" %*'),
                              ("start-frontend.cmd", f'"%~dp0bin\\{frontend}.exe"{wizard} %*'),
                              ("check-deps.cmd", 'powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0check-deps.ps1" %*')):
            gui_env = 'set "QT_PLUGIN_PATH=%~dp0plugins"\r\nset "QT_QPA_PLATFORM_PLUGIN_PATH=%~dp0plugins\\platforms"\r\n' if name == "start-frontend.cmd" and info["frontend"] == "gui" else ""
            (staging / name).write_bytes(("@echo off\r\nsetlocal\r\n" + gui_env + command + "\r\nexit /b %errorlevel%\r\n").encode("utf-8"))
        shutil.copy2(ROOT / "tools/package/check-deps.ps1", staging)
    if info["frontend"] == "gui":
        (staging / "bin/qt.conf").write_text("[Paths]\nPrefix=..\nLibraries=lib\nPlugins=plugins\nTranslations=translations\n", encoding="utf-8")
    backend = "bin/apack.exe" if settings.platform == "windows" else "bin/apack"
    backend_files = {backend}
    if settings.platform == "linux":
        dependencies, _ = ldd_dependencies(staging / backend, [staging / "lib"])
        backend_files.update("lib/" + name for name, _ in dependencies if not BASE_LINUX.match(name))
    else:
        queue = [staging / backend]
        while queue:
            for name in pe_imports(queue.pop()):
                relative = "bin/" + name
                if relative not in backend_files and (staging / relative).exists():
                    backend_files.add(relative)
                    queue.append(staging / relative)
    runtime_files = {p.relative_to(staging).as_posix() for folder in (staging / "bin", staging / "lib", staging / "plugins")
                     if folder.exists() for p in folder.rglob("*") if p.is_file()}
    (staging / "runtime-files.txt").write_text("\n".join(sorted(runtime_files)) + "\n", encoding="utf-8")
    (staging / "runtime-backend.txt").write_text("\n".join(sorted(backend_files)) + "\n", encoding="utf-8")


def deploy_package(staging, settings, info, builder):
    system, optional = collect_linux(staging, settings, info) if settings.platform == "linux" else collect_windows(staging, settings, info, builder)
    licenses = staging / "licenses"
    licenses.mkdir()
    for source, name in ((ROOT / "LICENSE", "Accretion-LICENSE.txt"),
                         (ROOT / "THIRD_PARTY_NOTICES.md", "THIRD_PARTY_NOTICES.md"),
                         (ROOT / "third_party/lz4/LICENSE", "LZ4-LICENSE.txt"),
                         (ROOT / "third_party/elf2epf/LICENSE", "elf2epf-LICENSE.txt")):
        shutil.copy2(source, licenses / name)
    if info["frontend"] == "gui":
        _, _, _, prefixes = qt_locations(info, settings)
        for prefix in prefixes:
            for name in ("Licenses", "LICENSES", "licenses"):
                source = prefix / name
                if source.is_dir() and not (licenses / "Qt").exists():
                    shutil.copytree(source, licenses / "Qt")
        # Debian/Ubuntu put runtime copyright notices in /usr/share/doc.
        roots = [Path("/usr/share/doc")]
        if settings.qt_prefix:
            roots.insert(0, settings.qt_prefix / "share/doc")
        for root in roots:
            if root.is_dir():
                for source in root.glob("libqt6*/copyright"):
                    target = licenses / "runtime" / source.parent.name
                    target.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(source, target / "copyright")
    docs = staging / "share/docs"
    docs.mkdir(parents=True)
    for source in (ROOT / "docs").glob("*.md"):
        shutil.copy2(source, docs)
    (staging / "share/schema").mkdir(parents=True)
    shutil.copy2(ROOT / "schema/project.schema.json", staging / "share/schema")
    write_launchers(staging, settings, info)
    suffix = ".cmd" if settings.platform == "windows" else ".sh"
    cli = "apack.cmd" if settings.platform == "windows" else "./apack"
    (staging / "START_HERE.txt").write_text(
        f"Accretion Package {info['version']} — {settings.platform}-x64\n\n"
        f"Frontend: {info['frontend']}\n"
        f"Installer templates: {', '.join(settings.runtimes)}; UI: {', '.join(settings.modes)}\n"
        f"Start frontend: start-frontend{suffix}\n"
        f"Backend: {cli} --help (or run bin/apack{'.exe' if settings.platform == 'windows' else ''})\n"
        f"Check dependencies: check-deps{suffix}\n\n"
        f"Choose an included template when building: {cli} build --project project.json --ui {settings.modes[0]}\n\n"
        "This entire folder can be moved. Keep bin/ap-stubs, lib/plugins (when present),\n"
        "and bin/qt.conf together. Source code, build directories, Python, CMake and Qt SDK\n"
        "are not needed at runtime. System libraries/display services may still be required;\n"
        "check-deps reports missing requirements. See share/docs/building.md.\n",
        encoding="utf-8")
    records = [{"path": p.relative_to(staging).as_posix(), "size": p.stat().st_size, "sha256": digest(p)}
               for p in sorted(staging.rglob("*")) if p.is_file()]
    manifest = {"format": "accretion-portable-v1", "version": info["version"], "platform": settings.platform,
                "architecture": "x64", "configuration": settings.configuration, "frontend": info["frontend"],
                "runtimePlatforms": list(settings.runtimes), "runtimeModes": list(settings.modes),
                "systemLibraries": system, "optionalPluginDependencies": optional, "files": records}
    (staging / "package-manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    if optional:
        print("Some optional Qt plugins need system libraries; check-deps will report their names.")
