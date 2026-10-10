"""Проверка CONNACK без авторизации и генерация списка MQTT для прошивки."""
import argparse
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import sys
import threading
import uuid

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / '.eim-codex/python-deps'))
from paho.mqtt import client as mqtt


def parse_addresses(text):
    addresses, invalid, duplicates = [], [], []
    seen = set()
    for number, address in enumerate(text.splitlines(), 1):
        if not address:
            continue
        if not re.fullmatch(r'[A-Za-z0-9.-]{1,192}', address):
            invalid.append({'line': number, 'address': address, 'accepted': False,
                            'reason': 'Недопустимый адрес: ожидается домен или IPv4 без пробелов/порта/схемы'})
            continue
        if address.lower() in seen:
            duplicates.append({'line': number, 'address': address})
            continue
        seen.add(address.lower())
        addresses.append(address)
    return addresses, invalid, duplicates


def probe(address, port, timeout=5):
    result = {'address': address, 'accepted': False, 'reason': 'Нет CONNACK до истечения срока'}
    done = threading.Event()
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2,
                         client_id='thekidlock-check-' + uuid.uuid4().hex[:20],
                         clean_session=True, protocol=mqtt.MQTTv311,
                         reconnect_on_failure=False)
    client.connect_timeout = timeout

    def on_connect(client, userdata, flags, reason_code, properties):
        result['accepted'] = reason_code == 0
        result['reason'] = 'CONNACK: ' + str(reason_code)
        done.set()

    client.on_connect = on_connect
    try:
        client.connect(address, port, keepalive=15)
        client.loop_start()
        done.wait(timeout)
    except (OSError, ValueError) as error:
        result['reason'] = str(error)
    finally:
        client.disconnect()
        client.loop_stop()
    return result


def write_changed(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists() or path.read_text(encoding='utf-8') != text:
        temporary = path.with_suffix(path.suffix + '.tmp')
        temporary.write_text(text, encoding='utf-8')
        temporary.replace(path)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--input', type=Path, required=True)
    parser.add_argument('--header', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    source = args.input.read_bytes()
    addresses, invalid, duplicates = parse_addresses(source.decode('utf-16' if source.startswith((b'\xff\xfe', b'\xfe\xff')) else 'utf-8-sig'))
    port = int(re.search(r'^#define MQTT_PORT\s+(\d+)',
                        (ROOT / 'main/mqtt_config.h').read_text(), re.M)[1])
    with ThreadPoolExecutor(max_workers=4) as pool:
        results = list(pool.map(lambda host: probe(host, port), addresses))
    accepted = [item['address'] for item in results if item['accepted']]
    report = {'checked_at_utc': datetime.now(timezone.utc).isoformat(),
              'source_sha256': hashlib.sha256(source).hexdigest(),
              'protocol': 'MQTT 3.1.1', 'port': port, 'authentication': False,
              'criterion': 'Успешный CONNACK, без подписки и публикации',
              'accepted': accepted, 'results': results + invalid, 'duplicates': duplicates}
    write_changed(args.report, json.dumps(report, ensure_ascii=False, indent=2) + '\n')
    for item in results + invalid:
        print(('PASS ' if item['accepted'] else 'FAIL ') + item['address'] + ': ' + item['reason'])
    if not accepted:
        raise SystemExit('Ни один MQTT-брокер не прошёл проверку; сборка остановлена')
    choices = json.dumps(accepted, separators=(',', ':'))
    write_changed(args.header, '#pragma once\n#define MQTT_BROKER_CHOICES_JSON ' + json.dumps(choices) + '\n')
    print('MQTT brokers accepted: ' + str(len(accepted)))


if __name__ == '__main__':
    main()
