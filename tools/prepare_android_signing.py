"""Однократно создаёт постоянный ключ Android; секреты остаются вне Git."""
from pathlib import Path
import os
import secrets
import subprocess


def main():
    root = Path(__file__).resolve().parent.parent
    properties = root / "android_lock_beacon/keystore.properties"
    key = root / ".security/android-release.jks"
    if properties.exists():
        print("Существующая подпись сохранена: keystore.properties")
        return
    if key.exists():
        raise RuntimeError("Ключ уже существует; восстановите keystore.properties из резервной копии")
    key.parent.mkdir(exist_ok=True)
    password = secrets.token_hex(32)
    env = os.environ.copy()
    env["THEKIDLOCK_KEYSTORE_PASSWORD"] = password
    keytool = Path(r"C:\Program Files\Android\Android Studio\jbr\bin\keytool.exe")
    subprocess.run([str(keytool), "-genkeypair", "-keystore", str(key),
                    "-storetype", "JKS", "-alias", "thekidlock", "-keyalg", "RSA",
                    "-keysize", "3072", "-sigalg", "SHA256withRSA", "-validity", "10000",
                    "-dname", "CN=TheKidLock", "-storepass:env", "THEKIDLOCK_KEYSTORE_PASSWORD",
                    "-keypass:env", "THEKIDLOCK_KEYSTORE_PASSWORD", "-noprompt"],
                   env=env, check=True, capture_output=True)
    properties.write_text("storeFile=../../.security/android-release.jks\n"
                          f"storePassword={password}\nkeyAlias=thekidlock\nkeyPassword={password}\n",
                          encoding="utf-8")
    print("Создан постоянный ключ .security/android-release.jks; сохраните ключ и keystore.properties")


if __name__ == "__main__":
    main()
