"""Проверки удаления старых GitHub релизов без сетевых запросов."""
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location("publisher", Path(__file__).with_name("publish_esp32_release.py"))
publisher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(publisher)
API = "https://api.github.com/repos/uah0/thekidlock"
TAG = "esp-v10.0.442"


class GitHubFixture:
    def __init__(self):
        self.releases = [{"id": 3, "tag_name": TAG}, {"id": 2, "tag_name": TAG, "draft": True},
                         {"id": 1, "tag_name": "esp-v10.0.440"}]
        self.tags = ["refs/tags/" + tag for tag in [TAG, "esp-v10.0.440", "esp-v10.0.395"]]
        self.deleted = []
        self.change_latest = False

    def request(self, url, method="GET"):
        path = url.removeprefix(API)
        if method == "DELETE":
            self.deleted.append(path)
            if path.startswith("/releases/"):
                self.releases = [r for r in self.releases if r["id"] != int(path.rsplit("/", 1)[1])]
            else:
                self.tags.remove("refs/tags/" + path.rsplit("/", 1)[1])
            return None
        if path == "/releases/latest":
            return {"id": 4 if self.change_latest and self.deleted else 3, "tag_name": TAG}
        if path.startswith("/releases?"):
            page = int(path.split("page=")[-1])
            return self.releases[(page - 1) * 100:page * 100]
        if path.startswith("/git/matching-refs/tags/"):
            return [{"ref": t} for t in self.tags]
        raise AssertionError(url)


class RetentionTests(unittest.TestCase):
    def test_removes_old_releases_duplicate_draft_and_orphan_tags(self):
        fixture = GitHubFixture()
        result = publisher.prune_old_releases(fixture.request, API, 3, TAG)
        self.assertEqual([r["id"] for r in fixture.releases], [3])
        self.assertEqual(fixture.tags, ["refs/tags/" + TAG])
        self.assertEqual(len(result["removed_releases"]), 2)
        self.assertNotIn("/releases/3", fixture.deleted)

    def test_latest_mismatch_prevents_any_deletion(self):
        fixture = GitHubFixture()
        with self.assertRaises(ValueError):
            publisher.prune_old_releases(fixture.request, API, 99, TAG)
        self.assertEqual(fixture.deleted, [])

    def test_newer_version_prevents_any_deletion(self):
        fixture = GitHubFixture()
        fixture.tags.append("refs/tags/esp-v10.0.443")
        with self.assertRaises(ValueError):
            publisher.prune_old_releases(fixture.request, API, 3, TAG)
        self.assertEqual(fixture.deleted, [])

    def test_concurrent_publication_stops_further_deletions(self):
        fixture = GitHubFixture()
        fixture.change_latest = True
        with self.assertRaises(ValueError):
            publisher.prune_old_releases(fixture.request, API, 3, TAG)
        self.assertEqual(len(fixture.deleted), 1)
        self.assertNotIn("/releases/3", fixture.deleted)

    def test_pagination_is_collected_before_deletion(self):
        fixture = GitHubFixture()
        fixture.releases.extend({"id": i, "tag_name": "esp-v10.0.399"} for i in range(10, 115))
        publisher.prune_old_releases(fixture.request, API, 3, TAG)
        self.assertEqual([r["id"] for r in fixture.releases], [3])

    def test_other_repository_is_rejected(self):
        fixture = GitHubFixture()
        with self.assertRaises(ValueError):
            publisher.prune_old_releases(fixture.request, "https://api.github.com/repos/other/repo", 3, TAG)
        self.assertEqual(fixture.deleted, [])

    def test_app_releases_and_tags_are_preserved(self):
        fixture = GitHubFixture()
        fixture.releases.append({"id": 500, "tag_name": "app-v1.0.287"})
        fixture.tags.append("refs/tags/app-v1.0.287")
        publisher.prune_old_releases(fixture.request, API, 3, TAG)
        self.assertEqual({r["id"] for r in fixture.releases}, {3, 500})
        self.assertIn("refs/tags/app-v1.0.287", fixture.tags)
        self.assertNotIn("/releases/500", fixture.deleted)


if __name__ == "__main__":
    unittest.main()
