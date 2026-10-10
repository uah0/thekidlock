"""Offline factory preparation. Never connects to hardware or writes eFuses."""
import argparse
import base64
import csv
import hashlib
import json
from pathlib import Path
import subprocess
import sys


def require(condition, message):
    if not condition:
        raise ValueError(message)


def read_nvs(data, sdk):
    # Use Espressif's parser; reconstruct indexed multi-page blobs without truncation.
    sys.path.insert(0, str(sdk / "components/nvs_flash/nvs_partition_tool"))
    from nvs_parser import NVS_Partition
    partition = NVS_Partition("snapshot", bytearray(data))
    entries = []
    for page in partition.pages:
        if page.is_empty:
            continue
        require(page.header["crc"]["original"] == page.header["crc"]["computed"], "NVS page CRC mismatch")
        for entry in page.entries:
            if entry.state != "Written":
                continue
            crc = entry.metadata["crc"]
            require(crc["original"] == crc["computed"], "NVS entry CRC mismatch")
            if entry.children:
                require(crc["data_original"] == crc["data_computed"], "NVS data CRC mismatch")
            entries.append(entry)
    namespaces = {e.data["value"]: e.key for e in entries if e.metadata["namespace"] == 0}
    grouped = {}
    for entry in entries:
        ns = entry.metadata["namespace"]
        if ns:
            require(ns in namespaces, "Unknown NVS namespace")
            grouped.setdefault((namespaces[ns], entry.key), []).append(entry)
    def variable(entry):
        return b"".join(bytes(child.raw) for child in entry.children)[:entry.data["size"]]
    result = {}
    for key, group in grouped.items():
        indexes = [e for e in group if e.metadata["type"] == "blob_index"]
        if indexes:
            require(len(indexes) == 1, "Ambiguous NVS blob index")
            index = indexes[0].data
            chunks = {e.metadata["chunk_index"]: variable(e) for e in group if e.metadata["type"] == "blob_data"}
            ids = range(index["chunk_start"], index["chunk_start"] + index["chunk_count"])
            require(all(i in chunks for i in ids), "Missing NVS blob chunk")
            value = b"".join(chunks[i] for i in ids)
            require(len(value) == index["size"], "NVS blob length mismatch")
            result[key] = ("binary", value)
        else:
            require(len(group) == 1, "Ambiguous duplicate NVS value")
            entry = group[0]
            kind = entry.metadata["type"]
            if kind == "string":
                result[key] = ("string", variable(entry).rstrip(b"\0").decode("utf8"))
            elif kind == "blob":
                result[key] = ("binary", variable(entry))
            else:
                require(kind.endswith("_t") and kind.startswith(("uint", "int")), "Unsupported NVS type")
                encoding = kind.replace("uint", "u").replace("int", "i").removesuffix("_t")
                result[key] = (encoding, entry.data["value"])
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--snapshot", type=Path, required=True)
    parser.add_argument("--idf", type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    build, snapshot, sdk = args.build.resolve(), args.snapshot.resolve(), args.idf.resolve()
    require(snapshot.is_relative_to(root / ".security"), "Factory data must stay under .security")
    config = (build / "sdkconfig").read_text()
    for setting in ["CONFIG_SECURE_BOOT=y", "CONFIG_SECURE_FLASH_ENCRYPTION_MODE_RELEASE=y",
                    "CONFIG_NVS_SEC_KEY_PROTECT_USING_HMAC=y",
                    "CONFIG_NVS_SEC_HMAC_EFUSE_KEY_ID=2", "CONFIG_PARTITION_TABLE_OFFSET=0x8000"]:
        require(setting in config.splitlines(), "Required factory configuration missing: " + setting)
    final = "CONFIG_SECURE_DISABLE_ROM_DL_MODE=y" in config.splitlines()
    require(final or "CONFIG_SECURE_ENABLE_SECURE_ROM_DL_MODE=y" in config.splitlines(), "Unsafe ROM Download configuration")
    fuses = json.loads((snapshot / "efuses-before.json").read_text())
    for i in range(3):
        block = fuses["BLOCK_KEY" + str(i)]
        zero = str(block["value"]).replace(" ", "").replace("0x", "")
        require(zero and set(zero) <= {"0"} and block["readable"] and block["writeable"], "Key block is not empty/writable")
        purpose = fuses["KEY_PURPOSE_" + str(i)]
        require(purpose["value"] == "USER" and purpose["writeable"], "Key purpose is already allocated/locked")
    require(not fuses["SECURE_BOOT_EN"]["value"] and not fuses["DIS_DOWNLOAD_MODE"]["value"], "Device snapshot already protected")
    backup = (snapshot / "flash-before.bin").read_bytes()
    require(len(backup) == 0x400000, "Unexpected flash size")
    table = (build / "partition_table/partition-table.bin").read_bytes()
    require(backup[0x8000:0x8000 + len(table)] == table, "Partition table differs from device")
    loader = (build / "bootloader/bootloader.bin").read_bytes()
    app = (build / "ble_lock_stage1_s3.bin").read_bytes()
    require(len(loader) <= 0x8000 and len(app) <= 0x1E0000, "Image does not fit existing layout")
    output = snapshot / ("prepared-final" if final else "prepared-test")
    output.mkdir(exist_ok=True)
    def run(*command):
        with (output / "offline-tools.log").open("ab") as log:
            subprocess.run([sys.executable, *map(str, command)], check=True, stdout=log, stderr=log)
    flash_key, hmac_key = snapshot / "flash-key.bin", snapshot / "nvs-hmac-key.bin"
    for key in (flash_key, hmac_key):
        if not key.exists():
            run("-m", "espsecure", "generate_flash_encryption_key", "--keylen", "256", key)
        require(key.stat().st_size == 32, "Invalid key length")
    public = output / "signing-public.pem"
    run("-m", "espsecure", "extract_public_key", "--version", "2", "--keyfile", root / ".security/ota_signing_key.pem", public)
    for image in (build / "bootloader/bootloader.bin", build / "ble_lock_stage1_s3.bin"):
        run("-m", "espsecure", "verify_signature", "--version", "2", "--keyfile", public, image)
    run("-m", "espsecure", "digest_sbv2_public_key", "--keyfile", public, "--output", output / "secure-boot-digest.bin")
    original = read_nvs(backup[0x9000:0xF000], sdk)
    source = output / "nvs.csv"
    with source.open("w", newline="", encoding="utf8") as stream:
        writer = csv.writer(stream)
        writer.writerow(["key", "type", "encoding", "value"])
        for ns in sorted({key[0] for key in original}):
            writer.writerow([ns, "namespace", "", ""])
            for (namespace, key), (kind, value) in sorted(original.items()):
                if namespace == ns:
                    writer.writerow([key, "data", "base64" if kind == "binary" else kind,
                                     base64.b64encode(value).decode() if kind == "binary" else value])
    keys = output / "nvs-xts-keys.bin"
    run("-m", "esp_idf_nvs_partition_gen", "generate-key", "--key_protect_hmac", "--kp_hmac_inputkey", hmac_key,
        "--keyfile", keys, "--outdir", output)
    encrypted_nvs = output / "nvs-encrypted.bin"
    run("-m", "esp_idf_nvs_partition_gen", "encrypt", source, encrypted_nvs, "0x6000", "--inputkey", keys, "--outdir", output)
    decrypted_nvs = output / "nvs-roundtrip.bin"
    run("-m", "esp_idf_nvs_partition_gen", "decrypt", encrypted_nvs, keys, decrypted_nvs, "--outdir", output)
    require(read_nvs(decrypted_nvs.read_bytes(), sdk) == original, "NVS values changed during migration")
    factory = bytearray(b"\xff" * len(backup))
    factory[:len(loader)] = loader
    factory[0x8000:0x8000 + len(table)] = table
    factory[0x9000:0xF000] = encrypted_nvs.read_bytes()
    factory[0xF000:0x12000] = backup[0xF000:0x12000]
    for address in (0x20000, 0x200000):
        factory[address:address + len(app)] = app
    for start, end in [(0, 0x9000), (0xF000, 0x11000), (0x20000, 0x200000), (0x200000, 0x3E0000)]:
        plain, cipher, verified = [output / f"range-{start:x}-{name}.bin" for name in ("plain", "cipher", "verified")]
        plain.write_bytes(factory[start:end])
        run("-m", "espsecure", "encrypt_flash_data", "--aes_xts", "--address", hex(start), "--keyfile", flash_key, "--output", cipher, plain)
        run("-m", "espsecure", "decrypt_flash_data", "--aes_xts", "--address", hex(start), "--keyfile", flash_key, "--output", verified, cipher)
        require(verified.read_bytes() == plain.read_bytes(), "Flash encryption roundtrip mismatch")
        factory[start:end] = cipher.read_bytes()
        verified.unlink()
    image = output / "factory-encrypted-flash.bin"
    image.write_bytes(factory)
    build_header = (build / "generated/app_build_number.h").read_text()
    import re
    number = re.search(r'APP_BUILD_NUMBER_STRING\s+"(\d+)"', build_header)
    require(number is not None, "Build number metadata missing")
    version = "10.0." + number.group(1)
    require(version.encode() in app, "Build number differs from compiled application")
    report = dict(prepared_only=True, hardware_applied=False, stage="final" if final else "hardware-test", bootloader_bytes=len(loader), app_bytes=len(app),
                  firmware_version=version,
                  partition_table_offset="0x8000", nvs_values_preserved=len(original), flash_bytes=len(factory),
                  sha256=hashlib.sha256(factory).hexdigest(), key_slots={"flash_xts_aes128":0, "secure_boot_digest0":1, "nvs_hmac_up":2})
    (output / "manifest.json").write_text(json.dumps(report, indent=2))
    print(json.dumps(report))


if __name__ == "__main__":
    main()
