"""Launcher for BMSSP Mizchi Visualizer local demo.

Starts background server if not already running on port 8766,
and opens the interactive demo in the default browser.
"""
from __future__ import annotations

import argparse
import json
import os
import signal
import subprocess
import sys
import time
import urllib.request
from pathlib import Path
from urllib.parse import urlencode
import webbrowser

ROOT = Path(__file__).resolve().parent
PID_FILE = ROOT / 'server.pid'
LOG_FILE = ROOT / 'server.log'
DEFAULT_PORT = 8766


def is_ready(base_url: str) -> bool:
    try:
        req = urllib.request.Request(f'{base_url}/api/datasets', headers={'User-Agent': 'MizchiLauncher/1.0'})
        with urllib.request.urlopen(req, timeout=1.5) as resp:
            data = json.load(resp)
            return any(item.get('id') == 'demo' for item in data.get('datasets', []))
    except Exception:
        return False


def get_running_pid() -> int | None:
    if not PID_FILE.exists():
        return None
    try:
        pid = int(PID_FILE.read_text(encoding='ascii').strip())
        # Check if process exists on Windows
        if sys.platform == 'win32':
            import ctypes
            PROCESS_QUERY_LIMITED_INFORMATION = 0x1000
            h = ctypes.windll.kernel32.OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, False, pid)
            if h:
                ctypes.windll.kernel32.CloseHandle(h)
                return pid
            return None
        else:
            os.kill(pid, 0)
            return pid
    except Exception:
        return None


def stop_server() -> bool:
    pid = get_running_pid()
    if pid is None:
        if PID_FILE.exists():
            PID_FILE.unlink(missing_ok=True)
        return False
    try:
        if sys.platform == 'win32':
            subprocess.run(['taskkill', '/F', '/T', '/PID', str(pid)], capture_output=True, check=False)
        else:
            os.kill(pid, signal.SIGTERM)
        PID_FILE.unlink(missing_ok=True)
        return True
    except Exception:
        return False


def start_server(port: int) -> subprocess.Popen:
    base_url = f'http://127.0.0.1:{port}'
    if is_ready(base_url):
        return None

    # Clear old PID if stale
    stop_server()

    log_handle = LOG_FILE.open('a', encoding='utf-8')
    log_handle.write(f"\n--- Server khởi động lúc {time.strftime('%Y-%m-%d %H:%M:%S')} ---\n")
    log_handle.flush()

    flags = 0
    if sys.platform == 'win32':
        DETACHED_PROCESS = 0x00000008
        CREATE_NEW_PROCESS_GROUP = 0x00000200
        flags = getattr(subprocess, 'CREATE_NO_WINDOW', 0) | DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP
    proc = subprocess.Popen(
        [sys.executable, '-X', 'utf8', str(ROOT / 'server.py'), '--port', str(port)],
        cwd=ROOT,
        stdout=log_handle,
        stderr=subprocess.STDOUT,
        creationflags=flags
    )
    PID_FILE.write_text(str(proc.pid), encoding='ascii')
    log_handle.close()

    deadline = time.monotonic() + 30.0
    while not is_ready(base_url):
        if proc.poll() is not None:
            raise RuntimeError(f'Server dừng bất ngờ với mã lỗi {proc.returncode}. Xem {LOG_FILE.name} để biết chi tiết.')
        if time.monotonic() > deadline:
            raise TimeoutError(f'Quá thời gian 30s chờ server khởi động tại {base_url}. Xem {LOG_FILE.name}.')
        time.sleep(0.2)

    return proc


def main():
    parser = argparse.ArgumentParser(description='Khởi động BMSSP Mizchi Lab visualizer cục bộ (port 8766).')
    parser.add_argument('--port', type=int, default=DEFAULT_PORT, help='Cổng HTTP máy chủ (mặc định: 8766)')
    parser.add_argument('--dataset', type=str, default='demo', help='ID bộ dữ liệu ban đầu (demo, weighted_100, ...)')
    parser.add_argument('--algorithm', type=str, choices=['dmm25', 'chd', 'dijkstra'], default='dmm25',
                        help='Thuật toán ban đầu (dmm25, chd, dijkstra)')
    parser.add_argument('--no-browser', action='store_true', help='Chỉ đảm bảo server chạy, không tự mở trình duyệt')
    parser.add_argument('--no-autoplay', action='store_true', help='Mở giao diện nhưng không tự động bấm chạy demo live')
    parser.add_argument('--stop', action='store_true', help='Dừng tiến trình server nền đang chạy')
    parser.add_argument('--status', action='store_true', help='Kiểm tra trạng thái server đang chạy')
    args = parser.parse_args()

    base_url = f'http://127.0.0.1:{args.port}'

    if args.stop:
        if stop_server():
            print(f'Đã dừng server BMSSP Mizchi (PID file: {PID_FILE.name}).')
        else:
            print('Không có server BMSSP Mizchi nào đang chạy.')
        return

    if args.status:
        pid = get_running_pid()
        ready = is_ready(base_url)
        if ready:
            print(f'Server BMSSP Mizchi đang hoạt động tại {base_url} (PID: {pid or "ngoài PID file"}).')
        else:
            print(f'Server không sẵn sàng tại {base_url}. (PID file ghi: {pid or "trống"}).')
        return

    # Khởi động server nếu cần
    if not is_ready(base_url):
        print(f'Đang khởi động BMSSP Mizchi Server trên cổng {args.port}...', end='', flush=True)
        start_server(args.port)
        print(' [OK]')
    else:
        print(f'Server BMSSP Mizchi đã sẵn sàng tại {base_url}.')

    # Dựng URL mở giao diện
    params = {}
    if args.dataset:
        params['dataset'] = args.dataset
    if args.algorithm and args.algorithm != 'dmm25':
        params['algorithm'] = args.algorithm
    if not args.no_autoplay:
        params['demo'] = 'live'

    target_url = base_url + ('/' if not params else '/?' + urlencode(params))
    print(f'Giao diện trực quan: {target_url}', flush=True)

    if not args.no_browser:
        webbrowser.open(target_url)


if __name__ == '__main__':
    main()
