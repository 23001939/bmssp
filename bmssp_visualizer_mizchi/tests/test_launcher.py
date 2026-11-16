"""Unit tests for start_demo.py launcher."""
from __future__ import annotations

import sys
import unittest
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))

import start_demo


class TestLauncher(unittest.TestCase):
    def test_01_constants_and_paths(self):
        self.assertEqual(start_demo.DEFAULT_PORT, 8766)
        self.assertTrue((start_demo.ROOT / 'server.py').exists())
        self.assertTrue((start_demo.ROOT / 'dist' / 'index.html').exists())

    def test_02_is_ready_active_server(self):
        # Kiểm tra logic is_ready với mock server response hợp lệ
        with patch('urllib.request.urlopen') as mock_urlopen:
            mock_resp = mock_urlopen.return_value.__enter__.return_value
            mock_resp.read.return_value = b'{"datasets": [{"id": "demo", "name": "demo"}]}'
            self.assertTrue(start_demo.is_ready('http://127.0.0.1:8766'))

    def test_03_is_ready_invalid_port(self):
        base_url = 'http://127.0.0.1:59999'
        ready = start_demo.is_ready(base_url)
        self.assertFalse(ready, "Cổng không mở phải trả về False.")

    def test_04_cli_status_running(self):
        with patch.object(start_demo, 'is_ready', return_value=True), \
             patch.object(start_demo, 'get_running_pid', return_value=12345), \
             patch('sys.argv', ['start_demo.py', '--status']), \
             patch('builtins.print') as mock_print:
            start_demo.main()
            printed = " ".join(str(call.args[0]) for call in mock_print.call_args_list if call.args)
            self.assertIn("Server BMSSP Mizchi đang hoạt động", printed)

    def test_05_cli_status_stopped(self):
        with patch.object(start_demo, 'is_ready', return_value=False), \
             patch.object(start_demo, 'get_running_pid', return_value=None), \
             patch('sys.argv', ['start_demo.py', '--status']), \
             patch('builtins.print') as mock_print:
            start_demo.main()
            printed = " ".join(str(call.args[0]) for call in mock_print.call_args_list if call.args)
            self.assertIn("Server không sẵn sàng", printed)


if __name__ == '__main__':
    unittest.main()

