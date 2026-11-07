#!/usr/bin/env python3
"""Fetch the pinned x86-64 AppImage tools, verifying every download before use."""
# Copyright (c) 2026 davoda1. See LICENSE for the zlib license.
from pathlib import Path
import argparse
import hashlib
import os
import tempfile
import urllib.request

TOOLS = {
    "appimagetool-x86_64.AppImage": {
        "version": "1.9.1",
        "url": "https://github.com/AppImage/appimagetool/releases/download/1.9.1/appimagetool-x86_64.AppImage",
        "api_url": "https://api.github.com/repos/AppImage/appimagetool/releases/assets/324406736",
        "sha256": "ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0",
    },
    "runtime-x86_64": {
        "version": "20251108",
        "url": "https://github.com/AppImage/type2-runtime/releases/download/20251108/runtime-x86_64",
        "api_url": "https://api.github.com/repos/AppImage/type2-runtime/releases/assets/326011592",
        "sha256": "2fca8b443c92510f1483a883f60061ad09b46b978b2631c807cd873a47ec260d",
    },
}


def digest(path):
    result = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(chunk)
    return result.hexdigest()


def fetch(directory):
    directory = Path(directory).resolve()
    directory.mkdir(parents=True, exist_ok=True)
    result = {}
    for name, pin in TOOLS.items():
        target = directory / name
        if target.exists():
            if digest(target) != pin["sha256"]:
                raise RuntimeError(f"Checksum mismatch in cached tool: {target}; remove it and retry")
        else:
            errors = []
            for url in (pin["url"], pin["api_url"]):
                temporary = None
                try:
                    request = urllib.request.Request(url, headers={
                        "Accept": "application/octet-stream", "User-Agent": "tally-counter-release/1.0.0",
                    })
                    with urllib.request.urlopen(request, timeout=45) as response, tempfile.NamedTemporaryFile(
                            prefix=name + ".", suffix=".part", dir=directory, delete=False) as stream:
                        temporary = Path(stream.name)
                        for chunk in iter(lambda: response.read(1024 * 1024), b""):
                            stream.write(chunk)
                    if digest(temporary) != pin["sha256"]:
                        raise RuntimeError(f"SHA-256 mismatch for {name} from {url}")
                    os.replace(temporary, target)
                    break
                except (OSError, ValueError, RuntimeError) as error:
                    errors.append(str(error))
                finally:
                    if temporary is not None:
                        temporary.unlink(missing_ok=True)
            else:
                raise RuntimeError(f"Cannot obtain verified {name}: " + "; ".join(errors))
        target.chmod(0o755)
        print(f"Verified {name} ({pin['version']}): {pin['sha256']}", flush=True)
        result[name] = target
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=Path(__file__).resolve().parents[1] / "build/appimage-tools")
    args = parser.parse_args()
    fetch(args.output)


if __name__ == "__main__":
    main()
