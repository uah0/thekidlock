"""Управление через ThingSpeak HTTPS → MQTT TLS → ESP, стандартная библиотека."""
import argparse
import getpass
import json
import os
from pathlib import Path
import re
import ssl
import time
import urllib.error
import urllib.parse
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
MAX_ID = 2**53 - 1


def make_command(mac, command_id, operation, actor, now=None, ttl=45):
    mac = mac.lower()
    if not re.fullmatch(r"(?:[0-9a-f]{2}:){5}[0-9a-f]{2}", mac):
        raise ValueError("Некорректный MAC")
    if type(command_id) is not int or not 0 < command_id <= MAX_ID:
        raise ValueError("Некорректный ID")
    if operation not in ("open", "close", "hold_on", "hold_off"):
        raise ValueError("Неизвестная операция")
    if not re.fullmatch(r"[A-Za-z0-9_.-]{1,17}", actor):
        raise ValueError("Некорректная метка оператора")
    if type(ttl) is not int or not 1 <= ttl <= 60:
        raise ValueError("Срок действия: 1–60 секунд")
    now = int(time.time()) if now is None else now
    return dict(field1=mac, field2=str(command_id), field3=operation,
                field4=str(now + ttl), field5=str(now), field6=actor)


def request_json(url, body=None):
    parsed = urllib.parse.urlsplit(url)
    if parsed.scheme != "https" or parsed.hostname != "api.thingspeak.com":
        raise ValueError("Разрешён только HTTPS api.thingspeak.com")
    class NoRedirect(urllib.request.HTTPRedirectHandler):
        def redirect_request(self, req, fp, code, msg, headers, newurl):
            raise ValueError("Переадресация запрещена")
    opener = urllib.request.build_opener(NoRedirect(),
        urllib.request.HTTPSHandler(context=ssl.create_default_context()))
    data = urllib.parse.urlencode(body).encode("ascii") if body is not None else None
    req = urllib.request.Request(url, data=data)
    with opener.open(req, timeout=8) as response:
        raw = response.read(65537)
        if len(raw) > 65536:
            raise ValueError("Слишком большой ответ")
        return json.loads(raw)


def verified_result(feed, command, command_started):
    if not isinstance(feed, dict):
        return None
    if feed.get("field1") != command["field2"] or feed.get("field4") != command["field1"]:
        return None
    try:
        if int(feed["field5"]) < command_started - 5:
            return None
    except (KeyError, TypeError, ValueError):
        return None
    result = feed.get("field2")
    if result not in ("ok", "fault", "timeout", "queue_failed", "hold_conflict", "interrupted", "mode_nvs"):
        return None
    if result == "ok":
        states = {"open": {"OPEN", "OPEN_HOLD"}, "close": {"LOCKED"},
                  "hold_on": {"OPEN_HOLD"}, "hold_off": {"OPEN", "LOCKED"}}
        if feed.get("field3") not in states[command["field3"]] or feed.get("field6") != "0":
            return None
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("operation", choices=("open", "close", "hold_on", "hold_off"))
    parser.add_argument("--mac", required=True)
    parser.add_argument("--command-channel", type=int, required=True)
    parser.add_argument("--result-channel", type=int, required=True)
    parser.add_argument("--actor", default="owner")
    parser.add_argument("--id", type=int, help="Повтор сохранённого запроса")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    if not 0 < args.command_channel <= 2**32 - 1 or not 0 < args.result_channel <= 2**32 - 1:
        parser.error("Нужны положительные номера каналов")
    if args.command_channel == args.result_channel:
        parser.error("Нужны отдельные каналы")
    command_id = args.id if args.id is not None else time.time_ns() // 1000
    command = make_command(args.mac, command_id, args.operation, args.actor)
    state_dir = ROOT / ".device-agent" / "cloud-client"
    filename = state_dir / (command["field1"].replace(":", "") + ".json")
    if args.id is not None:
        if not filename.exists():
            raise ValueError("Нет сохранённого запроса")
        saved = json.loads(filename.read_text(encoding="utf-8"))
        if saved.get("field2") != str(args.id) or saved.get("field1") != command["field1"] or saved.get("field3") != args.operation:
            raise ValueError("ID можно повторить только с исходной операцией")
        command = saved
    if args.dry_run:
        print(json.dumps(command, ensure_ascii=False, indent=2))
        return 0
    if int(command["field4"]) <= time.time():
        raise ValueError("Срок истёк; сначала проверьте результат в облаке")
    write_key = os.environ.get("THINGSPEAK_COMMAND_WRITE_KEY") or getpass.getpass("Write API key канала команд: ")
    read_key = os.environ.get("THINGSPEAK_RESULT_READ_KEY") or getpass.getpass("Read API key канала результатов: ")
    if not re.fullmatch(r"[A-Za-z0-9]{1,64}", write_key) or not re.fullmatch(r"[A-Za-z0-9]{1,64}", read_key):
        raise ValueError("Некорректный API key")
    state_dir.mkdir(parents=True, exist_ok=True)
    temp = filename.with_suffix(".tmp")
    temp.write_text(json.dumps(command), encoding="utf-8")
    temp.replace(filename)
    print(f"ID {command['field2']}: ожидание фактического результата ESP…")
    url = ("https://api.thingspeak.com/channels/" + str(args.result_channel) +
           "/feeds.json?" + urllib.parse.urlencode(dict(api_key=read_key, results=20)))
    try:
        response = request_json("https://api.thingspeak.com/update.json", dict(api_key=write_key, **command))
        if not isinstance(response, dict) or not response.get("entry_id"):
            print("Запись команды не подтверждена; проверяем результат.")
    except (urllib.error.URLError, TimeoutError, ValueError):
        print("Ответ отправки не получен; проверяем тот же ID.")
    deadline = time.monotonic() + 75
    while time.monotonic() < deadline:
        try:
            response = request_json(url)
            for feed in response.get("feeds", []):
                result = verified_result(feed, command, int(command["field5"]))
                if result:
                    print(f"ID {command['field2']}: {result}, состояние {feed.get('field3')}")
                    return 0 if result == "ok" else 2
        except (urllib.error.URLError, TimeoutError, ValueError, AttributeError):
            pass
        time.sleep(3)
    print(f"Нет подтверждения ID {command['field2']}. Выполнение неизвестно; проверьте канал результатов.")
    return 3


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (ValueError, OSError):
        # No credential-bearing URLs or request exceptions in console output.
        print("Ошибка параметров или сети. Проверьте настройки; ключи не выводятся.")
        raise SystemExit(1)
