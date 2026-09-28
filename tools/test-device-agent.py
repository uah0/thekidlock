"""Проверки клиента на локальном HTTP-стенде, без обращения к ESP32.
Запуск: python tools/test-device-agent.py (требуется pwsh).
"""
import json
import shutil
import subprocess
import threading
import unittest
import uuid
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlsplit

ROOT = Path(__file__).resolve().parents[1]


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *_):
        pass

    def reply(self, obj, code=200, headers=None):
        body = (obj if isinstance(obj, str) else json.dumps(obj)).encode()
        self.send_response(code)
        self.send_header('Content-Type', 'text/plain' if isinstance(obj, str) else 'application/json')
        self.send_header('Content-Length', str(len(body)))
        for key, value in (headers or {}).items():
            self.send_header(key, value)
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        path = urlsplit(self.path).path
        state = self.server.state
        state['requests'].append(('GET', path))
        if state.get('offline'):
            return self.reply({'error': 'offline'}, 503)
        if path == '/api/agent/info':
            if state.get('legacy'):
                return self.reply({'api_version': 1, 'device_type': 'ble_lock_esp32s3',
                                   'firmware': state['version'], 'build': 'legacy'})
            return self.reply({'agent_api_version': 1, 'device_type': 'relay_controller',
                               'device_id': state['id'], 'device_name': 'Test relay',
                               'firmware_version': state['version'], 'boot_id': state['boot']})
        if path == '/api/agent/capabilities':
            return self.reply({'api_version': 1, 'features': {'status': True, 'events': True,
                               'monitor': True, 'restart': True, 'ota': True},
                               'commands': [{'name': 'relay_on', 'description': 'Enable relay'}]})
        if path in ('/api/agent/status', '/api/status'):
            return self.reply({'enabled': True})
        if path == '/api/agent/events':
            return self.reply(['event'])
        if path == '/api/agent/monitor':
            return self.reply('runtime log\n', headers={'X-Log-Cursor': '12',
                              'X-Log-Oldest': '0', 'X-Log-Dropped': '0'})
        return self.reply({'error': 'Unknown URI'}, 404)

    def do_POST(self):
        state = self.server.state
        uri = urlsplit(self.path)
        state['requests'].append(('POST', uri.path))
        data = self.rfile.read(int(self.headers.get('Content-Length', 0)))
        if uri.path == '/api/agent/command':
            args = parse_qs(uri.query)
            state['command'] = args
            if args['action'] == ['unknown']:
                return self.reply({'error': 'Unknown action'}, 400)
            return self.reply({'accepted': True, 'action': args['action'][0]})
        if uri.path in ('/api/agent/restart', '/api/system/restart'):
            return self.reply({'ok': True, 'accepted': True})
        if uri.path in ('/api/agent/ota', '/api/ota'):
            state['image'] = data
            mode = state.get('mode', 'upgrade')
            if mode == 'upgrade':
                state['version'] = 'release-2026.09'
            if mode not in ('no-reboot', 'offline'):
                state['boot'] = 'boot-2'
            if mode == 'wrong-device':
                state['id'] = 'another-device'
            if mode == 'offline':
                state['offline'] = True
            return self.reply('Firmware written')
        return self.reply({'error': 'Unknown URI'}, 404)


class ClientTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.work = ROOT / '.device-agent' / ('client-tests-' + uuid.uuid4().hex)
        cls.work.mkdir(parents=True)
        cls.client = cls.work / 'device-agent.ps1'
        shutil.copyfile(ROOT / 'device-agent.ps1', cls.client)
        cls.image = cls.work / 'firmware.bin'
        cls.image.write_bytes(b'Test image, never sent to hardware')
        cls.server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        cls.base = f'http://127.0.0.1:{cls.server.server_port}'
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()
        cls.thread.join()

    def setUp(self):
        self.server.state = {'id': 'relay-1', 'version': '2.7-beta', 'boot': 'boot-1', 'requests': []}

    def run_client(self, *args, ok=True, expression=None):
        if expression:
            # Paths and expression here are fixed test inputs, never device data.
            command = ['pwsh', '-NoProfile', '-Command',
                       f"& '{self.client}' -Device '{self.base}' {expression}"]
        else:
            command = ['pwsh', '-NoProfile', '-File', str(self.client), *args, '-Device', self.base]
        result = subprocess.run(command, capture_output=True, text=True, encoding='utf-8', timeout=20)
        self.assertEqual(result.returncode == 0, ok, result.stdout + result.stderr)
        return result

    @staticmethod
    def result_json(result):
        return json.loads(result.stdout[result.stdout.index('{'):])

    def test_generic_discovery_info_and_capabilities(self):
        info = self.result_json(self.run_client('info'))
        self.assertEqual(info['firmware_version'], '2.7-beta')
        self.assertEqual(info['device_type'], 'relay_controller')
        caps = self.result_json(self.run_client('capabilities'))
        self.assertEqual(caps['commands'][0]['name'], 'relay_on')

    def test_snapshot_without_lock_endpoints(self):
        snap = self.result_json(self.run_client('snapshot'))
        self.assertTrue(snap['status']['enabled'])
        self.assertEqual(snap['events'], ['event'])
        self.assertNotIn(('GET', '/api/ulp'), self.server.state['requests'])

    def test_generic_command_and_encoded_arguments(self):
        self.run_client('command', 'relay_on')
        self.assertEqual(self.server.state['command'], {'action': ['relay_on']})
        self.run_client(expression="command relay_on -Arguments @{value='a&b +%=тест'}")
        self.assertEqual(self.server.state['command']['value'], ['a&b +%=тест'])
        self.run_client('command', 'unknown', ok=False)

    def test_lock_aliases(self):
        for alias in ('open', 'close', 'auto', 'always-open', 'toggle-hold-open',
                      'arm-open', 'disable-auto-close', 'ulp-lock', 'ulp-unlock'):
            with self.subTest(alias=alias):
                self.run_client(alias)
                self.assertEqual(self.server.state['command']['action'], [alias.replace('-', '_')])
        self.run_client('buzzer', '-BuzzerPattern', '7')
        self.assertEqual(self.server.state['command']['value'], ['7'])
        self.run_client('diagnostics-on')
        self.assertEqual(self.server.state['command']['value'], ['1'])
        self.run_client('diagnostics-off')
        self.assertEqual(self.server.state['command']['value'], ['0'])

    def test_status_log_restart_routes(self):
        self.run_client('status')
        self.assertIn(('GET', '/api/agent/status'), self.server.state['requests'])
        self.assertIn('runtime log', self.run_client('log').stdout)
        self.run_client('restart')
        self.assertIn(('POST', '/api/agent/restart'), self.server.state['requests'])

    def test_flash_generic_versions_and_bytes(self):
        result = self.result_json(self.run_client('flash', str(self.image)))
        self.assertEqual(result['previous_version'], '2.7-beta')
        self.assertEqual(result['current_version'], 'release-2026.09')
        self.assertEqual(self.server.state['image'], self.image.read_bytes())

    def test_same_version_reflash(self):
        self.server.state['mode'] = 'same-version'
        result = self.result_json(self.run_client('flash', '-Firmware', str(self.image)))
        self.assertTrue(result['flashed'])
        self.assertFalse(result['version_changed'])

    def test_flash_failure_cases(self):
        for mode in ('offline', 'no-reboot', 'wrong-device'):
            with self.subTest(mode=mode):
                self.setUp()
                self.server.state['mode'] = mode
                self.run_client('flash', str(self.image), '-RestartTimeoutSec', '1', ok=False)

    def test_original_agent_compatibility(self):
        self.server.state['legacy'] = True
        self.run_client('status')
        self.run_client('restart')
        self.run_client('flash', str(self.image))
        self.assertIn(('GET', '/api/status'), self.server.state['requests'])
        self.assertIn(('POST', '/api/system/restart'), self.server.state['requests'])
        self.assertIn(('POST', '/api/ota'), self.server.state['requests'])


if __name__ == '__main__':
    unittest.main(verbosity=2)
