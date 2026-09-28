"""Архив текущих исходников без локальных config, сборок, ключей и кешей.

Запуск из любого каталога: python tools/package-project.py
Использует tracked + untracked/non-ignored файлы Git, не изменяет индекс.
"""
from pathlib import Path, PurePosixPath
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / 'ble-lock-project.zip'
EXCLUDED_NAMES = {'sdkconfig', 'sdkconfig.old', 'local.properties', 'keystore.properties'}
EXCLUDED_DIRS = {'.git', 'build', 'out', '.gradle', '.gradle-codex', '.android', '.device-agent', '__pycache__'}
EXCLUDED_SUFFIXES = {'.zip', '.tmp', '.apk', '.aab', '.jks', '.keystore', '.p12', '.pyc'}


def source_files():
    names = subprocess.check_output(
        ['git', 'ls-files', '--cached', '--others', '--exclude-standard', '-z'], cwd=ROOT
    ).decode('utf-8').split('\0')
    for name in sorted(set(names)):
        path = PurePosixPath(name)
        if not name or path.name in EXCLUDED_NAMES or path.suffix.lower() in EXCLUDED_SUFFIXES:
            continue
        if any(part in EXCLUDED_DIRS for part in path.parts):
            continue
        local = ROOT / name
        if local.is_file() and not local.is_symlink() and local.resolve().is_relative_to(ROOT):
            yield local, path.as_posix()


def main():
    temporary = OUTPUT.with_suffix('.zip.tmp')
    try:
        with zipfile.ZipFile(temporary, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
            for local, name in source_files():
                archive.write(local, name)
        with zipfile.ZipFile(temporary) as archive:
            assert not any(PurePosixPath(n).name in EXCLUDED_NAMES for n in archive.namelist())
            assert archive.testzip() is None
            count = len(archive.namelist())
        temporary.replace(OUTPUT)
        print(f'{OUTPUT.name}: {count} files; generated configs excluded; CRC OK')
    finally:
        temporary.unlink(missing_ok=True)


if __name__ == '__main__':
    main()
