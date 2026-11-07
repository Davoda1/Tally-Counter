#!/usr/bin/env python3
"""Validate the exact five release artifacts, native entry points and checksums."""
# Copyright (c) 2026 davoda1. See LICENSE for the zlib license.
from pathlib import Path
import argparse
import io
import hashlib
import re
import json
import os
import struct
import subprocess
import tarfile
import tempfile
import zipfile

from package import FORMATS, NAMES, ROOT, VERSION, sha256


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def elf(payload, label):
    require(payload[:5] == b"\x7fELF\x02", f"{label} is not a 64-bit native ELF executable")
    require(payload[5] == 1 and struct.unpack_from("<H", payload, 18)[0] == 62, f"{label} is not x86-64")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "dist")
    args = parser.parse_args()
    output = args.output.resolve()
    present = {p.name for p in output.iterdir() if p.is_file()}
    require(present == set(NAMES.values()), "Release directory must contain exactly the five named artifacts")
    with zipfile.ZipFile(output / NAMES["windows"]) as archive:
        payload = archive.read("TallyCounter.exe")
        require(payload[:2] == b"MZ", "Windows root entry is not a real EXE")
        offset = struct.unpack_from("<I", payload, 0x3C)[0]
        require(payload[offset:offset + 6] == b"PE\0\0\x64\x86", "Windows EXE is not x86-64 PE")
        require({"LICENSE", "README.md", "THIRD-PARTY-NOTICES.txt"} <= set(archive.namelist()), "Windows notices missing")
        require(not any(p.endswith((".dll", ".py", ".bat", ".cmd")) for p in archive.namelist()), "Windows archive contains a launcher or separate runtime")
    with tarfile.open(output / NAMES["linux"], "r:gz") as archive:
        member = archive.getmember("tally-counter")
        require(member.isfile() and member.mode & 0o111, "Linux archive root executable is not executable")
        elf(archive.extractfile(member).read(), "Linux archive root executable")
        require(not any(p.name.endswith((".sh", ".py")) for p in archive.getmembers()), "Linux archive contains a launcher script")
    data = subprocess.check_output(["dpkg-deb", "--fsys-tarfile", str(output / NAMES["deb"])])
    with tarfile.open(fileobj=io.BytesIO(data), mode="r:") as archive:
        member = next(p for p in archive.getmembers() if p.name.lstrip("./") == "usr/bin/tally-counter")
        require(member.isfile() and member.mode & 0o111, "Debian /usr/bin executable is not executable")
        elf(archive.extractfile(member).read(), "Debian /usr/bin/tally-counter")
    top = f"Tally-Counter-{VERSION}-Source"
    with tarfile.open(output / NAMES["source"], "r:gz") as archive:
        members = archive.getmembers()
        require(all(p.name.startswith(top + "/") for p in members), "Source archive must have one clean top-level folder")
        forbidden = {"build", "dist", ".git", ".tools", "__pycache__", "node_modules"}
        require(not any(forbidden & set(Path(p.name).parts[1:]) for p in members), "Source archive contains build/cache/repository metadata")
        for name in ("LICENSE", "README.md", "Makefile", "packaging/package.py", "packaging/AppRun.c"):
            require(f"{top}/{name}" in archive.getnames(), f"Source archive is missing {name}")
        require(any(p.name.startswith(f"{top}/.github/workflows/") and p.isfile() for p in members), "Source archive is missing GitHub workflows")
        sums_files = [p for p in members if Path(p.name).name == "SHA256SUMS"]
        require(len(sums_files) == 1 and sums_files[0].name == f"{top}/SHA256SUMS", "Source must contain exactly one SHA256SUMS")
        sums = archive.extractfile(sums_files[0]).read().decode().splitlines()
        expected = [f"{sha256(output / NAMES[name])}  {NAMES[name]}" for name in FORMATS if name != "source"]
        require(sums == expected, "Source SHA256SUMS does not cover the four exact binary artifacts")
        manifest = json.load(archive.extractfile(f"{top}/third-party/appimage/BUNDLED-LIBRARIES.json"))
        require(manifest["architecture"] == "x86_64" and len(manifest["packages"]) > 20, "Bundled dependency provenance is incomplete")
        for package in manifest["packages"]:
            require(f"{top}/third-party/appimage/{package['copyright_file']}" in archive.getnames(), "Bundled dependency copyright missing")
        source_prefix = f"{top}/third-party/corresponding-source/"
        ubuntu = json.load(archive.extractfile(source_prefix + "UBUNTU-SOURCES.json"))
        runtime = json.load(archive.extractfile(source_prefix + "RUNTIME-SOURCES.json"))
        wanted = {(p["source_package"], p["source_version"]) for p in manifest["packages"]}
        actual = {(p["source_package"], p["source_version"]) for p in ubuntu["packages"]}
        require(wanted == actual, "Source archive does not supply the exact bundled dependency versions")
        require(runtime["runtime_binary_sha256"] == manifest["appimage_runtime"]["sha256"], "Runtime source mismatch")
        from fetch_corresponding_source import RUNTIME_SOURCES
        require({Path(p["file"]).name: p["sha256"] for p in runtime["files"]} ==
                {name: checksum for name, _, checksum in RUNTIME_SOURCES}, "Runtime source pins are incomplete")
        entries = [entry for package in ubuntu["packages"] for entry in package["files"]] + runtime["files"]
        source_hashes = {source_prefix + entry["file"]: entry["sha256"] for entry in entries}
        checked = set()
        for member in members:
            if member.name in source_hashes:
                digest = hashlib.sha256()
                with archive.extractfile(member) as stream:
                    for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                        digest.update(chunk)
                require(digest.hexdigest() == source_hashes[member.name], "Corrupted corresponding source: " + member.name)
                checked.add(member.name)
        require(checked == set(source_hashes), "Missing corresponding-source archive")
    image = output / NAMES["appimage"]
    with image.open("rb") as stream:
        header = stream.read(64)
    elf(header, "AppImage runtime")
    require(header[8:11] == b"AI\x02" and os.access(image, os.X_OK), "AppImage is not an executable type-2 image")
    with tempfile.TemporaryDirectory(prefix="tally-appimage-check-") as temporary:
        subprocess.run([str(image), "--appimage-extract"], cwd=temporary, stdout=subprocess.DEVNULL, check=True)
        appdir = Path(temporary) / "squashfs-root"
        elf((appdir / "AppRun").read_bytes(), "AppImage AppRun")
        elf((appdir / "usr/bin/tally-counter").read_bytes(), "AppImage application")
        require((appdir / "usr/lib/libgtk-3.so.0").is_file(), "AppImage does not bundle GTK3")
        require((appdir / "usr/lib/libasound.so.2").is_file(), "AppImage does not bundle ALSA")
        require((appdir / "usr/lib/alsa-lib/libasound_module_pcm_pulse.so").is_file(), "AppImage does not bundle desktop audio plugin")
        require((appdir / "usr/share/glib-2.0/schemas/gschemas.compiled").is_file(), "AppImage does not bundle GTK settings schemas")
        require((appdir / "usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf").is_file(), "AppImage does not bundle its digit font")
        versions = []
        for candidate in appdir.rglob("*"):
            if not candidate.is_file():
                continue
            with candidate.open("rb") as stream:
                if stream.read(4) != b"\x7fELF":
                    continue
            info = subprocess.check_output(["readelf", "--version-info", str(candidate)], text=True)
            versions.extend(tuple(int(part) for part in v.split(".")) for v in
                            re.findall(r"Name: GLIBC_(\d+(?:\.\d+)+)", info))
        require(max(versions) <= (2, 38), "An AppImage ELF exceeds the glibc 2.38 baseline")
        bundle_manifest = json.loads((appdir / "usr/share/doc/tally-counter/appimage-provenance/BUNDLED-LIBRARIES.json").read_text())
        require(bundle_manifest == manifest, "Source provenance does not match this AppImage")
        env = os.environ.copy()
        env["LD_LIBRARY_PATH"] = str(appdir / "usr/lib")
        listing = subprocess.check_output(["ldd", str(appdir / "usr/bin/tally-counter")], env=env, text=True)
        require("not found" not in listing, "AppImage has unresolved linked dependencies")
        require(str(appdir / "usr/lib/libgtk-3.so.0") in listing, "AppImage resolves GTK outside its bundle")
    print("PASS: exactly five release artifacts; native x86-64 entry points; four binary checksums; bundled resources and notices; all corresponding-source hashes; glibc <= 2.38")


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, KeyError, subprocess.CalledProcessError) as error:
        raise SystemExit(str(error)) from error
