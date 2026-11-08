#!/usr/bin/env python3
"""Collect the exact Ubuntu source packages for every bundled AppImage package.

The original signed .dsc files and their complete source/patch archives are
included. Every archive is checked against Checksums-Sha256 in its .dsc.
No installation, package-manager configuration changes, or root is needed.
"""
# Copyright (c) 2026 davoda1. See LICENSE for the zlib license.
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path
import argparse
import hashlib
import json
import os
import subprocess
import tempfile
import urllib.error
import urllib.parse
import urllib.request

ROOT = Path(__file__).resolve().parents[1]


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def fields(text):
    result, key = {}, None
    for line in text.splitlines():
        if line.startswith("-----BEGIN PGP SIGNATURE-----"):
            break
        if line.startswith(" ") and key:
            result[key] += "\n" + line[1:]
        elif ": " in line or line.endswith(":"):
            key, value = line.split(":", 1)
            result[key] = value.lstrip()
    return result


def download(url, target, expected=None):
    if expected and target.is_file() and sha256(target) == expected:
        return sha256(target)
    target.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        request = urllib.request.Request(url, headers={"User-Agent": "tally-counter-release/1.0.1"})
        with urllib.request.urlopen(request, timeout=45) as response, tempfile.NamedTemporaryFile(
                prefix=target.name + ".", suffix=".partial", dir=target.parent, delete=False) as stream:
            temporary = Path(stream.name)
            for chunk in iter(lambda: response.read(1024 * 1024), b""):
                stream.write(chunk)
        digest = sha256(temporary)
        if expected and digest != expected:
            raise RuntimeError(f"Checksum mismatch for {url}")
        os.replace(temporary, target)
        return digest
    finally:
        if temporary:
            temporary.unlink(missing_ok=True)


def source_directory(package):
    data = subprocess.check_output(["apt-cache", "show", package["package"].split(":")[0]], text=True)
    for record in data.split("\n\n"):
        values = fields(record)
        candidate_source = values.get("Source", values.get("Package", "")).split(" (")[0]
        if candidate_source == package["source_package"] and "Filename" in values:
            return str(Path(values["Filename"]).parent)
    raise RuntimeError(f"Cannot find the Ubuntu source directory for {package['package']}")


def collect_package(package, output, mirrors):
    name, version = package["source_package"], package["source_version"]
    pool = source_directory(package)
    filename = name + "_" + version.split(":", 1)[-1] + ".dsc"
    target_dir = output / "ubuntu" / name
    dsc = target_dir / filename
    errors = []
    bases = [mirror.rstrip("/") + "/" + pool for mirror in mirrors]
    # Launchpad retains superseded source uploads when ordinary mirrors no longer do.
    bases.append("https://launchpad.net/ubuntu/+archive/primary/+sourcefiles/" + name + "/" + version)
    for base in bases:
        url = base + "/" + urllib.parse.quote(filename, safe="+")
        try:
            dsc_hash = download(url, dsc)
            control = fields(dsc.read_text(encoding="utf-8"))
            if control.get("Source") != name or control.get("Version") != version or "Checksums-Sha256" not in control:
                raise RuntimeError(f"Unexpected source control data at {url}")
            entries = [{"file": str(dsc.relative_to(output)), "sha256": dsc_hash, "url": url}]
            for line in control["Checksums-Sha256"].splitlines():
                if not line.strip():
                    continue
                expected, size, source_file = line.split()
                if Path(source_file).name != source_file:
                    raise RuntimeError(f"Unsafe source archive path in {dsc.name}")
                source_url = base + "/" + urllib.parse.quote(source_file, safe="+")
                target = target_dir / source_file
                digest = download(source_url, target, expected)
                if target.stat().st_size != int(size):
                    raise RuntimeError(f"Size mismatch for {target.name}")
                entries.append({"file": str(target.relative_to(output)), "sha256": digest, "url": source_url})
            print(f"Verified corresponding source: {name} {version}", flush=True)
            return {"source_package": name, "source_version": version, "files": entries}
        except (OSError, RuntimeError, ValueError) as error:
            errors.append(str(error))
    raise RuntimeError(f"Cannot collect {name} {version}: " + "; ".join(errors))


