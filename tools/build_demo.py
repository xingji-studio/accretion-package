#!/usr/bin/env python3
"""Build the small, real native applications used by examples/project.json."""
from __future__ import annotations
import argparse
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]

def run(command):
    subprocess.run([str(x) for x in command], cwd=ROOT, check=True)

def build(target, output):
    folder = output / target
    folder.mkdir(parents=True, exist_ok=True)
    source = ROOT / "examples/app/hello.cpp"
    if target == "linux":
        binary = folder / "toolbox"
        run(["g++", "-O2", source, "-o", binary])
    elif target == "windows":
        binary = folder / "toolbox.exe"
        obj = folder / "hello.o"
        run(["clang++", "--target=x86_64-w64-windows-gnu", "--sysroot=/usr/x86_64-w64-mingw32",
             "-O2", "-c", source, "-o", obj])
        run(["x86_64-w64-mingw32-gcc", obj, "-o", binary])
        obj.unlink()
    else:
        binary = folder / "toolbox.elf"
        flags = ["--target=x86_64-unknown-none-elf", "-ffreestanding", "-fno-builtin", "-fno-pie",
                 "-fno-pic", "-fno-stack-protector", "-mno-red-zone", "-fno-exceptions", "-fno-rtti",
                 "-ffunction-sections", "-fdata-sections", "-DAP_XJ380", "-Iinclude"]
        objects = []
        for name, filename in [("hello", source), ("xapi", ROOT / "src/platform/xj380_api.cpp"),
                               ("crt", ROOT / "src/platform/xj380_crt.S")]:
            obj = folder / (name + ".o")
            run(["clang++", *flags, "-O2", "-c", filename, "-o", obj])
            objects.append(obj)
        run(["ld", "--gc-sections", "-T", ROOT / "cmake/xj380.ld", *objects, "-o", binary])
        for obj in objects:
            obj.unlink()
    cli = binary.with_name("toolbox-cli" + binary.suffix)
    shutil.copy2(binary, cli)
    (folder / "config").mkdir(exist_ok=True)
    (folder / "config/default.json").write_text('{"language":"zh-CN"}\n', encoding="utf-8")
    print("Built demo:", binary)

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target", choices=("all", "linux", "windows", "xj380"), default="all")
    parser.add_argument("--out", type=Path, default=ROOT / "build/demo")
    args = parser.parse_args()
    for target in (("linux", "windows", "xj380") if args.target == "all" else (args.target,)):
        build(target, args.out.resolve())
