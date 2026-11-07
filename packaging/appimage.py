#!/usr/bin/env python3
"""Create a relocatable GTK3 AppImage with a native ELF AppRun entry point.

Build on the oldest supported distribution (Ubuntu 24.04 for this release).
Only glibc and the kernel/display/audio services remain host requirements.
"""
# Copyright (c) 2026 davoda1. See LICENSE for the zlib license.
from pathlib import Path
import hashlib
import json
import os
import re
import shutil
import subprocess
import tempfile

from fetch_appimage_tools import TOOLS, fetch

BASE_LIBRARIES = {
    "libc.so.6", "libm.so.6", "libpthread.so.0", "libdl.so.2", "librt.so.1",
    "libresolv.so.2", "libutil.so.1", "libanl.so.1", "ld-linux-x86-64.so.2",
}
MULTIARCH = "x86_64-linux-gnu"


def run(command, **kwargs):
    return subprocess.run([str(item) for item in command], check=True, **kwargs)


def text_output(command, **kwargs):
    return run(command, stdout=subprocess.PIPE, text=True, **kwargs).stdout


def sha256(path):
    result = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(chunk)
    return result.hexdigest()


def copy(source, target, mode=None):
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, target)
    target.chmod(mode if mode is not None else (0o755 if os.access(source, os.X_OK) else 0o644))


def dependencies(binary, environment=None):
    result = {}
    listing = text_output(["ldd", binary], env=environment)
    if "not found" in listing:
        raise RuntimeError(f"Unresolved dependencies for {binary}:\n{listing}")
    for line in listing.splitlines():
        match = re.match(r"\s*(\S+)\s+=>\s+(/\S+)\s+\(", line)
        if match:
            result[match.group(1)] = Path(match.group(2))
    return result


def owner(path):
    candidates = [str(path), str(path.resolve())]
    candidates += [item.removeprefix("/usr") for item in candidates if item.startswith("/usr/lib/")]
    for candidate in dict.fromkeys(candidates):
        found = subprocess.run(["dpkg-query", "-S", candidate], text=True, capture_output=True)
        if found.returncode == 0:
            for line in found.stdout.splitlines():
                package, _, filename = line.rpartition(": ")
                if filename == candidate:
                    return package
    return None


def system_path(relative, dependency_root=None, required=True):
    # Extra extracted build dependencies are optional; the running distribution
    # remains authoritative for its already installed libraries and data.
    candidates = [Path("/") / relative]
    if dependency_root:
        candidates.append(Path(dependency_root) / relative)
    for item in candidates:
        if item.exists():
            return item
    if required:
        raise RuntimeError(f"Missing AppImage build dependency: /{relative}")
    return None


