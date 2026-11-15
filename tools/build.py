#!/usr/bin/env python3
"""Dependency-light bootstrap build; CMake is the supported native build too."""
from __future__ import annotations
import argparse
import concurrent.futures
import json
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
CORE = ["base", "json", "hash", "io", "package", "manifest", "context", "transaction", "engine", "integrations"]

def run(command):
    command=[str(x) for x in command]
    cache=None
    if "-c" in command and "-o" in command:
        output=Path(command[command.index("-o")+1]);source=ROOT/command[command.index("-c")+1]
        cache=output.with_suffix(output.suffix+".command")
        signature=hashlib.sha256("\0".join(command).encode()).hexdigest()
        dependencies=[source,*ROOT.glob("include/ap/*"),ROOT/"third_party/lz4/lz4.h",*ROOT.glob("src/**/*.hpp"),*ROOT.glob("third_party/elf2epf/**/*.hpp")]
        if output.exists() and cache.exists() and cache.read_text()==signature and all(p.stat().st_mtime<=output.stat().st_mtime for p in dependencies if p.is_file()):
            return subprocess.CompletedProcess(command,0,"","")
    result = subprocess.run([str(x) for x in command], cwd=ROOT, text=True, capture_output=True)
    if result.returncode:
        raise RuntimeError(" ".join(str(x) for x in command) + "\n" + result.stdout + result.stderr)
    if cache:
        cache.write_text(signature)
    return result

def need(name):
    value = shutil.which(name)
    if not value:
        raise RuntimeError(f"Missing {name}; install the compiler toolchain or use CMake with your native compiler")
    return value

