#!/usr/bin/env python3
"""Package native Tally Counter builds into the five release deliverables."""
# Copyright (c) 2026 davoda1. See LICENSE for the zlib license.
from pathlib import Path
import argparse
import datetime
import gzip
import hashlib
import io
import os
import shutil
import subprocess
import tarfile
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
VERSION = "1.0.0"
STAMP = int(os.environ.get("SOURCE_DATE_EPOCH", "1794009600"))
FORMATS = ("windows", "deb", "linux", "appimage", "source")
NAMES = {
    "windows": f"Tally-Counter-{VERSION}-Windows-x64.zip",
    "deb": f"tally-counter_{VERSION}_amd64.deb",
    "linux": f"Tally-Counter-{VERSION}-Linux-x64.tar.gz",
    "appimage": f"Tally-Counter-{VERSION}-x86_64.AppImage",
    "source": f"Tally-Counter-{VERSION}-Source.tar.gz",
}


def copy(source, target, mode=0o644):
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, target)
    target.chmod(mode)


def sha256(path):
    result = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(block)
    return result.hexdigest()


def clean_info(info):
    if "__pycache__" in Path(info.name).parts or info.name.endswith((".pyc", ".pyo")):
        return None
    info.uid = info.gid = 0
    info.uname = info.gname = "root"
    info.mtime = STAMP
    # Git tracks only the executable bit; use a stable public mode, independent
    # of the builder's umask, with no setuid/setgid or group-write bits.
    info.mode = 0o755 if info.isdir() or info.mode & 0o111 else 0o644
    return info


def tar_gz(destination, members, extra_files=None):
    with destination.open("wb") as stream, gzip.GzipFile(filename="", mode="wb", fileobj=stream, mtime=STAMP) as compressed:
        with tarfile.open(mode="w", fileobj=compressed, format=tarfile.PAX_FORMAT) as archive:
            for path, arcname in members:
                archive.add(path, arcname=arcname, filter=clean_info)
            for arcname, payload in (extra_files or {}).items():
                info = tarfile.TarInfo(arcname)
                info.size = len(payload)
                info.mode = 0o644
                archive.addfile(clean_info(info), io.BytesIO(payload))


def common_docs(directory):
    for name in ("LICENSE", "README.md", "THIRD-PARTY-NOTICES.txt", "CHANGELOG.md", "CONTRIBUTING.md", "SECURITY.md"):
        copy(ROOT / name, directory / name)
    for item in sorted((ROOT / "licenses").glob("*")):
        if item.is_file():
            copy(item, directory / "licenses" / item.name)
    if (ROOT / "docs").is_dir():
        shutil.copytree(ROOT / "docs", directory / "docs")


def require_binary(path, magic):
    if not path.is_file():
        raise RuntimeError(f"Build the native executable first: {path.name}")
    with path.open("rb") as stream:
        if stream.read(len(magic)) != magic:
            raise RuntimeError(f"Expected a real native executable: {path}")