def collect(manifest, output, mirrors, jobs=6):
    output.mkdir(parents=True, exist_ok=True)
    dependencies = json.loads(manifest.read_text())
    unique = {}
    for package in dependencies["packages"]:
        unique[(package["source_package"], package["source_version"])] = package
    results, failures = [], []
    with ThreadPoolExecutor(max_workers=jobs) as pool:
        futures = {pool.submit(collect_package, package, output, mirrors): name for name, package in unique.items()}
        for future in as_completed(futures):
            try:
                results.append(future.result())
            except Exception as error:
                failures.append(str(error))
    if failures:
        raise RuntimeError("Incomplete corresponding sources:\n" + "\n".join(failures))
    manifest_out = output / "UBUNTU-SOURCES.json"
    manifest_out.write_text(json.dumps({
        "format_version": 1,
        "verification": "Original Ubuntu .dsc files retrieved over HTTPS. Every source archive SHA-256 and size verified against its .dsc; original .dsc signatures retained.",
        "packages": sorted(results, key=lambda item: (item["source_package"], item["source_version"])),
    }, indent=2) + "\n", encoding="utf-8")
    (output / "README.txt").write_text(
        "Corresponding source for the libraries and resources bundled in the AppImage.\n\n"
        "ubuntu/ contains the exact original Ubuntu source-control (.dsc), upstream\n"
        "source archives, Debian/Ubuntu patches and packaging/build instructions.\n"
        "Use dpkg-source -x PACKAGE.dsc to reconstruct the patched source tree.\n"
        "Consult each package's debian/copyright and build instructions.\n"
        "UBUNTU-SOURCES.json records exact versions, original locations and hashes.\n\n"
        "The application's dynamically linked libraries can be replaced after\n"
        "extracting the AppImage with --appimage-extract. The native AppRun source\n"
        "is in packaging/AppRun.c. Run the extracted AppRun to use modified libraries.\n",
        encoding="utf-8")
    print(f"Collected {len(results)} exact Ubuntu source packages in {output}", flush=True)


RUNTIME_SOURCES = (
    ("type2-runtime-dd6cebedcbddde9c82f89b011e8e1d40b6e43868.tar.gz",
     "https://api.github.com/repos/AppImage/type2-runtime/tarball/dd6cebedcbddde9c82f89b011e8e1d40b6e43868",
     "70129e551d44571a438b06640cd12ea3b6db67f31b142b607dcccf66d5dbc320"),
    ("fuse-3.15.0.tar.xz",
     "https://github.com/libfuse/libfuse/releases/download/fuse-3.15.0/fuse-3.15.0.tar.xz",
     "70589cfd5e1cff7ccd6ac91c86c01be340b227285c5e200baa284e401eea2ca0"),
    ("squashfuse-0.5.2.tar.gz",
     "https://codeload.github.com/vasi/squashfuse/tar.gz/refs/tags/0.5.2",
     "db0238c5981dabbd80ee09ae15387f390091668ca060a7bc38047912491443d3"),
)


