"""Публикует проверенный release APK; сохраняет последний app и последний ESP релиз."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import urllib.error
import urllib.parse
import urllib.request

from publish_esp32_release import list_releases

API = "https://api.github.com/repos/uah0/thekidlock"
RELEASES = "https://github.com/uah0/thekidlock/releases/download"


def app_code(tag):
    match = re.fullmatch(r"app-v1\.0\.([1-9][0-9]{0,9})", tag)
    return int(match[1]) if match and int(match[1]) <= 2_100_000_000 else None


def prune_app_releases(request, keep_id, keep_tag):
    keep_code = app_code(keep_tag)
    if keep_code is None:
        raise ValueError("Unexpected app tag")

    def check_current():
        items = [r for r in list_releases(request, API) if r["tag_name"].startswith("app-v")]
        published = [r for r in items if not r.get("draft") and not r.get("prerelease")]
        if not published or any(app_code(r["tag_name"]) is None for r in items):
            raise ValueError("Invalid app release set")
        newest = max(published, key=lambda r: app_code(r["tag_name"]))
        if newest["id"] != keep_id or newest["tag_name"] != keep_tag:
            raise ValueError("Newest app release changed; cleanup stopped")
        if any(app_code(r["tag_name"]) > keep_code for r in items):
            raise ValueError("Newer app draft exists; cleanup stopped")
        return items

    items = check_current()
    tags = [t for t in request(f"{API}/git/matching-refs/tags/app-v")
            if t["ref"].startswith("refs/tags/app-v")]
    if any(app_code(t["ref"].removeprefix("refs/tags/")) is None or
           app_code(t["ref"].removeprefix("refs/tags/")) > keep_code for t in tags):
        raise ValueError("Unexpected or newer app tag; cleanup stopped")
    removed = []
    for old in items:
        if old["id"] == keep_id:
            continue
        check_current()
        request(f"{API}/releases/{old['id']}", "DELETE")
        removed.append(old["tag_name"])
    for old in tags:
        tag = old["ref"].removeprefix("refs/tags/")
        if tag == keep_tag:
            continue
        check_current()
        request(f"{API}/git/refs/tags/{urllib.parse.quote(tag, safe='')}", "DELETE")
    remaining = check_current()
    if len(remaining) != 1:
        raise ValueError("App cleanup incomplete")
    return removed


def verify_apk(root, apk):
    tools = Path(r"C:\Dev\AndroidSdk\build-tools\36.0.0")
    java = Path(r"C:\Program Files\Android\Android Studio\jbr\bin\java.exe")
    signer = subprocess.run([str(java), "-jar", str(tools / "lib/apksigner.jar"),
                             "verify", "--verbose", "--print-certs", str(apk)],
                            capture_output=True, text=True, check=True).stdout
    certificates = re.findall(r"Signer #[0-9]+ certificate SHA-256 digest: ([0-9a-f]{64})", signer)
    if len(certificates) != 1:
        raise ValueError("Expected one verified APK signer")
    badging = subprocess.run([str(tools / "aapt.exe"), "dump", "badging", str(apk)],
                            capture_output=True, text=True, encoding="utf-8", check=True).stdout
    match = re.search(r"package: name='([^']+)' versionCode='([0-9]+)' versionName='([^']+)'", badging)
    if not match or match[1] != "com.blelock.app" or "application-debuggable" in badging:
        raise ValueError("Expected a non-debuggable TheKidLock release APK")
    number = int((root / "android_lock_beacon/BUILD_NUMBER").read_text().strip())
    if match[2] != str(number) or match[3] != f"1.0.{number}":
        raise ValueError("APK does not match the current build number")
    properties = dict(line.split("=", 1) for line in
                      (root / "android_lock_beacon/keystore.properties").read_text().splitlines()
                      if "=" in line and not line.startswith("#"))
    env = os.environ.copy()
    env["THEKIDLOCK_KEYSTORE_PASSWORD"] = properties["storePassword"]
    # The project creates this path relative to the app module, like Gradle file().
    key = (root / "android_lock_beacon/app" / properties["storeFile"]).resolve()
    certificate = subprocess.run([str(java.with_name("keytool.exe")), "-exportcert",
                                  "-keystore", str(key), "-alias", properties["keyAlias"],
                                  "-storepass:env", "THEKIDLOCK_KEYSTORE_PASSWORD"],
                                 env=env, capture_output=True, check=True).stdout
    if hashlib.sha256(certificate).hexdigest() != certificates[0]:
        raise ValueError("APK signer differs from the persistent release key")
    return number, certificates[0]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--notes-file", type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    apk = root / "android_lock_beacon/app/build/outputs/apk/release/app-release.apk"
    number, certificate = verify_apk(root, apk)
    tag = f"app-v1.0.{number}"
    notes = args.notes_file.read_text(encoding="utf-8").strip() if args.notes_file else \
        "Обновление приложения TheKidLock. Улучшения и исправления."
    if not notes or len(notes) > 8_000:
        raise ValueError("Release notes must contain 1..8000 characters")
    payload = apk.read_bytes()
    if not 0 < len(payload) <= 64 * 1024 * 1024:
        raise ValueError("Unexpected APK size")
    digest = hashlib.sha256(payload).hexdigest()
    metadata = {"schemaVersion": 1, "packageName": "com.blelock.app", "versionCode": number,
                "versionName": f"1.0.{number}", "bytes": len(payload), "sha256": digest,
                "certificateSha256": certificate, "apkUrl": f"{RELEASES}/{tag}/thekidlock.apk",
                "notes": notes}
    manifest = (json.dumps(metadata, ensure_ascii=False, indent=2) + "\n").encode()
    token = os.environ.get("GH_TOKEN")
    if not token:
        credential = subprocess.run(["git", "credential", "fill"], cwd=root,
                                    input="protocol=https\nhost=github.com\n\n", text=True,
                                    capture_output=True, check=True)
        token = dict(line.split("=", 1) for line in credential.stdout.splitlines() if "=" in line)["password"]

    def request(url, method="GET", data=None, binary=False):
        headers = {"Authorization": "Bearer " + token, "Accept": "application/vnd.github+json",
                   "X-GitHub-Api-Version": "2022-11-28"}
        if data is not None:
            headers["Content-Type"] = "application/octet-stream" if binary else "application/json"
            data = data if binary else json.dumps(data).encode()
        with urllib.request.urlopen(urllib.request.Request(url, headers=headers, method=method, data=data), timeout=120) as response:
            body = response.read()
            return json.loads(body) if body else None

    releases = list_releases(request, API)
    if any(app_code(r["tag_name"]) is not None and app_code(r["tag_name"]) > number for r in releases):
        raise ValueError("A newer app release already exists")
    release = next((r for r in releases if r["tag_name"] == tag), None)
    if release and not release["draft"]:
        raise ValueError("Published app versions are immutable; build a new version")
    if release is None:
        release = request(f"{API}/releases", "POST", {"tag_name": tag, "name": f"TheKidLock 1.0.{number}",
                          "draft": True, "prerelease": False, "make_latest": "false", "body": notes})
    for asset in release["assets"]:
        if asset["name"] in ("thekidlock.apk", "thekidlock-update.json"):
            request(f"{API}/releases/assets/{asset['id']}", "DELETE")
    upload = release["upload_url"].split("{")[0]
    for name, content in [("thekidlock.apk", payload), ("thekidlock-update.json", manifest)]:
        uploaded = request(upload + "?name=" + name, "POST", content, binary=True)
        if uploaded["size"] != len(content):
            raise ValueError("Uploaded asset size mismatch")
    request(f"{API}/releases/{release['id']}", "PATCH", {"draft": False, "prerelease": False,
                                                       "make_latest": "false", "body": notes})
    with urllib.request.urlopen(metadata["apkUrl"], timeout=120) as response:
        if hashlib.sha256(response.read()).hexdigest() != digest:
            raise ValueError("Public APK hash mismatch; cleanup stopped")
    with urllib.request.urlopen(f"{RELEASES}/{tag}/thekidlock-update.json", timeout=60) as response:
        if json.load(response) != metadata:
            raise ValueError("Public update metadata mismatch; cleanup stopped")
    if not request(f"{API}/releases/latest")["tag_name"].startswith("esp-v"):
        raise ValueError("Firmware global latest changed unexpectedly; cleanup stopped")
    removed = prune_app_releases(request, release["id"], tag)
    artifact = root / f"production/android-release-{number}"
    artifact.mkdir(parents=True, exist_ok=True)
    (artifact / "thekidlock.apk").write_bytes(payload)
    (artifact / "thekidlock-update.json").write_bytes(manifest)
    summary = {"tag": tag, "url": f"https://github.com/uah0/thekidlock/releases/tag/{tag}",
               "sha256": digest, "certificateSha256": certificate, "removed": removed}
    (artifact / "publication.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
    print(json.dumps(summary))


if __name__ == "__main__":
    main()
