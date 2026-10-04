#!/usr/bin/env python3
"""Apply the project-local, version-pinned NimBLE security-request guard."""

from pathlib import Path
import os
import re
import sys


EXPECTED_VERSION = (5, 4, 4)
PATCH_MARKER = "/* BLE_LOCK: preserve an already encrypted bonded link on Security Request. */"
OLD_BLOCK = """            authreq_mitm = cmd->authreq & BLE_SM_PAIR_AUTHREQ_MITM;
            if (authreq_mitm && !value_sec.authenticated) {
                res->app_status = BLE_HS_EREJECT;
            }
"""
NEW_BLOCK = """            authreq_mitm = cmd->authreq & BLE_SM_PAIR_AUTHREQ_MITM;
            if (authreq_mitm && !value_sec.authenticated) {
                struct ble_hs_conn *secured_conn;
                bool already_encrypted_bonded = false;

                ble_hs_lock();
                secured_conn = ble_hs_conn_find(conn_handle);
                if (secured_conn != NULL) {
                    already_encrypted_bonded =
                        secured_conn->bhc_sec_state.encrypted &&
                        secured_conn->bhc_sec_state.bonded &&
                        value_sec.ltk_present;
                }
                ble_hs_unlock();

                if (already_encrypted_bonded) {
                    /* BLE_LOCK: preserve an already encrypted bonded link on Security Request. */
                    res->out_of_order = 1;
                    return;
                }
                res->app_status = BLE_HS_EREJECT;
            }
"""


def main() -> int:
    idf_path = os.environ.get("IDF_PATH")
    if not idf_path:
        print("IDF_PATH is not set; run this tool through the project ESP-IDF script.", file=sys.stderr)
        return 2

    root = Path(idf_path)
    version_file = root / "tools" / "cmake" / "version.cmake"
    source = root / "components" / "bt" / "host" / "nimble" / "nimble" / "nimble" / "host" / "src" / "ble_sm.c"
    if not version_file.is_file() or not source.is_file():
        print(f"ESP-IDF NimBLE source not found under {root}", file=sys.stderr)
        return 2

    version_text = version_file.read_text(encoding="utf-8")
    version = tuple(int(value) for value in re.findall(r"set\(IDF_VERSION_(?:MAJOR|MINOR|PATCH)\s+(\d+)\)", version_text))
    if version != EXPECTED_VERSION:
        print(f"NimBLE guard supports ESP-IDF 5.4.4 only; found {version}", file=sys.stderr)
        return 2

    source_text = source.read_text(encoding="utf-8")
    if PATCH_MARKER in source_text:
        print("NimBLE Security Request guard already applied (ESP-IDF 5.4.4).")
        return 0
    if source_text.count(OLD_BLOCK) != 1:
        print("NimBLE source does not match the audited 5.4.4 block; refusing to patch.", file=sys.stderr)
        return 2

    source.write_text(source_text.replace(OLD_BLOCK, NEW_BLOCK, 1), encoding="utf-8", newline="")
    print(f"Applied NimBLE encrypted-bond Security Request guard to {source}.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
