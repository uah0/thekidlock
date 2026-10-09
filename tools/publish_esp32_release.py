"""Publish the OTA asset using the GitHub REST API; no gh dependency."""
import hashlib
import json
import os
from pathlib import Path
import sys
import subprocess
import tempfile
import urllib.error
import urllib.parse
import urllib.request


def main():
    tag, filename = sys.argv[1:]
    repo = os.environ["GITHUB_REPOSITORY"]
    if repo != "uah0/thekidlock" or not tag.startswith("esp-v"):
        raise ValueError("Repository or tag does not match the fixed OTA source")
    payload = Path(filename).read_bytes()
    if Path(filename).name != "ble_lock_stage1_s3.bin" or not 512 < len(payload) <= 0x1E0000 + 512:
        raise ValueError("Unexpected asset name or size")
    # Official Espressif RSA-3072 encrypted container header, not an ESP app image.
    if len(payload) <= 512 or payload[:4] != bytes.fromhex("cfb68807"):
        raise ValueError("Only Espressif pre-encrypted OTA containers may be published")
    if int.from_bytes(payload[404:408], "little") != len(payload) - 512:
        raise ValueError("Encrypted OTA container length mismatch")
    root = Path(__file__).resolve().parent.parent
    with tempfile.TemporaryDirectory(dir=root / ".security") as temporary:
        plaintext = Path(temporary) / "verified.bin"
        public_key = Path(temporary) / "signing-public.pem"
        tool = root / "managed_components/espressif__esp_encrypted_img/tools/esp_enc_img_gen.py"
        subprocess.run([sys.executable, str(tool), "decrypt", filename,
                        str(root / ".security/ota_decryption_key.pem"), str(plaintext)], check=True)
        subprocess.run([sys.executable, "-m", "espsecure", "extract_public_key", "--version", "2",
                        "--keyfile", str(root / ".security/ota_signing_key.pem"), str(public_key)], check=True)
        subprocess.run([sys.executable, "-m", "espsecure", "verify_signature", "--version", "2",
                        "--keyfile", str(public_key), str(plaintext)], check=True)
    token = os.environ["GH_TOKEN"]
    api = f"https://api.github.com/repos/{repo}"

    def request(url, method="GET", data=None, binary=False):
        headers = {"Authorization": f"Bearer {token}",
                   "Accept": "application/vnd.github+json",
                   "X-GitHub-Api-Version": "2022-11-28"}
        if data is not None:
            headers["Content-Type"] = "application/octet-stream" if binary else "application/json"
            data = data if binary else json.dumps(data).encode()
        with urllib.request.urlopen(urllib.request.Request(url, data=data, headers=headers, method=method), timeout=120) as response:
            body = response.read()
            return json.loads(body) if body else None

    try:
        release = request(f"{api}/releases/tags/{urllib.parse.quote(tag, safe='')}")
    except urllib.error.HTTPError as error:
        if error.code != 404:
            raise
        release = request(f"{api}/releases", "POST", {
            "tag_name": tag, "name": tag, "draft": True, "prerelease": False,
            "body": "Подписанный OTA-образ для существующей разметки ESP32-S3. Профиль signed-dev; аппаратный Secure Boot/Flash Encryption не включаются."})
    for asset in release["assets"]:
        if asset["name"] == Path(filename).name:
            request(f"{api}/releases/assets/{asset['id']}", "DELETE")
    upload = release["upload_url"].split("{")[0] + "?name=" + Path(filename).name
    asset = request(upload, "POST", payload, binary=True)
    if asset["size"] != len(payload):
        raise ValueError("Uploaded asset size mismatch")
    request(f"{api}/releases/{release['id']}", "PATCH", {
        "draft": False, "prerelease": False, "make_latest": "true"})
    # Android downloads without authentication, so validate the same public URL.
    url = f"https://github.com/{repo}/releases/latest/download/{Path(filename).name}"
    downloaded = hashlib.sha256()
    with urllib.request.urlopen(url, timeout=120) as response:
        while chunk := response.read(65536):
            downloaded.update(chunk)
    digest = downloaded.hexdigest()
    expected = hashlib.sha256(payload).hexdigest()
    if digest != expected:
        raise ValueError("Public latest asset hash mismatch")
    print(f"Published {url}; SHA256={expected}")


if __name__ == "__main__":
    main()