def windows_package(binary, destination):
    readme = (
        "Tally Counter 1.0.0\r\n\r\n"
        "Run TallyCounter.exe. No installation or separate runtime is needed.\r\n"
        "Click the app or press Space to add one. Holding Space counts once.\r\n"
        "Click the reset knob to clear. Tab selects Reset; Enter activates it.\r\n"
        "Space always adds one, including when Reset has keyboard focus.\r\n"
        "The reset knob rotates clockwise; higher counts rotate longer, up to 2.2s.\r\n"
        "Digits roll backward in sync with the knob; a new tally finishes the rewind.\r\n"
        "Successful tallies play a quiet embedded mechanical click.\r\n"
        "Window size, position and maximized state are remembered automatically.\r\n"
        "Counting stops at 9999. The app remembers the count automatically.\r\n"
        "Saved count: %APPDATA%\\TallyCounter\\count.txt\r\n"
        "Window placement: %APPDATA%\\TallyCounter\\window.ini\r\n"
        "The appearance follows Windows light/dark settings.\r\n\r\n"
        "Windows 10/11 x64. Copyright (c) 2026 davoda1.\r\n"
        "See LICENSE, README.md and THIRD-PARTY-NOTICES.txt for details.\r\n"
    )
    timestamp = datetime.datetime.fromtimestamp(max(STAMP, 315532800), datetime.timezone.utc).timetuple()[:6]
    with zipfile.ZipFile(destination, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        def add(name, payload, mode=0o644):
            entry = zipfile.ZipInfo(name, timestamp)
            entry.create_system = 3
            entry.compress_type = zipfile.ZIP_DEFLATED
            entry.external_attr = (0o100000 | mode) << 16
            archive.writestr(entry, payload)
        add("TallyCounter.exe", binary.read_bytes(), 0o755)
        add("README.txt", readme.encode("utf-8"))
        for name in ("LICENSE", "README.md", "THIRD-PARTY-NOTICES.txt", "CHANGELOG.md", "CONTRIBUTING.md", "SECURITY.md"):
            add(name, (ROOT / name).read_bytes())
        for item in sorted((ROOT / "licenses").glob("*")):
            if item.is_file():
                add("licenses/" + item.name, item.read_bytes())
        for item in sorted((ROOT / "build/windows-runtime-notices").rglob("*")):
            if item.is_file():
                add("licenses/msys2/" + item.relative_to(ROOT / "build/windows-runtime-notices").as_posix(), item.read_bytes())
        for item in sorted((ROOT / "docs").rglob("*")):
            if item.is_file():
                add(item.relative_to(ROOT).as_posix(), item.read_bytes())


def deb_package(binary, destination):
    with tempfile.TemporaryDirectory(prefix="tally-package-") as temporary:
        stage = Path(temporary)
        stage.chmod(0o755)
        copy(binary, stage / "usr/bin/tally-counter", 0o755)
        subprocess.run(["strip", "--strip-unneeded", str(stage / "usr/bin/tally-counter")], check=True)
        copy(ROOT / "packaging/tally-counter.desktop", stage / "usr/share/applications/tally-counter.desktop")
        copy(ROOT / "assets/icon.svg", stage / "usr/share/icons/hicolor/scalable/apps/tally-counter.svg")
        copy(ROOT / "assets/icon.png", stage / "usr/share/icons/hicolor/256x256/apps/tally-counter.png")
        docs = stage / "usr/share/doc/tally-counter"
        common_docs(docs)
        copy(ROOT / "THIRD-PARTY-NOTICES.txt", docs / "copyright")
        installed = sum(path.stat().st_size for path in stage.rglob("*") if path.is_file())
        control = stage / "DEBIAN"
        control.mkdir()
        (control / "control").write_text(
            "Package: tally-counter\n"
            f"Version: {VERSION}\nArchitecture: amd64\n"
            "Section: utils\nPriority: optional\nMaintainer: davoda1\n"
            f"Installed-Size: {(installed + 1023) // 1024}\n"
            "Depends: libc6 (>= 2.38), libgtk-3-0 (>= 3.24) | libgtk-3-0t64 (>= 3.24), "
            "libglib2.0-0 (>= 2.66) | libglib2.0-0t64 (>= 2.66), "
            "libcairo2 (>= 1.14), libpango-1.0-0 (>= 1.44), libpangocairo-1.0-0 (>= 1.44), "
            "libatk1.0-0 (>= 2.36) | libatk1.0-0t64 (>= 2.36), libasound2t64 | libasound2\n"
            "Description: Minimal native four-digit tally counter\n"
            " A mechanical rolling counter from 0000 to 9999. Click or press Space\n"
            " to increment, and use the reset knob to clear. The count is saved\n"
            " automatically. No browser, network service, or launcher script.\n",
            encoding="utf-8")
        for item in [stage, *stage.rglob("*")]:
            os.utime(item, (STAMP, STAMP), follow_symlinks=False)
        env = os.environ.copy()
        env["SOURCE_DATE_EPOCH"] = str(STAMP)
        subprocess.run(["dpkg-deb", "--root-owner-group", "-Zxz", "-z9", "--build", str(stage), str(destination)], check=True, env=env)


def linux_package(binary, destination):
    with tempfile.TemporaryDirectory(prefix="tally-linux-") as temporary:
        stage = Path(temporary)
        copy(binary, stage / "tally-counter", 0o755)
        subprocess.run(["strip", "--strip-unneeded", str(stage / "tally-counter")], check=True)
        common_docs(stage)
        copy(ROOT / "packaging/tally-counter.desktop", stage / "tally-counter.desktop")
        copy(ROOT / "assets/icon.png", stage / "tally-counter.png")
        tar_gz(destination, [(path, path.name) for path in sorted(stage.iterdir())])


def source_package(output, destination, provenance, corresponding_source):
    binaries = [output / NAMES[name] for name in FORMATS if name != "source"]
    missing = [path.name for path in binaries if not path.is_file()]
    if missing:
        raise RuntimeError("Source SHA256SUMS needs all four completed binaries: " + ", ".join(missing))
    if not (provenance / "BUNDLED-LIBRARIES.json").is_file():
        raise RuntimeError(f"Source package needs AppImage provenance: {provenance}; build the AppImage first or supply --appimage-provenance")
    from fetch_corresponding_source import collect, collect_runtime, validate_sources
    try:
        validate_sources(provenance, corresponding_source)
    except RuntimeError:
        collect(provenance / "BUNDLED-LIBRARIES.json", corresponding_source,
                ["https://archive.ubuntu.com/ubuntu", "https://security.ubuntu.com/ubuntu"])
        collect_runtime(corresponding_source)
        validate_sources(provenance, corresponding_source)
    sums = "".join(f"{sha256(path)}  {path.name}\n" for path in binaries)
    top = f"Tally-Counter-{VERSION}-Source"
    names = ("src", "assets", "tests", "packaging", "licenses", "docs", ".github", "Makefile",
             "README.md", "LICENSE", "THIRD-PARTY-NOTICES.txt", "VERIFICATION.txt", ".gitignore", ".gitattributes",
             "CHANGELOG.md", "CONTRIBUTING.md", "SECURITY.md")
    members = [(ROOT / name, f"{top}/{name}") for name in names if (ROOT / name).exists()]
    members.append((provenance, f"{top}/third-party/appimage"))
    members.append((corresponding_source, f"{top}/third-party/corresponding-source"))
    windows_notices = ROOT / "build/windows-runtime-notices"
    if windows_notices.is_dir():
        members.append((windows_notices, f"{top}/licenses/msys2"))
    tar_gz(destination, members, {f"{top}/SHA256SUMS": sums.encode("utf-8")})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "dist")
    parser.add_argument("--formats", nargs="+", choices=FORMATS, default=list(FORMATS), help="build selected formats; default: all five")
    parser.add_argument("--appimage-tools", type=Path, default=ROOT / "build/appimage-tools")
    parser.add_argument("--appimage-provenance", type=Path, default=ROOT / "build/appimage-provenance")
    parser.add_argument("--dependency-root", type=Path, help="optional directory with extracted extra Debian build dependencies")
    parser.add_argument("--corresponding-source", type=Path, default=ROOT / "build/corresponding-source", help="mandatory exact third-party source payload collected by fetch_corresponding_source.py")
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    if output == ROOT or ROOT.is_relative_to(output):
        raise SystemExit("Choose a dedicated output directory, not the source root or its parent")
    linux, windows = ROOT / "build/tally-counter", ROOT / "build/TallyCounter.exe"
    selected = set(args.formats)
    try:
        if selected & {"deb", "linux", "appimage"}:
            require_binary(linux, b"\x7fELF")
        if "windows" in selected:
            require_binary(windows, b"MZ")
        for name in FORMATS:
            if name not in selected:
                continue
            destination = output / NAMES[name]
            # Never leave a partial release file under its final name.
            temporary = output / (NAMES[name] + ".partial")
            temporary.unlink(missing_ok=True)
            try:
                if name == "windows":
                    windows_package(windows, temporary)
                elif name == "deb":
                    deb_package(linux, temporary)
                elif name == "linux":
                    linux_package(linux, temporary)
                elif name == "appimage":
                    from appimage import build
                    build(ROOT, linux, temporary, args.appimage_tools.resolve(), STAMP, args.dependency_root)
                elif name == "source":
                    source_package(output, temporary, args.appimage_provenance.resolve(), args.corresponding_source.resolve())
                os.replace(temporary, destination)
            finally:
                temporary.unlink(missing_ok=True)
            print(f"{destination.name}: {destination.stat().st_size:,} bytes", flush=True)
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        raise SystemExit(str(error)) from error


if __name__ == "__main__":
    main()