def build(target, out, jobs, sanitize=False, adapter=None, runtime_only=False, modes=("gui", "tui"), configuration="RelWithDebInfo"):
    obj = out / "obj" / target
    obj.mkdir(parents=True, exist_ok=True)
    optimization = "-O1" if sanitize else "-O0" if configuration == "Debug" else "-Os" if configuration == "MinSizeRel" else "-O2"
    symbols = ["-g"] if sanitize or configuration in ("Debug", "RelWithDebInfo") else []
    common = ["-std=c++17", optimization, *symbols, *([] if configuration == "Debug" else ["-DNDEBUG"]), "-Wall", "-Wextra", "-Wno-misleading-indentation", "-fno-exceptions", "-fno-rtti", "-fno-builtin", "-ffunction-sections", "-fdata-sections", "-Iinclude", "-Ithird_party/lz4", "-Ithird_party/elf2epf/include"]
    if sanitize:
        common += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    platform = target.split("-")[0]
    if platform == "linux":
        compiler = need(os.environ.get("CXX", "g++")); cc = need(os.environ.get("CC", "gcc"))
        prefix = []; link = [compiler]; libs = ["-ldl"]; suffix = ""
    elif platform == "windows":
        compiler = need("clang++"); cc = need("clang")
        prefix = ["--target=x86_64-w64-windows-gnu", "--sysroot=/usr/x86_64-w64-mingw32"]
        link = [need("x86_64-w64-mingw32-gcc")]; libs = ["-static-libgcc", "-lcomctl32", "-lshell32", "-lole32", "-luuid", "-ladvapi32", "-luser32", "-lgdi32"]; suffix = ".exe"
    elif platform == "xj380":
        compiler = need("clang++"); cc = need("clang")
        prefix = ["--target=x86_64-unknown-none-elf", "-ffreestanding", "-fno-pic", "-fno-pie", "-fno-stack-protector", "-mno-red-zone", "-fno-threadsafe-statics", "-fno-use-cxa-atexit", "-DAP_XJ380", "-nostdinc", "-isystem", run([cc, "-print-resource-dir"]).stdout.strip() + "/include"]
        link = [need(os.environ.get("AP_XJ380_LINKER", "ld")), "-T", ROOT / "cmake/xj380.ld"]; libs = []; suffix = ".elf"
    else:
        raise RuntimeError("Unsupported target " + target)
    sources = [f"src/core/{name}.cpp" for name in CORE] + ["src/platform/memory.cpp", f"src/platform/{platform}.cpp", "src/ui/model.cpp", "src/ui/tui.cpp", f"src/ui/{platform}_gui.cpp"]
    if platform == "windows":
        sources.append("src/runtime/windows_resources.cpp")
    if platform == "xj380":
        sources.extend(["src/platform/xj380_api.cpp","src/platform/xj380_adapter.cpp"])
        if adapter:
            sources.append(str(adapter.resolve()))
    commands = []
    objects = []
    for source in sources:
        output = obj / (source.replace("/", "_") + ".o")
        objects.append(output)
        commands.append([compiler, *prefix, *common, "-c", source, "-o", output])
    lz4 = obj / "lz4.o"
    lz_flags = ["-O2", "-fno-builtin", "-ffunction-sections", "-fdata-sections", "-Iinclude", "-Ithird_party/lz4", "-Ithird_party/elf2epf/include"]
    if platform == "xj380":
        lz_flags += ["-include", "ap/codec_config.h"]
    commands.append([cc, *prefix, *lz_flags, "-c", "third_party/lz4/lz4.c", "-o", lz4])
    objects.append(lz4)
    if platform == "xj380":
        crt = obj / "crt.o"
        commands.append([cc, *prefix, "-c", "src/platform/xj380_crt.S", "-o", crt]); objects.append(crt)
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as executor:
        for _ in executor.map(run, commands):
            pass
    stubs = out / "ap-stubs"
    stubs.mkdir(parents=True, exist_ok=True)
    for mode in modes:
        main = obj / ("main-" + mode + ".o")
        run([compiler, *prefix, *common, *(["-DAP_DEFAULT_TUI"] if mode == "tui" else []), "-c", "src/runtime/main.cpp", "-o", main])
        output = stubs / ("ap-setup-" + target + "-" + mode + suffix)
        flags = ["--gc-sections"] if platform == "xj380" else ["-Wl,--gc-sections"]
        if platform == "windows" and mode == "gui":
            flags.append("-mwindows")
        if sanitize:
            flags.append("-fsanitize=address,undefined")
        run([*link, *flags, main, *objects, *libs, "-o", output])
        print("Built", output.relative_to(ROOT) if output.is_relative_to(ROOT) else output, flush=True)
    if platform == "linux" and not runtime_only:
        main = obj / "builder.o"
        run([compiler, "-std=c++17", "-O2", "-Iinclude", "-Ithird_party/lz4", "-Ithird_party/elf2epf/include", "-c", "src/builder/main.cpp", "-o", main])
        output = out / "apack"
        flags = ["-Wl,--gc-sections"] + (["-fsanitize=address,undefined"] if sanitize else [])
        # The CLI doesn't link GUI objects; its wizard works without GTK.
        cli_objects = [p for p in objects if "src_ui_" not in p.name]
        pe = obj / "builder-pe.o"
        run([compiler,"-std=c++17","-O2","-Iinclude","-Ithird_party/lz4","-c","src/builder/pe.cpp","-o",pe])
        epf_objects=[]
        for source in ["converter","elf","epf","io"]:
            object_file=obj/("epf-"+source+".o")
            run([compiler,"-std=c++17","-O2","-Ithird_party/elf2epf/include","-c","third_party/elf2epf/src/"+source+".cpp","-o",object_file]);epf_objects.append(object_file)
        epf_builder=obj/"builder-epf.o"
        run([compiler,"-std=c++17","-O2","-Iinclude","-Ithird_party/lz4","-Ithird_party/elf2epf/include","-c","src/builder/epf.cpp","-o",epf_builder])
        run([compiler, *flags, main, pe, epf_builder, *epf_objects, *cli_objects, "-ldl", "-o", output])
        unit=obj / "tests-unit.o"
        run([compiler,"-std=c++17","-O1","-g","-Iinclude","-Ithird_party/lz4",*(["-fsanitize=address,undefined"] if sanitize else []),"-c","tests/unit.cpp","-o",unit])
        run([compiler,*flags,unit,*cli_objects,"-ldl","-o",out/"ap-core-tests"])
        print("Built", output, flush=True)
    if platform == "linux" and not runtime_only:
        run([compiler,"-std=c++17","-O2","-DAP_XAPI_TEST","-Iinclude","tests/xapi_abi.cpp","src/platform/xj380_api.cpp","-o",out/"ap-xapi-tests"])
        run([compiler,"-std=c++17","-O2","-DAP_XJ_ADAPTER_TEST","-Iinclude","tests/xj_adapter.cpp","src/platform/xj380_adapter.cpp","src/core/base.cpp","src/core/json.cpp","src/platform/memory.cpp","-o",out/"ap-xj-adapter-tests"])
    if platform == "windows" and not runtime_only:
        test=obj/"registry-test.o"
        run([compiler,*prefix,*common,"-c","tests/windows_registry.cpp","-o",test])
        native_objects=[p for p in objects if "src_ui_" not in p.name]
        run([*link,"-Wl,--gc-sections",test,*native_objects,*libs,"-o",out/"ap-windows-registry-tests.exe"])
    return platform

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target", choices=("all", "linux", "windows", "xj380"), default="linux")
    parser.add_argument("--out", type=Path, default=ROOT / "build")
    parser.add_argument("--jobs", type=int, default=min(os.cpu_count() or 2, 4))
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--runtime-only", action="store_true", help="Build installer templates without host tools or tests")
    parser.add_argument("--ui", choices=("both", "gui", "tui"), default="both")
    parser.add_argument("--configuration", choices=("Release", "Debug", "MinSizeRel", "RelWithDebInfo"), default="RelWithDebInfo")
    parser.add_argument("--xj-adapter", type=Path, help="Native XJ380 v1 adapter source supplied by the OS SDK")
    args = parser.parse_args()
    targets = ("linux", "windows", "xj380") if args.target == "all" else (args.target,)
    args.out = args.out.resolve(); args.out.mkdir(parents=True, exist_ok=True)
    try:
        for name in targets:
            print("Building", name, flush=True)
            build(name + "-x64", args.out, args.jobs, args.sanitize and name == "linux", args.xj_adapter,
                  args.runtime_only, ("gui", "tui") if args.ui == "both" else (args.ui,), args.configuration)
        return 0
    except (RuntimeError, OSError) as error:
        print(error, file=sys.stderr)
        return 1

if __name__ == "__main__":
    raise SystemExit(main())
