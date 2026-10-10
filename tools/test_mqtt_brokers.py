"""Проверки MQTT CONNACK на локальном сервере; внешние брокеры не используются."""
import importlib.util
from pathlib import Path
import socketserver
import subprocess
import sys
import tempfile
import json
import threading
import time
import unittest

spec = importlib.util.spec_from_file_location('checker', Path(__file__).with_name('check_mqtt_brokers.py'))
checker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(checker)

class Handler(socketserver.BaseRequestHandler):
    def handle(self):
        packet = self.request.recv(1024)
        self.server.packets.append(packet)
        if self.server.answer is not None:
            self.request.sendall(self.server.answer)
            self.request.recv(1024)
        else:
            time.sleep(0.4)

class BrokerTests(unittest.TestCase):
    def check_response(self, answer):
        with socketserver.ThreadingTCPServer(('127.0.0.1', 0), Handler) as server:
            server.answer = answer
            server.packets = []
            worker = threading.Thread(target=server.serve_forever, daemon=True)
            worker.start()
            try:
                result = checker.probe('127.0.0.1', server.server_address[1], timeout=0.2)
            finally:
                server.shutdown()
                worker.join()
            packet = server.packets[0]
            self.assertEqual(packet[0], 0x10)
            # Short CONNECT remaining-length is one byte; protocol header follows.
            self.assertEqual(packet[4:8], b'MQTT')
            self.assertEqual(packet[8], 4)  # MQTT 3.1.1
            self.assertEqual(packet[9] & 0xc0, 0)  # no username/password
            return result

    def test_successful_connack(self):
        self.assertTrue(self.check_response(bytes([0x20, 2, 0, 0]))['accepted'])

    def test_authentication_required(self):
        result = self.check_response(bytes([0x20, 2, 0, 4]))
        self.assertFalse(result['accepted'])
        self.assertIn('CONNACK', result['reason'])

    def test_tcp_connection_without_connack_is_rejected(self):
        self.assertFalse(self.check_response(None)['accepted'])

    def test_empty_list_blocks_build_and_does_not_replace_header(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / 'addresses.txt'
            source.write_text('bad address\n', encoding='utf-16')
            header = root / 'mqtt_brokers.h'
            header.write_text('old header')
            report = root / 'report.json'
            result = subprocess.run([sys.executable, str(Path(checker.__file__)),
                                     '--input', str(source), '--header', str(header),
                                     '--report', str(report)], capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(header.read_text(), 'old header')
            self.assertEqual(json.loads(report.read_text(encoding='utf-8'))['accepted'], [])

    def test_parse_duplicates_and_invalid_lines(self):
        addresses, invalid, duplicates = checker.parse_addresses(
            'broker.example\nBROKER.EXAMPLE\n\n192.168.1.10\n bad.example\nmqtt://bad.example\nbad.example:1883\n')
        self.assertEqual(addresses, ['broker.example', '192.168.1.10'])
        self.assertEqual(len(invalid), 3)
        self.assertEqual(len(duplicates), 1)

if __name__ == '__main__':
    unittest.main()