def build(root, binary, destination, tools_directory, stamp, dependency_root=None):
    root, binary, destination = Path(root), Path(binary), Path(destination)
    tools_directory = Path(tools_directory)
    fetched = fetch(tools_directory)
    toolroot = tools_directory / "squashfs-root"
    if not (toolroot / "AppRun").exists():
        with (tools_directory / "extract.log").open("w") as log:
            run([fetched["appimagetool-x86_64.AppImage"], "--appimage-extract"], cwd=tools_directory,
                stdout=log)
    metadata = root / "build/appimage-provenance"
    if metadata.exists():
        shutil.rmtree(metadata)
    metadata.mkdir(parents=True)
    records, packages = {}, {}
    extra_root = Path(dependency_root) if dependency_root else None

    def record(source, target, appdir, package_hint=None):
        package = package_hint or owner(source)
        if package is None and extra_root:
            try:
                relative = source.relative_to(extra_root)
            except ValueError:
                relative = None
            if relative is not None:
                # This mode is for locally extracted distro packages. Read the
                # adjacent .deb's control data without requiring installation.
                for deb in sorted(extra_root.parent.glob("*.deb")):
                    if deb.name.startswith("libasound2-plugins_") and "alsa" in str(relative):
                        package = "libasound2-plugins"
                        control = text_output(["dpkg-deb", "--field", deb])
                        fields = dict(line.split(": ", 1) for line in control.splitlines() if ": " in line and not line.startswith(" "))
                        source_field = fields.get("Source", package).split(" (")[0]
                        packages[package] = {"package": package, "version": fields["Version"],
                                             "source_package": source_field, "source_version": fields["Version"]}
                        break
        if package is None:
            raise RuntimeError(f"Cannot establish package/license provenance for {source}")
        if package not in packages:
            fields = text_output(["dpkg-query", "-W", "-f", "${Version}\t${source:Package}\t${source:Version}", package]).split("\t")
            packages[package] = {"package": package, "version": fields[0], "source_package": fields[1], "source_version": fields[2]}
        record_path = target.relative_to(appdir).as_posix()
        records[record_path] = {"path": record_path, "sha256": sha256(target), "package": package}

    with tempfile.TemporaryDirectory(prefix="tally-appimage-") as temporary:
        appdir = Path(temporary) / "TallyCounter.AppDir"
        appdir.mkdir()
        libdir = appdir / "usr/lib"
        libdir.mkdir(parents=True)
        app = appdir / "usr/bin/tally-counter"
        copy(binary, app, 0o755)
        run(["strip", "--strip-unneeded", app])
        run([os.environ.get("CC", "cc"), "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
             "-fstack-protector-strong", "-D_FORTIFY_SOURCE=2", root / "packaging/AppRun.c",
             "-Wl,-z,relro,-z,now", "-o", appdir / "AppRun"])
        run(["strip", "--strip-unneeded", appdir / "AppRun"])
        libraries = dependencies(binary)
        alsa = system_path(f"usr/lib/{MULTIARCH}/libasound.so.2", dependency_root)
        libraries["libasound.so.2"] = alsa
        libraries.update(dependencies(alsa))
        extra_modules = []
        gio = system_path(f"usr/lib/{MULTIARCH}/gio/modules/libdconfsettings.so", dependency_root, False)
        if gio:
            extra_modules.append((gio, appdir / "usr/lib/gio/modules" / gio.name))
        pulse_modules = []
        for name in ("libasound_module_pcm_pulse.so", "libasound_module_ctl_pulse.so"):
            module = system_path(f"usr/lib/{MULTIARCH}/alsa-lib/{name}", dependency_root, False)
            if module:
                pulse_modules.append(module)
                extra_modules.append((module, appdir / "usr/lib/alsa-lib" / name))
        if len(pulse_modules) != 2:
            raise RuntimeError("Install/extract libasound2-plugins before packaging: the AppImage includes PulseAudio/PipeWire ALSA support")
        # The app uses PNG icons. PNG and JPEG are built in on Ubuntu 24.04;
        # include raster loader modules too, without an unused SVG renderer.
        loader_directory = system_path(f"usr/lib/{MULTIARCH}/gdk-pixbuf-2.0/2.10.0/loaders", dependency_root)
        loader_names = {"libpixbufloader-png.so", "libpixbufloader-jpeg.so", "libpixbufloader-ico.so",
                        "libpixbufloader-bmp.so", "libpixbufloader-gif.so", "libpixbufloader-xpm.so"}
        for module in sorted(loader_directory.glob("*.so")):
            if module.name in loader_names:
                extra_modules.append((module, appdir / "usr/lib/gdk-pixbuf-2.0/2.10.0/loaders" / module.name))
        for source, target in extra_modules:
            copy(source, target)
            record(source, target, appdir)
            libraries.update(dependencies(source))
        for name, source in sorted(libraries.items()):
            if name in BASE_LIBRARIES or name.startswith("libnss_"):
                continue
            target = libdir / name
            copy(source, target)
            record(source, target, appdir)
        # GTK's Adwaita CSS theme is embedded in libgtk-3. Bundle the icon theme,
        # schema database and data files that are looked up at runtime as well.
        data_trees = ["usr/share/glib-2.0/schemas", "usr/share/icons/Adwaita", "usr/share/alsa"]
        for relative in data_trees:
            source = system_path(relative, dependency_root)
            target = appdir / relative
            if relative.endswith("schemas"):
                target.mkdir(parents=True)
                for item in sorted(source.iterdir()):
                    if item.name.endswith((".xml", ".override")):
                        copy(item, target / item.name)
                        record(item, target / item.name, appdir)
                compiler = system_path(f"usr/lib/{MULTIARCH}/glib-2.0/glib-compile-schemas", dependency_root)
                run([compiler, target])
            else:
                shutil.copytree(source, target, symlinks=False)
                hint = owner(source / ("index.theme" if "icons" in relative else "alsa.conf"))
                for item in sorted(source.rglob("*")):
                    if item.is_file():
                        record(item, target / item.relative_to(source), appdir, hint)
        # Supply the PulseAudio defaults shipped with alsa-plugins, also useful
        # for PipeWire distributions providing the PulseAudio compatibility API.
        for name in ("50-pulseaudio.conf", "99-pulseaudio-default.conf.example"):
            source = system_path(f"usr/share/alsa/alsa.conf.d/{name}", dependency_root, False)
            if source:
                target = appdir / "usr/share/alsa/alsa.conf.d" / name
                copy(source, target)
                record(source, target, appdir)
        (appdir / "usr/share/alsa/tally-counter.conf.in").write_text(
            '<@APPDIR@/usr/share/alsa/alsa.conf>\n'
            'pcm.!default { type pulse fallback "sysdefault" }\n'
            'ctl.!default { type pulse fallback "sysdefault" }\n', encoding="utf-8")
        for name in ("DejaVuSans.ttf", "DejaVuSans-Bold.ttf", "DejaVuSansMono.ttf", "DejaVuSansMono-Bold.ttf"):
            source = system_path(f"usr/share/fonts/truetype/dejavu/{name}", dependency_root)
            target = appdir / "usr/share/fonts/truetype/dejavu" / name
            copy(source, target)
            record(source, target, appdir)
        fonts = appdir / "usr/etc/fonts/fonts.conf"
        fonts.parent.mkdir(parents=True)
        fonts.write_text('''<?xml version="1.0"?>
<!DOCTYPE fontconfig SYSTEM "fonts.dtd">
<fontconfig>
  <dir prefix="relative">../../share/fonts</dir>
  <include ignore_missing="yes">/etc/fonts/fonts.conf</include>
  <cachedir prefix="xdg">fontconfig</cachedir>
  <alias><family>sans-serif</family><prefer><family>DejaVu Sans</family></prefer></alias>
  <alias><family>monospace</family><prefer><family>DejaVu Sans Mono</family></prefer></alias>
</fontconfig>
''', encoding="utf-8")
        for relative in ("usr/share/icons/hicolor/index.theme",):
            source = system_path(relative, dependency_root)
            copy(source, appdir / relative)
            record(source, appdir / relative, appdir)
        copy(root / "assets/icon.png", appdir / "tally-counter.png")
        copy(root / "assets/icon.png", appdir / ".DirIcon")
        copy(root / "assets/icon.png", appdir / "usr/share/icons/hicolor/256x256/apps/tally-counter.png")
        copy(root / "assets/icon.svg", appdir / "usr/share/icons/hicolor/scalable/apps/tally-counter.svg")
        copy(root / "packaging/tally-counter.desktop", appdir / "tally-counter.desktop")
        copy(root / "packaging/tally-counter.desktop", appdir / "usr/share/applications/tally-counter.desktop")
        docs = appdir / "usr/share/doc/tally-counter"
        for name in ("LICENSE", "README.md", "THIRD-PARTY-NOTICES.txt", "CHANGELOG.md", "CONTRIBUTING.md", "SECURITY.md"):
            copy(root / name, docs / name)
        if (root / "docs").is_dir():
            shutil.copytree(root / "docs", docs / "docs")
        for item in sorted((root / "licenses").glob("*")):
            if item.is_file():
                copy(item, docs / "licenses" / item.name)
        env = os.environ.copy()
        env["LD_LIBRARY_PATH"] = str(libdir) + (":" + env["LD_LIBRARY_PATH"] if env.get("LD_LIBRARY_PATH") else "")
        query = system_path(f"usr/lib/{MULTIARCH}/gdk-pixbuf-2.0/gdk-pixbuf-query-loaders", dependency_root)
        loaders = sorted((appdir / "usr/lib/gdk-pixbuf-2.0/2.10.0/loaders").glob("*.so"))
        cache = text_output([query, *loaders], env=env).replace(str(appdir), "@APPDIR@")
        (appdir / "usr/lib/gdk-pixbuf-2.0/2.10.0/loaders.cache.in").write_text(cache, encoding="utf-8")
        if gio:
            (appdir / "usr/lib/gio/modules/giomodule.cache").write_text("libdconfsettings.so: gsettings-backend\n", encoding="utf-8")
        copyrights = metadata / "copyright"
        copyrights.mkdir()
        for name, package in sorted(packages.items()):
            base = name.split(":")[0]
            source = system_path(f"usr/share/doc/{base}/copyright", dependency_root)
            target = copyrights / f"{base}.txt"
            copy(source, target)
            package["copyright_file"] = f"copyright/{target.name}"
            package["source_package_url"] = "https://launchpad.net/ubuntu/+source/" + package["source_package"] + "/" + package["source_version"]
        versions = []
        for executable in [appdir / "AppRun", app, *libdir.rglob("*")]:
            if not executable.is_file() or executable.open("rb").read(4) != b"\x7fELF":
                continue
            info = text_output(["readelf", "--version-info", executable])
            versions.extend(tuple(int(part) for part in version.split("."))
                            for version in re.findall(r"Name: GLIBC_(\d+(?:\.\d+)+)", info))
        glibc_minimum = ".".join(str(part) for part in max(versions))
        if max(versions) > (2, 38):
            raise RuntimeError(f"Bundled libraries require glibc {glibc_minimum}; release baseline is 2.38")
        common_licenses = Path("/usr/share/common-licenses")
        if common_licenses.is_dir():
            shutil.copytree(common_licenses, metadata / "common-licenses", symlinks=False)
        runtime_license = root / "packaging/AppImage-runtime-LICENSE.txt"
        copy(runtime_license, metadata / "AppImage-runtime-LICENSE.txt")
        shutil.copytree(root / "packaging/appimage-runtime-licenses", metadata / "runtime-licenses")
        manifest = {
            "format_version": 1, "architecture": "x86_64", "glibc_minimum": glibc_minimum,
            "base_libraries_not_bundled": sorted(BASE_LIBRARIES),
            "appimage_runtime": {"version": TOOLS["runtime-x86_64"]["version"],
                "sha256": TOOLS["runtime-x86_64"]["sha256"],
                "source": "https://github.com/AppImage/type2-runtime/tree/dd6cebedcbddde9c82f89b011e8e1d40b6e43868"},
            "packages": [packages[name] for name in sorted(packages)],
            "files": [records[name] for name in sorted(records)],
        }
        (metadata / "BUNDLED-LIBRARIES.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
        shutil.copytree(metadata, docs / "appimage-provenance")
        for item in [appdir, *appdir.rglob("*")]:
            os.utime(item, (stamp, stamp), follow_symlinks=False)
        image_env = os.environ.copy()
        image_env.update({"ARCH": "x86_64", "SOURCE_DATE_EPOCH": str(stamp), "NO_APPSTREAM": "1"})
        run([toolroot / "AppRun", "--runtime-file", fetched["runtime-x86_64"],
             "--no-appstream", "--comp", "zstd", "--mksquashfs-opt", "-processors",
             "--mksquashfs-opt", "2", appdir, destination], env=image_env)
        destination.chmod(0o755)
    print(f"AppImage provenance: {metadata}", flush=True)
