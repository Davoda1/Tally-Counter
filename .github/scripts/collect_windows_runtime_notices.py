#!/usr/bin/env python3
"""Collect the native UCRT64 compiler/runtime notices from installed packages."""
# Copyright (c) 2026 davoda1. See LICENSE for the zlib license.
from pathlib import Path
import shutil
import subprocess


def output(*args):
    return subprocess.check_output(args, text=True)


def main():
    root = Path(__file__).resolve().parents[2]
    destination = root / "build/windows-runtime-notices"
    prefix = "mingw-w64-ucrt-x86_64-"
    installed = set(output("pacman", "-Qq").splitlines())
    candidates = [prefix + name for name in ("gcc", "crt", "headers", "winpthreads", "libwinpthread")]
    packages = [name for name in candidates if name in installed]
    if not {prefix + "gcc", prefix + "crt"} <= set(packages):
        raise RuntimeError("The native UCRT64 GCC and CRT packages must be installed")
    if destination.exists():
        shutil.rmtree(destination)
    destination.mkdir(parents=True)
    (destination / "PACKAGES.txt").write_text(output("pacman", "-Q", *packages), encoding="utf-8")
    (destination / "TOOLCHAIN.txt").write_text(output("g++", "--version") + "\nTarget: " + output("g++", "-dumpmachine"), encoding="utf-8")
    license_root = Path("/ucrt64/share/licenses")
    families = set()
    copied = 0
    for filename in output("pacman", "-Qql", *packages).splitlines():
        source = Path(filename)
        if not source.is_relative_to(license_root) or not source.is_file():
            continue
        relative = source.relative_to(license_root)
        target = destination / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
        families.add(relative.parts[0])
        copied += 1
    if not {"gcc", "crt"} <= families:
        raise RuntimeError("Installed GCC/CRT packages did not provide their runtime notices")
    print(f"Collected {copied} runtime notice files from {len(packages)} installed packages.")


if __name__ == "__main__":
    main()
