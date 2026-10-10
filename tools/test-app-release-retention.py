"""Проверяет независимую очистку APK-релизов без обращения к GitHub."""
import unittest
from publish_android_release import API, app_code, prune_app_releases


class Fixture:
    def __init__(self):
        self.releases = [{"id": 1, "tag_name": "esp-v10.0.453"},
                         {"id": 2, "tag_name": "app-v1.0.287"},
                         {"id": 3, "tag_name": "app-v1.0.288"}]
        self.tags = ["refs/tags/" + r["tag_name"] for r in self.releases]
        self.deleted = []
        self.concurrent = False

    def request(self, url, method="GET"):
        path = url.removeprefix(API)
        if method == "DELETE":
            self.deleted.append(path)
            if path.startswith("/releases/"):
                self.releases = [r for r in self.releases if r["id"] != int(path.rsplit("/", 1)[1])]
            else:
                self.tags.remove("refs/tags/" + path.rsplit("/", 1)[1])
            if self.concurrent:
                self.concurrent = False
                self.releases.append({"id": 4, "tag_name": "app-v1.0.289"})
            return None
        if path.startswith("/releases?"):
            page = int(path.rsplit("page=", 1)[1])
            return self.releases[(page - 1) * 100:page * 100]
        if path.startswith("/git/matching-refs/tags/"):
            return [{"ref": t} for t in self.tags]
        raise AssertionError(path)


class Tests(unittest.TestCase):
    def test_keeps_firmware_and_current_app(self):
        fixture = Fixture()
        prune_app_releases(fixture.request, 3, "app-v1.0.288")
        self.assertEqual({r["id"] for r in fixture.releases}, {1, 3})
        self.assertEqual(set(fixture.tags), {"refs/tags/esp-v10.0.453", "refs/tags/app-v1.0.288"})

    def test_wrong_current_is_rejected_before_deletion(self):
        fixture = Fixture()
        with self.assertRaises(ValueError):
            prune_app_releases(fixture.request, 2, "app-v1.0.287")
        self.assertEqual(fixture.deleted, [])

    def test_newer_or_malformed_tag_blocks_deletion(self):
        for tag in ["app-v1.0.289", "app-vbad"]:
            fixture = Fixture()
            fixture.tags.append("refs/tags/" + tag)
            with self.assertRaises(ValueError):
                prune_app_releases(fixture.request, 3, "app-v1.0.288")
            self.assertEqual(fixture.deleted, [])

    def test_concurrent_publication_stops_cleanup(self):
        fixture = Fixture()
        fixture.concurrent = True
        with self.assertRaises(ValueError):
            prune_app_releases(fixture.request, 3, "app-v1.0.288")
        self.assertEqual(len(fixture.deleted), 1)

    def test_draft_and_orphan_app_tag_are_removed(self):
        fixture = Fixture()
        fixture.releases.append({"id": 4, "tag_name": "app-v1.0.288", "draft": True})
        fixture.tags.append("refs/tags/app-v1.0.286")
        prune_app_releases(fixture.request, 3, "app-v1.0.288")
        self.assertEqual({r["id"] for r in fixture.releases}, {1, 3})

    def test_only_valid_app_tags_are_parsed(self):
        self.assertEqual(app_code("app-v1.0.288"), 288)
        for tag in ["esp-v10.0.453", "app-v1.0.0", "app-v1.0.01", "app-v1.0.2100000001", "app-v1.0.288/x"]:
            self.assertIsNone(app_code(tag))


if __name__ == "__main__":
    unittest.main()
