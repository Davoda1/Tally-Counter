#!/usr/bin/env python3
"""Test release recovery and fail-closed publication without network access."""
# Copyright (c) 2026 davoda1. See LICENSE for the zlib license.
from __future__ import annotations

import copy
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("publish_release", ROOT / ".github/scripts/publish_release.py")
release = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(release)
COMMIT = "a" * 40


class FakeGitHub:
    def __init__(self, files, *, exists=False, draft=True):
        self.files = files
        self.metadata = {"id": 1, "tag_name": release.TAG, "draft": draft,
                         "html_url": "https://example.invalid/release"} if exists else None
        self.uploaded = []
        self.mutations = []
        self.commit = COMMIT
        self.move_tag = False
        self.fail_upload = False
        self.corrupt = False
        self.omit_asset = False
        self.omit_digest = False
        self.downloads = []

    def tag_commit(self):
        return "b" * 40 if self.move_tag and "upload" in self.mutations else self.commit

    def release(self):
        return copy.deepcopy(self.metadata)

    def assets(self, metadata):
        return copy.deepcopy(self.uploaded)

    def create(self, commit, notes):
        self.mutations.append("create draft")
        self.metadata = {"id": 1, "tag_name": release.TAG, "draft": True,
                         "html_url": "https://example.invalid/release"}

    def upload(self, files):
        self.mutations.append("upload")
        if self.fail_upload:
            raise RuntimeError("Interrupted upload")
        self.uploaded = [{"name": name, "size": item["size"], "state": "uploaded",
                          "digest": None if self.omit_digest else "sha256:" + item["sha256"]}
                         for name, item in files.items()]
        if self.corrupt:
            self.uploaded[0]["digest"] = "sha256:" + "0" * 64
        if self.omit_asset:
            self.uploaded.pop()

    def download_sha256(self, name):
        self.downloads.append(name)
        return self.files[name]["sha256"]

    def publish(self, notes):
        self.mutations.append("publish")
        self.metadata["draft"] = False


class ReleaseFlowTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        directory = Path(self.temporary.name)
        for name in release.EXPECTED:
            (directory / name).write_bytes(name.encode())
        self.files = release.artifacts(directory)
        self.notes = ROOT / ".github/RELEASE_NOTES.md"

    def run_flow(self, client):
        return release.publish_release(client, COMMIT, self.files, self.notes)

    def test_new_release_publishes_only_after_verified_uploads(self):
        client = FakeGitHub(self.files)
        self.assertEqual(self.run_flow(client), "https://example.invalid/release")
        self.assertEqual(client.mutations, ["create draft", "upload", "publish"])
        self.assertFalse(client.metadata["draft"])

    def test_published_release_rerun_refreshes_same_names(self):
        client = FakeGitHub(self.files, exists=True, draft=False)
        self.run_flow(client)
        self.run_flow(client)
        self.assertEqual(client.mutations, ["upload", "publish", "upload", "publish"])
        self.assertEqual({a["name"] for a in client.uploaded}, release.EXPECTED)

    def test_partial_draft_is_recovered(self):
        client = FakeGitHub(self.files, exists=True)
        client.uploaded = [{"name": next(iter(release.EXPECTED))}]
        self.run_flow(client)
        self.assertEqual(client.mutations, ["upload", "publish"])

    def test_unrelated_assets_are_never_removed(self):
        client = FakeGitHub(self.files, exists=True)
        client.uploaded = [{"name": "unrelated.txt"}]
        with self.assertRaisesRegex(RuntimeError, "unrelated assets"):
            self.run_flow(client)
        self.assertEqual(client.mutations, [])

    def test_changed_tag_prevents_any_mutation(self):
        client = FakeGitHub(self.files)
        client.commit = "b" * 40
        with self.assertRaisesRegex(RuntimeError, "differs from the tested commit"):
            self.run_flow(client)
        self.assertEqual(client.mutations, [])

    def test_tag_moved_during_upload_keeps_new_release_draft(self):
        client = FakeGitHub(self.files)
        client.move_tag = True
        with self.assertRaisesRegex(RuntimeError, "moved during"):
            self.run_flow(client)
        self.assertTrue(client.metadata["draft"])
        self.assertNotIn("publish", client.mutations)

    def test_failed_upload_keeps_new_release_draft(self):
        client = FakeGitHub(self.files)
        client.fail_upload = True
        with self.assertRaisesRegex(RuntimeError, "Interrupted upload"):
            self.run_flow(client)
        self.assertTrue(client.metadata["draft"])

    def test_missing_or_corrupt_asset_prevents_publication(self):
        for flag in ("corrupt", "omit_asset"):
            with self.subTest(flag=flag):
                client = FakeGitHub(self.files)
                setattr(client, flag, True)
                with self.assertRaises(RuntimeError):
                    self.run_flow(client)
                self.assertTrue(client.metadata["draft"])
                self.assertNotIn("publish", client.mutations)

    def test_missing_server_digest_is_verified_by_download(self):
        client = FakeGitHub(self.files)
        client.omit_digest = True
        self.run_flow(client)
        self.assertEqual(set(client.downloads), release.EXPECTED)

    def test_local_extra_file_is_rejected(self):
        directory = Path(self.temporary.name)
        (directory / "extra.txt").write_text("unexpected")
        with self.assertRaisesRegex(RuntimeError, "exactly the five"):
            release.artifacts(directory)

    def test_missing_release_is_distinct_from_api_failure(self):
        client = release.GitHub("owner/repo")
        response = subprocess.CompletedProcess([], 0, 'HTTP/2.0 200 OK\ncontent-type: application/json\n\n[]', '')
        with patch.object(release.subprocess, "run", return_value=response):
            self.assertIsNone(client.release())
        for code in (403, 404):
            response = subprocess.CompletedProcess([], 1, f'HTTP/2.0 {code} Error\n\n{{"message":"Error"}}', f'gh: Error (HTTP {code})')
            with patch.object(release.subprocess, "run", return_value=response):
                with self.assertRaisesRegex(RuntimeError, str(code)):
                    client.release()

    def test_authenticated_release_list_finds_draft_on_later_page(self):
        client = release.GitHub("owner/repo")
        first_page = [{"tag_name": f"other-{index}", "draft": False} for index in range(100)]
        draft = {"id": 7, "tag_name": release.TAG, "draft": True}
        with patch.object(client, "get", side_effect=[first_page, [draft]]) as get:
            self.assertEqual(client.release(), draft)
            self.assertEqual([call.args[0] for call in get.call_args_list],
                             ["/releases?per_page=100&page=1", "/releases?per_page=100&page=2"])

    def test_duplicate_tag_releases_are_rejected(self):
        client = release.GitHub("owner/repo")
        entries = [{"id": 1, "tag_name": release.TAG, "draft": False},
                   {"id": 2, "tag_name": release.TAG, "draft": True}]
        with patch.object(client, "get", return_value=entries):
            with self.assertRaisesRegex(RuntimeError, "ambiguous update"):
                client.release()


if __name__ == "__main__":
    unittest.main()
