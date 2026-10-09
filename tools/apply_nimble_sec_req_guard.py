#!/usr/bin/env python3
"""Apply BLE security guards and remove retired GAP-name backports for IDF 5.4.4."""

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

PAIR_GATE = """/* BLE_LOCK: central pairing requires the project's registration window. */
__attribute__((weak)) int
ble_lock_pairing_allowed(uint16_t conn_handle)
{
    (void)conn_handle;
    return 1;
}

"""


def apply_pairing_gate(source_text: str) -> str:
    marker = "/* BLE_LOCK: central pairing requires the project's registration window. */"
    if marker in source_text:
        return source_text
    anchors = (
        ("void\nble_sm_process_result(uint16_t conn_handle, struct ble_sm_result *res,",
         PAIR_GATE + "void\nble_sm_process_result(uint16_t conn_handle, struct ble_sm_result *res,"),
        ("if (res->app_status == BLE_HS_HCI_ERR(BLE_ERR_PINKEY_MISSING)) {",
         "if (res->app_status == BLE_HS_HCI_ERR(BLE_ERR_PINKEY_MISSING) &&\n"
         "            ble_lock_pairing_allowed(conn_handle)) {"),
        ("ble_sm_pair_initiate(uint16_t conn_handle)\n{",
         "ble_sm_pair_initiate(uint16_t conn_handle)\n{\n"
         "    if (!ble_lock_pairing_allowed(conn_handle)) {\n"
         "        return BLE_HS_EREJECT;\n"
         "    }\n"),
    )
    for old, new in anchors:
        if source_text.count(old) != 1:
            raise ValueError("NimBLE pairing source does not match audited 5.4.4")
        source_text = source_text.replace(old, new, 1)
    return source_text


def revert_gap_name_trace(source_text: str) -> str:
    marker = "/* BLE_LOCK: trace standard GAP name reads without changing their result. */"
    if marker not in source_text:
        return source_text
    hook = (marker + "\n__attribute__((weak)) void\n"
            "ble_lock_gap_name_read(uint16_t handle, uint16_t offset, int status)\n"
            "{ (void)handle; (void)offset; (void)status; }\n\n")
    read_with_trace = ("            rc = ble_svc_gap_device_name_read_access(ctxt);\n"
                       "            ble_lock_gap_name_read(conn_handle, ctxt->offset, rc);")
    if source_text.count(hook) != 1 or source_text.count(read_with_trace) != 1:
        raise ValueError("NimBLE GAP trace does not match the project patch; refusing to alter IDF")
    return source_text.replace(hook, "", 1).replace(
        read_with_trace, "            rc = ble_svc_gap_device_name_read_access(ctxt);", 1)


def revert_read_type_fix(source_text: str) -> str:
    marker = "/* BLE_LOCK: backport mbuf Read By Type for long GAP names. */"
    if marker not in source_text:
        return source_text
    start = source_text.index("static int\nble_att_svr_build_read_type_rsp(")
    end = source_text.index("\nint\nble_att_svr_rx_read_type(", start)
    block = source_text[start:end]
    replacements = (
        ("    " + marker + "\n    struct os_mbuf *attr_om = NULL;", "    uint8_t buf[19];"),
        ("            if (attr_om == NULL) {\n"
         "                attr_om = ble_hs_mbuf_l2cap_pkt();\n"
         "                if (attr_om == NULL) {\n"
         "                    *att_err = BLE_ATT_ERR_INSUFFICIENT_RES;\n"
         "                    *err_handle = entry->ha_handle_id;\n"
         "                    rc = BLE_HS_ENOMEM;\n"
         "                    goto done;\n"
         "                }\n"
         "            } else {\n"
         "                os_mbuf_adj(attr_om, OS_MBUF_PKTLEN(attr_om));\n"
         "            }\n"
         "            rc = ble_att_svr_read(conn_handle, entry, 0, attr_om, att_err);",
         "            rc = ble_att_svr_read_flat(conn_handle, entry, 0, sizeof buf, buf,\n"
         "                                       &attr_len, att_err);"),
        ("            attr_len = OS_MBUF_PKTLEN(attr_om);\n"
         "            if (attr_len > 253) attr_len = 253;\n"
         "            if (attr_len > mtu - 4) {", "            if (attr_len > mtu - 4) {"),
        ("            os_mbuf_copydata(attr_om, 0, attr_len, data->value);",
         "            memcpy(data->value, buf, attr_len);"),
        ("done:\n    os_mbuf_free_chain(attr_om);\n    if (!entry_written) {",
         "done:\n    if (!entry_written) {"),
    )
    for patched, original in replacements:
        if block.count(patched) != 1:
            raise ValueError("NimBLE ATT backport does not match the project patch; refusing to alter IDF")
        block = block.replace(patched, original, 1)
    return source_text[:start] + block + source_text[end:]


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
    original_text = source_text
    gap_source = source.parent.parent / "services" / "gap" / "src" / "ble_svc_gap.c"
    gap_original = gap_source.read_text(encoding="utf-8")
    att_source = source.parent / "ble_att_svr.c"
    att_original = att_source.read_text(encoding="utf-8")
    if PATCH_MARKER not in source_text:
        if source_text.count(OLD_BLOCK) != 1:
            print("NimBLE source does not match the audited 5.4.4 block; refusing to patch.", file=sys.stderr)
            return 2
        source_text = source_text.replace(OLD_BLOCK, NEW_BLOCK, 1)
    try:
        source_text = apply_pairing_gate(source_text)
        gap_text = revert_gap_name_trace(gap_original)
        att_text = revert_read_type_fix(att_original)
    except ValueError as error:
        print(str(error), file=sys.stderr)
        return 2
    if source_text == original_text and gap_text == gap_original and att_text == att_original:
        print("NimBLE BLE security guards active; retired GAP-name backports absent (ESP-IDF 5.4.4).")
        return 0
    source.write_text(source_text, encoding="utf-8", newline="")
    gap_source.write_text(gap_text, encoding="utf-8", newline="")
    att_source.write_text(att_text, encoding="utf-8", newline="")
    print(f"Applied BLE security guard and removed retired GAP-name backports under {root}.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
