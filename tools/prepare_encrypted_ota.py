"""Encrypt with Espressif's tool and verify the decrypted signed image locally."""
import hashlib
from pathlib import Path
import subprocess
import sys
from cryptography.hazmat.primitives import serialization


def main():
    root = Path(__file__).resolve().parent.parent
    build = Path(sys.argv[1]).resolve()
    tool = root / "managed_components/espressif__esp_encrypted_img/tools/esp_enc_img_gen.py"
    private_key = root / ".security/ota_decryption_key.pem"
    public_key = root / ".security/ota_encryption_public_key.pem"
    key = serialization.load_pem_private_key(private_key.read_bytes(), password=None)
    public_key.write_bytes(key.public_key().public_bytes(
        serialization.Encoding.PEM, serialization.PublicFormat.SubjectPublicKeyInfo))
    signed = build / "ble_lock_stage1_s3.bin"
    signing_public = build / "ota_signing_public_key.pem"
    subprocess.run([sys.executable, "-m", "espsecure", "extract_public_key", "--version", "2",
                    "--keyfile", str(root / ".security/ota_signing_key.pem"), str(signing_public)], check=True)
    subprocess.run([sys.executable, "-m", "espsecure", "verify_signature", "--version", "2",
                    "--keyfile", str(signing_public), str(signed)], check=True)
    output = build / "encrypted/ble_lock_stage1_s3.bin"
    output.parent.mkdir(exist_ok=True)
    subprocess.run([sys.executable, str(tool), "encrypt", str(signed), str(public_key), str(output)], check=True)
    verified = build / "encrypted/verification-plaintext.bin"
    try:
        subprocess.run([sys.executable, str(tool), "decrypt", str(output), str(private_key), str(verified)], check=True)
        if verified.read_bytes() != signed.read_bytes():
            raise ValueError("Encrypted OTA round-trip mismatch")
    finally:
        verified.unlink(missing_ok=True)
    print(f"Encrypted OTA: {output}; SHA256={hashlib.sha256(output.read_bytes()).hexdigest()}")


if __name__ == "__main__":
    main()
