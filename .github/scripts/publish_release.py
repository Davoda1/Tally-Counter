#!/usr/bin/env python3
"""Publish one verified tag, leaving a new release in draft until all assets verify.

Reruns reconcile the same tag and replace only its five expected asset names.
A remote tag mismatch or an unrelated existing asset stops publication.
"""
# Copyright (c) 2026 davoda1. See LICENSE for the zlib license.
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
VERSION = "1.0.0"
TAG = "v" + VERSION
EXPECTED = {
    f"Tally-Counter-{VERSION}-Windows-x64.zip",
    f"Tally-Counter-{VERSION}-Linux-x64.tar.gz",
    f"tally-counter_{VERSION}_amd64.deb",
    f"Tally-Counter-{VERSION}-x86_64.AppImage",
    f"Tally-Counter-{VERSION}-Source.tar.gz",
}


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def artifacts(directory: Path) -> dict[str, dict]:
    paths = list(directory.iterdir())
    require({p.name for p in paths} == EXPECTED and len(paths) == 5,
            "Expected exactly the five named release artifacts")
    require(all(p.is_file() and not p.is_symlink() for p in paths),
            "Release artifacts must be regular files")
    return {p.name: {"path": p, "size": p.stat().st_size, "sha256": sha256(p)} for p in paths}


class GitHub:
    def __init__(self, repository: str):
        require(re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", repository) is not None,
                "GITHUB_REPOSITORY must identify one owner/repository")
        self.repository = repository
        self.base = "repos/" + repository

    @staticmethod
    def command(*args: str) -> str:
        result = subprocess.run(args, capture_output=True, text=True, check=False)
        require(result.returncode == 0, result.stderr.strip() or "Command failed: " + args[0])
        return result.stdout

    def get(self, suffix: str, *, missing_ok: bool = False):
        result = subprocess.run(
            ["gh", "api", "--method", "GET", "--include", self.base + suffix],
            capture_output=True, text=True, check=False,
        )
        headers, separator, body = result.stdout.partition("\n\n")
        status = re.match(r"HTTP/\S+ (\d{3})(?: |$)", headers)
        require(bool(separator and status), result.stderr.strip() or "Missing GitHub HTTP response")
        code = int(status.group(1))
        if code == 404 and missing_ok:
            return None
        require(result.returncode == 0 and 200 <= code < 300,
                result.stderr.strip() or f"GitHub returned HTTP {code}")
        return json.loads(body)

    def tag_commit(self) -> str:
        obj = self.get("/git/ref/tags/" + TAG)["object"]
        for _ in range(10):
            if obj["type"] == "commit":
                return obj["sha"]
            require(obj["type"] == "tag", "Release tag does not resolve to a commit")
            obj = self.get("/git/tags/" + obj["sha"])["object"]
        raise RuntimeError("Release tag indirection exceeds the supported limit")

    def release(self):
        # The tag endpoint returns published releases only. The authenticated
        # list includes drafts for callers with push access, including this
        # workflow's contents:write token. Search every page so reruns can also
        # recover an older draft without accidentally creating a duplicate.
        matches = []
        page = 1
        while True:
            entries = self.get(f"/releases?per_page=100&page={page}")
            require(isinstance(entries, list), "GitHub returned an invalid release list")
            matches.extend(item for item in entries if item.get("tag_name") == TAG)
            require(len(matches) <= 1, "Multiple releases use the release tag; refusing an ambiguous update")
            if len(entries) < 100:
                return matches[0] if matches else None
            page += 1

    def assets(self, release: dict) -> list[dict]:
        # Six or more assets already fail the exact-five requirement. The first
        # 100 entries therefore suffice to detect any oversized asset set.
        return self.get(f"/releases/{release['id']}/assets?per_page=100")

    def create(self, commit: str, notes: Path) -> None:
        self.command("gh", "release", "create", TAG, "--repo", self.repository,
                     "--verify-tag", "--target", commit, "--draft", "--title",
                     "Tally Counter " + VERSION, "--notes-file", str(notes))

    def upload(self, files: dict[str, dict]) -> None:
        self.command("gh", "release", "upload", TAG, "--repo", self.repository,
                     "--clobber", *(str(files[name]["path"]) for name in sorted(files)))

    def download_sha256(self, name: str) -> str:
        # Older release assets can lack a server-provided digest.
        with tempfile.TemporaryDirectory(prefix="tally-release-verify-") as temporary:
            self.command("gh", "release", "download", TAG, "--repo", self.repository,
                         "--pattern", name, "--dir", temporary)
            return sha256(Path(temporary) / name)

    def publish(self, notes: Path) -> None:
        self.command("gh", "release", "edit", TAG, "--repo", self.repository,
                     "--draft=false", "--title", "Tally Counter " + VERSION,
                     "--notes-file", str(notes))


def check_identity(release: dict) -> None:
    require(release is not None and release.get("tag_name") == TAG,
            "Release metadata does not match the verified tag")
    require(isinstance(release.get("draft"), bool), "Release draft state is missing")


def check_asset_names(assets: list[dict], *, complete: bool) -> None:
    names = [asset["name"] for asset in assets]
    require(len(names) == len(set(names)), "Release contains duplicate asset names")
    require(set(names) <= EXPECTED,
            "Release has unrelated assets; refusing to replace or delete them")
    if complete:
        require(set(names) == EXPECTED, "Release is missing one or more required assets")


def verify_assets(client, release: dict, files: dict[str, dict]) -> None:
    uploaded = client.assets(release)
    check_asset_names(uploaded, complete=True)
    for asset in uploaded:
        expected = files[asset["name"]]
        require(asset.get("state") == "uploaded", f"Asset upload is incomplete: {asset['name']}")
        require(asset.get("size") == expected["size"], f"Asset size mismatch: {asset['name']}")
        digest = asset.get("digest")
        if digest is None:
            digest = "sha256:" + client.download_sha256(asset["name"])
        require(digest == "sha256:" + expected["sha256"], f"Asset digest mismatch: {asset['name']}")


def publish_release(client, commit: str, files: dict[str, dict], notes: Path) -> str:
    require(client.tag_commit() == commit, "Remote release tag differs from the tested commit")
    release = client.release()
    if release is None:
        client.create(commit, notes)
        release = client.release()
    check_identity(release)
    check_asset_names(client.assets(release), complete=False)
    client.upload(files)
    verify_assets(client, release, files)
    require(client.tag_commit() == commit, "Release tag moved during artifact upload")
    client.publish(notes)
    release = client.release()
    check_identity(release)
    require(not release["draft"], "Release is still a draft")
    verify_assets(client, release, files)
    return release["html_url"]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--directory", type=Path, default=ROOT / "dist")
    args = parser.parse_args()
    require(os.environ.get("GITHUB_REF") == "refs/tags/" + TAG,
            "Publication is restricted to the v1.0.0 tag")
    require(os.environ.get("GITHUB_EVENT_NAME") == "push", "Publication requires the tag push event")
    files = artifacts(args.directory.resolve())
    client = GitHub(os.environ.get("GITHUB_REPOSITORY", ""))
    commit = client.command("git", "rev-parse", "--verify", "HEAD^{commit}").strip()
    url = publish_release(client, commit, files, ROOT / ".github/RELEASE_NOTES.md")
    print("Verified and published all five release assets: " + url)


if __name__ == "__main__":
    main()