def collect_runtime(output):
    entries = []
    for name, url, expected in RUNTIME_SOURCES:
        target = output / "runtime" / name
        digest = download(url, target, expected)
        entries.append({"file": str(target.relative_to(output)), "url": url, "sha256": digest})
    (output / "RUNTIME-SOURCES.json").write_text(json.dumps({
        "runtime_version": "20251108",
        "runtime_commit": "dd6cebedcbddde9c82f89b011e8e1d40b6e43868",
        "runtime_binary_sha256": "2fca8b443c92510f1483a883f60061ad09b46b978b2631c807cd873a47ec260d",
        "files": entries,
    }, indent=2) + "\n", encoding="utf-8")
    (output / "runtime/BUILDING.txt").write_text(
        "AppImage type-2 runtime 20251108 corresponding source\n"
        "Commit dd6cebedcbddde9c82f89b011e8e1d40b6e43868\n\n"
        "The runtime archive contains the entire source and upstream build scripts,\n"
        "including BUILD.md, src/runtime/Makefile, scripts/docker/Dockerfile and\n"
        "scripts/common/install-dependencies.sh. Follow BUILD.md to rebuild.\n"
        "The build uses Alpine 3.21, musl, zstd, zlib and mimalloc development\n"
        "packages, with libfuse 3.15.0 and squashfuse 0.5.2 built statically.\n"
        "The exact libfuse and squashfuse source archives are provided here. Their\n"
        "hashes match the pins in install-dependencies.sh. The libfuse modification\n"
        "is included in patches/libfuse/mount.c.diff in the runtime source archive.\n"
        "Use those supplied sources in the upstream build recipe, or modify the\n"
        "libfuse source and rebuild the complete runtime. The upstream script\n"
        "installs dependencies inside a build container, not on the host system.\n\n"
        "The upstream release does not record the exact Alpine binary-package\n"
        "revisions. Its original recipe is preserved rather than claiming bitwise\n"
        "reproducibility for those packages. Their licenses are permissive.\n"
        "Full runtime/library notices are in packaging/appimage-runtime-licenses.\n"
        "Pass a rebuilt runtime to appimagetool --runtime-file when recreating\n"
        "the image. The application and native AppRun remain separately buildable.\n",
        encoding="utf-8")
    print("Collected exact AppImage runtime, libfuse and squashfuse sources", flush=True)


def validate_sources(provenance, output):
    dependencies = json.loads((provenance / "BUNDLED-LIBRARIES.json").read_text())
    try:
        ubuntu = json.loads((output / "UBUNTU-SOURCES.json").read_text())
        runtime = json.loads((output / "RUNTIME-SOURCES.json").read_text())
    except FileNotFoundError as error:
        raise RuntimeError("Corresponding source is mandatory; run packaging/fetch_corresponding_source.py first") from error
    wanted = {(p["source_package"], p["source_version"]) for p in dependencies["packages"]}
    actual = {(p["source_package"], p["source_version"]) for p in ubuntu["packages"]}
    if wanted != actual:
        raise RuntimeError("Corresponding source versions do not match this AppImage")
    if runtime["runtime_binary_sha256"] != dependencies["appimage_runtime"]["sha256"]:
        raise RuntimeError("Corresponding runtime source does not match this AppImage")
    for entry in [f for package in ubuntu["packages"] for f in package["files"]] + runtime["files"]:
        path = output / entry["file"]
        if not path.is_file() or sha256(path) != entry["sha256"]:
            raise RuntimeError(f"Missing or corrupted corresponding-source file: {entry['file']}")
    if len(runtime["files"]) != len(RUNTIME_SOURCES):
        raise RuntimeError("Incomplete AppImage runtime corresponding source")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, default=ROOT / "build/appimage-provenance/BUNDLED-LIBRARIES.json")
    parser.add_argument("--output", type=Path, default=ROOT / "build/corresponding-source")
    parser.add_argument("--mirror", action="append", help="Ubuntu archive/snapshot base URL; may be repeated")
    parser.add_argument("--jobs", type=int, default=6)
    args = parser.parse_args()
    if not 1 <= args.jobs <= 16:
        parser.error("--jobs must be between 1 and 16")
    mirrors = args.mirror or ["https://archive.ubuntu.com/ubuntu", "https://security.ubuntu.com/ubuntu"]
    if any(not mirror.startswith("https://") for mirror in mirrors):
        parser.error("Source mirrors must use HTTPS")
    collect(args.manifest.resolve(), args.output.resolve(), mirrors, args.jobs)
    collect_runtime(args.output.resolve())
    validate_sources(args.manifest.resolve().parent, args.output.resolve())


if __name__ == "__main__":
    main()
